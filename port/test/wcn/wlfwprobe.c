/*
 * wlfwprobe: the first words with the WLAN firmware in the Mi Pad 4's
 * modem, over QRTR - questions only (port/clover/hw-wifi.md, section 3).
 *
 *	wlfwprobe [-x] [cap|reg|hostcap ...]
 *
 * Finds wlfw (QMI service 0x45) with the name server, then sends, in the
 * order given (default: reg hostcap cap):
 *   reg      IND_REGISTER (fw_ready, msa_ready, client "LENK") - prints
 *            fw_status;
 *   hostcap  HOST_CAP, the 8-bit form of SDM630/660;
 *   cap      CAP - chip, board, soc, firmware version and build id,
 *            compared with what the firmware told Linux on this tablet;
 *   msa      MSA_INFO with the MSA the wcn service holds, each region of
 *            the answer given away by the service (WCNIOC_ASSIGN on
 *            /dev/wlan0 - TrustZone), then MSA_READY: what Linux does
 *            between HOST_CAP and CAP.  Memory leaves this side here;
 *   bdf WHERE the board data for the board the firmware is (chip
 *            0x30214, board 0xff - CAP), in segments, then CAL_REPORT,
 *            then up to 20 s for FW_READY_IND.  WHERE: a directory - the
 *            modem's firmware, its bdwlan.<board> (wcn_bdf_name()); a
 *            board-2.bin; a raw board file; or "-": the modem's
 *            firmware directory of the kit first (BDF_DIR), board-2.bin
 *            (BOARD2) after - the order the driver will look in.
 * -x prints every datagram in hex.
 *
 * Without "msa" nothing hands the firmware memory; with it, only through
 * the wcn service, which holds the right to.  No board data yet.  Closing
 * the socket tells the modem the client is gone.
 *
 * Build: the cross compiler against DESTDIR with wcn_wlfw.c beside it
 * (port/test/wcn/probe-build.sh).
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netqrtr/qrtr.h>

#include <sys/ioctl.h>

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wcn_wlfw.h"
#include "wcn_ioc.h"
#include "wcn_fw.h"

#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

static int hex;
static int s;
static struct sockaddr_qrtr fw;
static uint16_t txn = 1;
static uint8_t out[WLFW_MSG_MAX], in[WLFW_MSG_MAX];

static void
dump(const char *dir, const uint8_t *p, size_t n)
{
	size_t i;

	if (!hex)
		return;
	printf("  %s %zu:", dir, n);
	for (i = 0; i < n; i++)
		printf("%s%02x", i % 32 == 0 && i ? "\n     " : " ", p[i]);
	printf("\n");
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

/* Send what "out" holds; wait for the answer of the same transaction. */
static int
ask(size_t n, struct qmi_msg *m, const char *what)
{
	struct sockaddr_qrtr from;
	int r;

	if (n == 0) {
		printf("%s: could not build\n", what);
		return -1;
	}
	dump(">", out, n);
	if (sendto(s, out, n, 0, (struct sockaddr *)&fw, sizeof(fw)) !=
	    (ssize_t)n) {
		perror("sendto");
		return -1;
	}
	for (;;) {
		if ((r = get(in, sizeof(in), &from, 10000)) < 0) {
			printf("%s: no answer in 10 s\n", what);
			return -1;
		}
		dump("<", in, r);
		if (from.sq_node != fw.sq_node || from.sq_port != fw.sq_port)
			continue;
		if (qmi_parse(in, r, m) != 0) {
			printf("%s: malformed datagram (%d bytes)\n", what, r);
			continue;
		}
		if (m->type == QMI_INDICATION) {
			printf("  (indication 0x%04x, %zu bytes)\n", m->id,
			    m->len);
			continue;
		}
		if (m->type == QMI_RESPONSE && m->txn == txn)
			break;
	}
	txn++;
	return 0;
}

