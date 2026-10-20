/*
 * wcnd: the user half of the WCN3990 Wi-Fi (port/clover/hw-wifi.md,
 * section 9).  The WLAN firmware runs in the modem and is brought up over
 * QMI (wlfw, service 0x45) on a QRTR socket - a socket a service that VFS
 * itself waits on should not hold, so it is held here; what needs rights
 * (TrustZone, the hardware) is asked of the wcn service on /dev/wlan0.
 *
 *	wcnd [-f] [-v] [-d fwdir] [-b board-2.bin] [-n]
 *
 *	-f	stay in the foreground
 *	-v	print the QMI exchange
 *	-d	the modem's firmware directory, where bdwlan.* lie
 *		(/libdata/firmware/qcom/clover/modem)
 *	-b	a board-2.bin to fall back on
 *		(/libdata/firmware/ath10k/WCN3990/hw1.0/board-2.bin)
 *	-n	stop after FW_READY: no supplies, no WLAN_MODE, no engines
 *
 * The order is Linux's (ath10k qmi.c and snoc.c): IND_REGISTER, HOST_CAP,
 * MSA_INFO, the MSA given through TrustZone (by the service), MSA_READY,
 * CAP, the board data, CAL_REPORT, FW_READY; then the supplies, WLAN_CFG,
 * WLAN_MODE, and the service starts the copy engines.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netqrtr/qrtr.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "wcn_wlfw.h"
#include "wcn_fw.h"
#include "wcn_ioc.h"

#define FWDIR	"/libdata/firmware/qcom/clover/modem"
#define BOARD2	"/libdata/firmware/ath10k/WCN3990/hw1.0/board-2.bin"
#define DEV	"/dev/wlan0"

static int verbose, fg, stop_at_ready;
static const char *fwdir = FWDIR, *board2 = BOARD2;
static int s, dev = -1;
static struct sockaddr_qrtr fw;
static uint16_t txn = 1;
static uint8_t out[WLFW_MSG_MAX], in[WLFW_MSG_MAX];

static void
say(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (fg) {
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
	} else
		vsyslog(pri, fmt, ap);
	va_end(ap);
}

static void
dump(const char *dir, const uint8_t *p, size_t n)
{
	size_t i;

	if (!verbose)
		return;
	fprintf(stderr, "  %s", dir);
	for (i = 0; i < n && i < 64; i++)
		fprintf(stderr, " %02x", p[i]);
	fprintf(stderr, "%s\n", n > 64 ? " ..." : "");
}

static int
get(void *buf, size_t len, struct sockaddr_qrtr *from, int ms)
{
	struct pollfd p = { s, POLLIN, 0 };
	socklen_t fl = sizeof(*from);

	if (poll(&p, 1, ms) != 1)
		return -1;
	return (int)recvfrom(s, buf, len, 0, (struct sockaddr *)from, &fl);
}

static int fw_ready_seen;

/* Send "out"; the answer of this transaction into "m".  Indications on the
 * way are noted (FW_READY). */
static int
ask(size_t n, struct qmi_msg *m, const char *what)
{
	struct sockaddr_qrtr from;
	int r;

	if (n == 0) {
		say(LOG_ERR, "%s: could not build", what);
		return -1;
	}
	dump(">", out, n);
	if (sendto(s, out, n, 0, (struct sockaddr *)&fw, sizeof(fw)) !=
	    (ssize_t)n) {
		say(LOG_ERR, "%s: sendto: %s", what, strerror(errno));
		return -1;
	}
	for (;;) {
		if ((r = get(in, sizeof(in), &from, 30000)) < 0) {
			say(LOG_ERR, "%s: no answer in 30 s", what);
			return -1;
		}
		if (from.sq_node != fw.sq_node || from.sq_port != fw.sq_port)
			continue;
		dump("<", in, r);
		if (qmi_parse(in, r, m) != 0)
			continue;
		if (m->type == QMI_INDICATION) {
			if (m->id == WLFW_FW_READY_IND)
				fw_ready_seen = 1;
			continue;
		}
		if (m->type == QMI_RESPONSE && m->txn == txn)
			break;
	}
	txn++;
	return 0;
}

