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
 *            compared with what the firmware told Linux on this tablet.
 * -x prints every datagram in hex.
 *
 * Nothing that hands the firmware memory: no MSA_INFO, no MSA_READY, no
 * board data.  Those come with TrustZone's hyp-assign, which is the wcn
 * service's.  Closing the socket tells the modem the client is gone.
 *
 * Build: the cross compiler against DESTDIR with wcn_wlfw.c beside it
 * (port/test/wcn/probe-build.sh).
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netqrtr/qrtr.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wcn_wlfw.h"

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
		else
			printf("%s: unknown\n", argv[i]);
	}
	close(s);
	return 0;
}