static int
result(struct qmi_msg *m, const char *what)
{
	unsigned res, err;

	if (qmi_result(m, &res, &err) != 0) {
		printf("%s: no result TLV\n", what);
		return -1;
	}
	printf("%s: result %u error %u%s\n", what, res, err,
	    res == 0 ? "" : err == QMI_ERR_NOT_SUPPORTED ?
	    " (not supported)" : " (refused)");
	return res == 0 ? 0 : (int)err;
}

static void
do_reg(void)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct qmi_msg m;
	uint64_t st;

	if (ask(wlfw_ind_register(&b, txn), &m, "IND_REGISTER") != 0 ||
	    result(&m, "IND_REGISTER") != 0)
		return;
	if (wlfw_ind_register_resp(&m, &st) != 0)
		printf("IND_REGISTER: malformed fw_status\n");
	else
		printf("IND_REGISTER: fw_status 0x%llx%s\n",
		    (unsigned long long)st, st & WLFW_FW_STATUS_READY ?
		    " (FW_READY: firmware already up)" : "");
}

static void
do_hostcap(void)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct qmi_msg m;

	if (ask(wlfw_host_cap(&b, txn), &m, "HOST_CAP") == 0)
		(void)result(&m, "HOST_CAP");
}

static void
do_cap(void)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct qmi_msg m;
	struct wlfw_cap c;
	int same;

	if (ask(wlfw_cap(&b, txn), &m, "CAP") != 0 || result(&m, "CAP") != 0)
		return;
	if (wlfw_cap_resp(&m, &c) != 0) {
		printf("CAP: malformed answer\n");
		return;
	}
	printf("CAP: chip_id 0x%x chip_family 0x%x board_id 0x%x "
	    "soc_id 0x%x\n", c.chip_id, c.chip_family, c.board_id, c.soc_id);
	printf("CAP: fw_version 0x%x fw_build_timestamp %s\n", c.fw_version,
	    c.fw_timestamp);
	printf("CAP: fw_build_id %s\n", c.fw_build_id);
	/* pmos-20261019/pass1/wifi/dmesg.txt */
	same = c.chip_id == 0x30214 && c.chip_family == 0x4001 &&
	    c.board_id == 0xff && c.soc_id == 0x40050000 &&
	    c.fw_version == 0x101f81f0 &&
	    strcmp(c.fw_timestamp, "2018-08-31 21:42") == 0 &&
	    strcmp(c.fw_build_id, "QC_IMAGE_VERSION_STRING="
	    "WLAN.HL.1.0.1.c2-00496-QCAHLSWMTPLZ-1") == 0;
	printf("CAP: %s the reference (postmarketOS, 2026-10-19)\n",
	    same ? "SAME as" : "DIFFERS from");
}

static void
do_msa(void)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct qmi_msg m;
	struct wlfw_region r[WLFW_MSA_REGIONS];
	struct wcn_msa msa;
	struct wcn_region g;
	unsigned n, i;
	int fd;

	if ((fd = open("/dev/wlan0", O_RDWR)) < 0) {
		perror("/dev/wlan0");
		return;
	}
	if (ioctl(fd, WCNIOC_MSA, &msa) != 0) {
		perror("WCNIOC_MSA");
		close(fd);
		return;
	}
	printf("MSA %llx+%x, %u region(s) out\n", (unsigned long long)msa.pa,
	    msa.size, msa.given);
	if (ask(wlfw_msa_info(&b, txn, msa.pa, msa.size), &m, "MSA_INFO") != 0 ||
	    result(&m, "MSA_INFO") != 0) {
		close(fd);
		return;
	}
	if (wlfw_msa_info_resp(&m, msa.pa, msa.size, r, &n) != 0) {
		printf("MSA_INFO: regions malformed or outside the MSA\n");
		close(fd);
		return;
	}
	for (i = 0; i < n; i++)
		printf("MSA_INFO: region %u %llx+%x secure %u\n", i,
		    (unsigned long long)r[i].addr, r[i].size, r[i].secure);
	/* Linux waits 20 ms here (an SDM845 firmware's XPU update). */
	usleep(20000);
	for (i = 0; i < n; i++) {
		g.addr = r[i].addr;
		g.size = r[i].size;
		g.secure = r[i].secure;
		if (ioctl(fd, WCNIOC_ASSIGN, &g) != 0) {
			perror("WCNIOC_ASSIGN");
			close(fd);
			return;
		}
		printf("region %u given\n", i);
	}
	close(fd);
	b.len = 0;
	if (ask(wlfw_msa_ready(&b, txn), &m, "MSA_READY") == 0)
		(void)result(&m, "MSA_READY");
}