/* The common answer: 0, or the error (and said). */
static int
result(struct qmi_msg *m, const char *what, int quiet_err)
{
	unsigned res, err;

	if (qmi_result(m, &res, &err) != 0) {
		say(LOG_ERR, "%s: no result", what);
		return -1;
	}
	if (res != 0 && (int)err != quiet_err)
		say(LOG_WARNING, "%s: refused, error %u", what, err);
	return res == 0 ? 0 : (int)err;
}

static uint8_t *
slurp(const char *path, size_t *len)
{
	FILE *fp;
	uint8_t *f;
	long n;

	if ((fp = fopen(path, "rb")) == NULL)
		return NULL;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	f = n > 0 ? malloc(n) : NULL;
	if (f == NULL || fread(f, 1, n, fp) != (size_t)n) {
		free(f);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	*len = n;
	return f;
}

/*
 * The board data for "board": the modem's own bdwlan.<board> first (the
 * kit carries no firmware; the tablet's partition has these), board-2.bin
 * after.  *f is to be freed.
 */
static int
board_data(unsigned board, unsigned chip, uint8_t **f, const uint8_t **bd,
	size_t *bdl, char *from, size_t fsz)
{
	char p[PATH_MAX], bn[32], name[WCN_BD_NAME_MAX];
	size_t n;
	int which, e = ENOENT;

	if (wcn_bdf_name(bn, sizeof(bn), board) > 0) {
		snprintf(p, sizeof(p), "%s/%s", fwdir, bn);
		if ((*f = slurp(p, &n)) != NULL) {
			*bd = *f;
			*bdl = n;
			snprintf(from, fsz, "%s", p);
			return 0;
		}
	}
	if ((*f = slurp(board2, &n)) == NULL)
		return ENOENT;
	for (which = 0; which < 3 && e == ENOENT; which++) {
		if (wcn_bd_name(name, sizeof(name), which, board, chip, NULL) <
		    0)
			continue;
		e = wcn_bd_find(*f, n, name, bd, bdl);
	}
	if (e == 0)
		snprintf(from, fsz, "%s, '%s'", board2, name);
	return e;
}

static int
find_wlfw(int wait_s)
{
	struct sockaddr_qrtr me, to, from;
	struct qrtr_ctrl_pkt pkt;
	socklen_t len = sizeof(me);
	int t;

	if ((s = socket(AF_QIPCRTR, SOCK_DGRAM, 0)) < 0) {
		say(LOG_ERR, "socket(AF_QIPCRTR): %s", strerror(errno));
		return -1;
	}
	memset(&me, 0, sizeof(me));
	me.sq_len = sizeof(me);
	me.sq_family = AF_QIPCRTR;
	me.sq_node = 1;
	if (bind(s, (struct sockaddr *)&me, sizeof(me)) != 0 ||
	    getsockname(s, (struct sockaddr *)&me, &len) != 0) {
		say(LOG_ERR, "bind: %s", strerror(errno));
		return -1;
	}
	to = me;
	to.sq_port = QRTR_PORT_CTRL;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = WLFW_SERVICE;
	(void)sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr *)&to,
	    sizeof(to));
	/* Servers known now, the empty one, then new ones as they come. */
	for (t = 0; t < wait_s; ) {
		if (get(&pkt, sizeof(pkt), &from, 1000) != sizeof(pkt)) {
			t++;
			continue;
		}
		if (pkt.cmd == QRTR_TYPE_NEW_SERVER && pkt.server.port != 0 &&
		    pkt.server.service == WLFW_SERVICE) {
			memset(&fw, 0, sizeof(fw));
			fw.sq_len = sizeof(fw);
			fw.sq_family = AF_QIPCRTR;
			fw.sq_node = pkt.server.node;
			fw.sq_port = pkt.server.port;
			say(LOG_INFO, "wlfw at %u:%u", fw.sq_node, fw.sq_port);
			return 0;
		}
	}
	say(LOG_ERR, "no wlfw in %d s (is the modem up?)", wait_s);
	return -1;
}

