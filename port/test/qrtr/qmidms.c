/*
 * qmidms: one QMI request to the modem's DMS service over QRTR - what
 * "qmicli -d qrtr://0 --dms-get-revision" asks (port/clover/hw-modem.md).
 *
 *	qmidms [rev|model|maker] [node]
 *
 * Finds DMS (QMI service 2) with the name server, sends the request (no
 * TLVs: GET_REVISION 0x0023, GET_MODEL 0x0022, GET_MANUFACTURER 0x0021),
 * prints the result TLV (0x02) and the string (0x01).  The QMI header over
 * QRTR is the QMUX-less one: type (0 request, 2 response), transaction (16
 * bits), message id (16), length (16), then TLVs {type (8), length (16),
 * value}.  Nothing that names this tablet (IMEI, serial) is asked.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netqrtr/qrtr.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QMI_DMS		2

static int
get(int s, void *buf, size_t len, struct sockaddr_qrtr *from, int ms)
{
	struct pollfd p = { s, POLLIN, 0 };
	socklen_t fl = sizeof(*from);

	if (poll(&p, 1, ms) != 1)
		return -1;
	return (int)recvfrom(s, buf, len, 0, (struct sockaddr *)from, &fl);
}

int
main(int argc, char **argv)
{
	struct sockaddr_qrtr me, to, from;
	struct qrtr_ctrl_pkt pkt;
	socklen_t len = sizeof(me);
	uint8_t req[7], rsp[1024];
	const char *what = argc > 1 ? argv[1] : "rev";
	unsigned want_node = argc > 2 ? (unsigned)atoi(argv[2]) : 0;
	uint16_t msg, txn = 1, tl, r16, e16;
	uint32_t node = ~0u, port = 0;
	int s, n, i;

	msg = strcmp(what, "model") == 0 ? 0x0022 :
	    strcmp(what, "maker") == 0 ? 0x0021 : 0x0023;
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

	/* The name server: DMS's address. */
	to = me;
	to.sq_port = QRTR_PORT_CTRL;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = QMI_DMS;
	(void)sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr *)&to,
	    sizeof(to));
	while (get(s, &pkt, sizeof(pkt), &from, 5000) == sizeof(pkt)) {
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER || pkt.server.port == 0)
			break;
		if (pkt.server.node == want_node) {
			node = pkt.server.node;
			port = pkt.server.port;
		}
	}
	if (node == ~0u) {
		fprintf(stderr, "qmidms: no DMS on node %u\n", want_node);
		return 2;
	}
	printf("DMS at %u:%u\n", node, port);

	req[0] = 0;				/* request */
	req[1] = txn & 0xff;
	req[2] = txn >> 8;
	req[3] = msg & 0xff;
	req[4] = msg >> 8;
	req[5] = req[6] = 0;			/* no TLVs */
	memset(&to, 0, sizeof(to));
	to.sq_len = sizeof(to);
	to.sq_family = AF_QIPCRTR;
	to.sq_node = node;
	to.sq_port = port;
	if (sendto(s, req, sizeof(req), 0, (struct sockaddr *)&to,
	    sizeof(to)) != sizeof(req)) {
		perror("sendto");
		return 1;
	}
	for (;;) {
		if ((n = get(s, rsp, sizeof(rsp), &from, 10000)) < 0) {
			fprintf(stderr, "qmidms: no answer in 10 s\n");
			return 3;
		}
		if (n >= 7 && from.sq_port == port && rsp[0] == 2 &&
		    (rsp[1] | rsp[2] << 8) == txn)
			break;
	}
	tl = rsp[5] | rsp[6] << 8;
	printf("response 0x%04x, %d bytes, TLVs %u bytes\n",
	    rsp[3] | rsp[4] << 8, n, tl);
	for (i = 7; i + 3 <= n && i + 3 <= 7 + tl; ) {
		unsigned t = rsp[i], l = rsp[i + 1] | rsp[i + 2] << 8;

		if (i + 3 + (int)l > n)
			break;
		if (t == 0x02 && l >= 4) {
			r16 = rsp[i + 3] | rsp[i + 4] << 8;
			e16 = rsp[i + 5] | rsp[i + 6] << 8;
			printf("result %u, error %u\n", r16, e16);
		} else if (t == 0x01)
			printf("%s: '%.*s'\n", what, (int)l, (char *)rsp + i + 3);
		i += 3 + l;
	}
	return 0;
}