/* Where the kit has the files: the modem's image, copied from the tablet's
 * own partition at the first boot; board-2.bin only if someone put it. */
#define BDF_DIR	"/libdata/firmware/qcom/clover/modem"
#define BOARD2	"/libdata/firmware/ath10k/WCN3990/hw1.0/board-2.bin"

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
	f = malloc(n > 0 ? n : 1);
	if (f == NULL || n <= 0 || fread(f, 1, n, fp) != (size_t)n) {
		free(f);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	*len = n;
	return f;
}

/*
 * The board data from "path": a directory - its bdwlan.<board>; a
 * board-2.bin - the board by name, most specific first; anything else -
 * the file is the board data.  "name" says which.
 */
static int
load_bdf(const char *path, uint8_t **f, const uint8_t **bd, size_t *bdl,
	char *name)
{
	char p[PATH_MAX], bn[32];
	struct stat st;
	size_t n;
	int which, e = ENOENT;

	free(*f);
	*f = NULL;
	if (stat(path, &st) != 0)
		return ENOENT;
	if (S_ISDIR(st.st_mode)) {
		if (wcn_bdf_name(bn, sizeof(bn), 0xff) < 0)
			return EINVAL;
		snprintf(p, sizeof(p), "%s/%s", path, bn);
		if ((*f = slurp(p, &n)) == NULL)
			return ENOENT;
		*bd = *f;
		*bdl = n;
		snprintf(name, WCN_BD_NAME_MAX, "%.90s", p);
		return 0;
	}
	if ((*f = slurp(path, &n)) == NULL)
		return ENOENT;
	if (n >= 16 && memcmp(*f, "QCA-ATH10K-BOARD", 16) == 0) {
		for (which = 0; which < 3 && e == ENOENT; which++) {
			if (wcn_bd_name(name, WCN_BD_NAME_MAX, which, 0xff,
			    0x30214, NULL) < 0)
				continue;
			e = wcn_bd_find(*f, n, name, bd, bdl);
		}
		return e;
	}
	*bd = *f;
	*bdl = n;
	snprintf(name, WCN_BD_NAME_MAX, "%.90s", path);
	return 0;
}