static int
bring_up(void)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct qmi_msg m;
	struct wlfw_region r[WLFW_MSA_REGIONS];
	struct wlfw_cap c;
	struct wcn_msa msa;
	struct wcn_region g;
	struct sockaddr_qrtr from;
	const uint8_t *bd;
	uint8_t *f = NULL;
	size_t bdl, off, piece;
	char src[PATH_MAX + 128];
	uint64_t st;
	uint32_t seg;
	unsigned n, i, board = 0xff, chip = 0x30214;
	int e, on, k;

	/* IND_REGISTER: fw_ready and msa_ready to this client. */
	if (ask(wlfw_ind_register(&b, txn), &m, "IND_REGISTER") != 0 ||
	    result(&m, "IND_REGISTER", -1) != 0 ||
	    wlfw_ind_register_resp(&m, &st) != 0)
		return -1;
	say(LOG_INFO, "fw_status 0x%llx", (unsigned long long)st);
	if (st & WLFW_FW_STATUS_READY) {
		say(LOG_INFO, "firmware up already: WLAN_CFG next");
		goto cfg;
	}

	/* HOST_CAP: this firmware does not know it (NOT_SUPPORTED). */
	if (ask(wlfw_host_cap(&b, txn), &m, "HOST_CAP") != 0 ||
	    ((e = result(&m, "HOST_CAP", QMI_ERR_NOT_SUPPORTED)) != 0 &&
	    e != QMI_ERR_NOT_SUPPORTED))
		return -1;

	/* MSA: where, what the firmware wants, given by the service. */
	if (ioctl(dev, WCNIOC_MSA, &msa) != 0) {
		say(LOG_ERR, "%s: MSA: %s", DEV, strerror(errno));
		return -1;
	}
	if (ask(wlfw_msa_info(&b, txn, msa.pa, msa.size), &m, "MSA_INFO") !=
	    0 || result(&m, "MSA_INFO", -1) != 0 ||
	    wlfw_msa_info_resp(&m, msa.pa, msa.size, r, &n) != 0) {
		say(LOG_ERR, "MSA_INFO failed");
		return -1;
	}
	usleep(20000);		/* as Linux (an SDM845 firmware's XPU) */
	for (i = 0; i < n; i++) {
		g.addr = r[i].addr;
		g.size = r[i].size;
		g.secure = r[i].secure;
		if (ioctl(dev, WCNIOC_ASSIGN, &g) != 0) {
			if (errno == EBUSY && msa.given != 0) {
				say(LOG_INFO, "MSA region %u: given before", i);
				continue;
			}
			say(LOG_ERR, "MSA region %u: %s", i, strerror(errno));
			return -1;
		}
	}
	say(LOG_INFO, "MSA: %u region(s) given", n);
	if (ask(wlfw_msa_ready(&b, txn), &m, "MSA_READY") != 0)
		return -1;
	(void)result(&m, "MSA_READY", -1);	/* Linux goes on regardless */

	/* CAP: the chip and the board, for the board data's name. */
	if (ask(wlfw_cap(&b, txn), &m, "CAP") != 0)
		return -1;
	e = result(&m, "CAP", 90);
	if (wlfw_cap_resp(&m, &c) == 0) {
		say(LOG_INFO, "chip 0x%x family 0x%x board 0x%x soc 0x%x, "
		    "fw 0x%x %s%s", c.chip_id, c.chip_family, c.board_id,
		    c.soc_id, c.fw_version, c.fw_build_id,
		    e ? " (CAP said error)" : "");
		board = c.board_id;
		chip = c.chip_id;
	}

	/* The board data, then CAL_REPORT; FW_READY_IND follows. */
	if ((e = board_data(board, chip, &f, &bd, &bdl, src, sizeof(src))) !=
	    0) {
		say(LOG_ERR, "no board data for board 0x%x (%d)", board, e);
		return -1;
	}
	say(LOG_INFO, "board data: %s, %zu bytes", src, bdl);
	for (off = 0, seg = 0; off < bdl; off += piece, seg++) {
		piece = bdl - off > WLFW_BDF_SEG ? WLFW_BDF_SEG : bdl - off;
		if (ask(wlfw_bdf(&b, txn, bdl, seg, bd + off, piece,
		    off + piece == bdl), &m, "BDF_DOWNLOAD") != 0) {
			free(f);
			return -1;
		}
		/* The last segment checks the CRC and may say MALFORMED:
		 * the firmware takes the data all the same (ath10k). */
		if ((e = result(&m, "BDF_DOWNLOAD", off + piece == bdl ?
		    QMI_ERR_MALFORMED : -1)) != 0 && off + piece != bdl) {
			free(f);
			return -1;
		}
	}
	free(f);
	if (ask(wlfw_cal_report(&b, txn), &m, "CAL_REPORT") != 0)
		return -1;
	(void)result(&m, "CAL_REPORT", -1);
	for (k = 0; !fw_ready_seen && k < 20; k++) {
		int rr = get(in, sizeof(in), &from, 1000);

		if (rr > 0 && from.sq_port == fw.sq_port &&
		    qmi_parse(in, rr, &m) == 0 && m.type == QMI_INDICATION &&
		    m.id == WLFW_FW_READY_IND)
			fw_ready_seen = 1;
	}
	if (!fw_ready_seen) {
		say(LOG_ERR, "no FW_READY in 20 s");
		return -1;
	}
	say(LOG_INFO, "firmware ready");
	if (stop_at_ready)
		return 0;