static void
do_bdf(const char *file)
{
	struct qmi_buf b = { out, sizeof(out), 0, 0 };
	struct sockaddr_qrtr from;
	struct qmi_msg m;
	char name[WCN_BD_NAME_MAX];
	const uint8_t *bd;
	uint8_t *f = NULL;
	size_t bdl, off, piece;
	uint32_t seg;
	unsigned res, err;
	int e, r;

	if (strcmp(file, "-") == 0) {
		f = NULL;
		if ((e = load_bdf(BDF_DIR, &f, &bd, &bdl, name)) != 0)
			e = load_bdf(BOARD2, &f, &bd, &bdl, name);
	} else
		e = load_bdf(file, &f, &bd, &bdl, name);
	if (e != 0) {
		printf("BDF: no board data (%d)\n", e);
		free(f);
		return;
	}
	printf("BDF: board '%s', %zu bytes\n", name, bdl);
	for (off = 0, seg = 0; off < bdl; off += piece, seg++) {
		piece = bdl - off > WLFW_BDF_SEG ? WLFW_BDF_SEG : bdl - off;
		if (ask(wlfw_bdf(&b, txn, bdl, seg, bd + off, piece,
		    off + piece == bdl), &m, "BDF_DOWNLOAD") != 0)
			break;
		if (qmi_result(&m, &res, &err) != 0) {
			printf("BDF %u: no result\n", seg);
			break;
		}
		printf("BDF %u (%zu bytes%s): result %u error %u\n", seg,
		    piece, off + piece == bdl ? ", end" : "", res, err);
		if (res != 0 && off + piece != bdl)
			break;
	}
	free(f);
	if (ask(wlfw_cal_report(&b, txn), &m, "CAL_REPORT") == 0)
		(void)result(&m, "CAL_REPORT");
	/* FW_READY_IND: the firmware is up and waits for WLAN_CFG. */
	for (;;) {
		if ((r = get(in, sizeof(in), &from, 20000)) < 0) {
			printf("FW_READY_IND: none in 20 s\n");
			return;
		}
		dump("<", in, r);
		if (qmi_parse(in, r, &m) == 0 && m.type == QMI_INDICATION) {
			printf("indication 0x%04x%s\n", m.id,
			    m.id == WLFW_FW_READY_IND ? " - FW_READY" : "");
			if (m.id == WLFW_FW_READY_IND)
				return;
		}
	}
}

int
main(int argc, char **argv)
{
	struct sockaddr_qrtr me, to, from;
	struct qrtr_ctrl_pkt pkt;
	socklen_t len = sizeof(me);
	int i, c, found = 0;

	while ((c = getopt(argc, argv, "x")) != -1)
		if (c == 'x')
			hex = 1;
		else
			return 2;
	argc -= optind;
	argv += optind;

	if ((s = socket(AF_QIPCRTR, SOCK_DGRAM, 0)) < 0) {
		perror("socket(AF_QIPCRTR)");
		return 1;
	}
	memset(&me, 0, sizeof(me));
	me.sq_len = sizeof(me);
	me.sq_family = AF_QIPCRTR;
	me.sq_node = 1;
	if (bind(s, (struct sockaddr *)&me, sizeof(me)) != 0 ||
	    getsockname(s, (struct sockaddr *)&me, &len) != 0) {
		perror("bind");
		return 1;
	}

	/* The name server: wlfw's address (any instance). */
	to = me;
	to.sq_port = QRTR_PORT_CTRL;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = WLFW_SERVICE;
	(void)sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr *)&to,
	    sizeof(to));
	while (get(&pkt, sizeof(pkt), &from, 5000) == sizeof(pkt)) {
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER || pkt.server.port == 0)
			break;
		printf("wlfw: node %u port %u version %u instance %u\n",
		    pkt.server.node, pkt.server.port,
		    pkt.server.instance & 0xff, pkt.server.instance >> 8);
		if (!found) {
			memset(&fw, 0, sizeof(fw));
			fw.sq_len = sizeof(fw);
			fw.sq_family = AF_QIPCRTR;
			fw.sq_node = pkt.server.node;
			fw.sq_port = pkt.server.port;
			found = 1;
		}
	}
	if (!found) {
		printf("wlfw: not found (is the modem up?)\n");
		return 2;
	}

	if (argc == 0) {
		do_reg();
		do_hostcap();
		do_cap();
	}
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "reg") == 0)
			do_reg();
		else if (strcmp(argv[i], "hostcap") == 0)
			do_hostcap();
		else if (strcmp(argv[i], "cap") == 0)
			do_cap();
		else if (strcmp(argv[i], "msa") == 0)
			do_msa();
		else if (strcmp(argv[i], "bdf") == 0 && i + 1 < argc)
			do_bdf(argv[++i]);
		else
			printf("%s: unknown\n", argv[i]);
	}
	close(s);
	return 0;
}