cfg:
	/* The supplies, then WLAN_CFG and WLAN_MODE, then the engines. */
	on = 1;
	if (ioctl(dev, WCNIOC_POWER, &on) != 0) {
		say(LOG_ERR, "supplies: %s", strerror(errno));
		return -1;
	}
	if (ask(wlfw_wlan_cfg(&b, txn), &m, "WLAN_CFG") != 0 ||
	    result(&m, "WLAN_CFG", -1) != 0)
		return -1;
	if (ask(wlfw_wlan_mode(&b, txn, WLFW_MODE_MISSION), &m,
	    "WLAN_MODE") != 0 || result(&m, "WLAN_MODE", -1) != 0)
		return -1;
	say(LOG_INFO, "WLAN on (mission mode)");
	if (ioctl(dev, WCNIOC_START) != 0) {
		say(LOG_ERR, "copy engines: %s", strerror(errno));
		return -1;
	}
	say(LOG_INFO, "copy engines up, HTC connected");
	return 0;
}

int
main(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "fvd:b:n")) != -1)
		switch (c) {
		case 'f': fg = 1; break;
		case 'v': verbose = 1; fg = 1; break;
		case 'd': fwdir = optarg; break;
		case 'b': board2 = optarg; break;
		case 'n': stop_at_ready = 1; break;
		default:
			fprintf(stderr, "usage: wcnd [-f] [-v] [-d fwdir] "
			    "[-b board-2.bin] [-n]\n");
			return 2;
		}
	openlog("wcnd", LOG_PID, LOG_DAEMON);
	if ((dev = open(DEV, O_RDWR)) < 0) {
		say(LOG_ERR, "%s: %s", DEV, strerror(errno));
		return 1;
	}
	if (find_wlfw(120) != 0)
		return 1;
	if (bring_up() != 0)
		return 1;
	if (stop_at_ready)
		return 0;
	if (!fg && daemon(0, 0) != 0)
		say(LOG_WARNING, "daemon: %s", strerror(errno));
	/* The network side comes next: scan, association, WPA2. */
	for (;;)
		pause();
}
