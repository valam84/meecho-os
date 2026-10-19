/*
 * qrtrt: the AF_QIPCRTR socket family and its name server, without a
 * modem - two sockets of this side through the qrtr service
 * (minix/net/qrtr, port/clover/hw-modem.md, section 1).
 *
 *   qrtrt		the checks below; "QRTRT n checks, m failed"
 *   qrtrt lookup [S]	list the servers the name server knows (of service
 *			S, or all), as qrtr-lookup does - on the tablet, the
 *			modem's
 *
 * The checks: socket(2) of the family; bind to port 0 gives a port of the
 * ephemeral range and getsockname() names it on node 1; a server
 * announces service 4242:7 with NEW_SERVER to the control port; a lookup
 * sees it and then the empty end-of-list packet, from the control port; a
 * datagram to the server's port arrives with the client's address, the
 * answer comes back; recv with a short buffer truncates; a lookup made
 * before a second server appears is told of it; closing the server makes
 * the lookup hear DEL_SERVER; sending to a port nobody holds fails; a
 * second socket cannot take a port that is bound.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netqrtr/qrtr.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failed;

static void
check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static int
sock(void)
{
	return socket(AF_QIPCRTR, SOCK_DGRAM, 0);
}

static int
name(int s, struct sockaddr_qrtr *sq)
{
	socklen_t len = sizeof(*sq);

	memset(sq, 0, sizeof(*sq));
	return getsockname(s, (struct sockaddr *)sq, &len);
}

static int
ctrl(int s, uint32_t cmd, uint32_t service, uint32_t instance)
{
	struct sockaddr_qrtr me, to;
	struct qrtr_ctrl_pkt pkt;

	if (name(s, &me) != 0)
		return -1;
	memset(&to, 0, sizeof(to));
	to.sq_len = sizeof(to);
	to.sq_family = AF_QIPCRTR;
	to.sq_node = me.sq_node;
	to.sq_port = QRTR_PORT_CTRL;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cmd;
	pkt.server.service = service;
	pkt.server.instance = instance;
	return sendto(s, &pkt, sizeof(pkt), 0, (struct sockaddr *)&to,
	    sizeof(to)) == sizeof(pkt) ? 0 : -1;
}

/* One packet, within ms milliseconds; its length, or -1. */
static ssize_t
get(int s, void *buf, size_t len, struct sockaddr_qrtr *from, int ms)
{
	struct pollfd p = { s, POLLIN, 0 };
	socklen_t fl = sizeof(*from);

	if (poll(&p, 1, ms) != 1)
		return -1;
	return recvfrom(s, buf, len, 0, (struct sockaddr *)from, &fl);
}

static int
lookup(int argc, char **argv)
{
	struct sockaddr_qrtr from;
	struct qrtr_ctrl_pkt pkt;
	unsigned service = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 0;
	int s, n = 0;

	if ((s = sock()) < 0 || bind(s, (struct sockaddr *)&(struct
	    sockaddr_qrtr){ sizeof(struct sockaddr_qrtr), AF_QIPCRTR, 1, 0 },
	    sizeof(struct sockaddr_qrtr)) != 0 ||
	    ctrl(s, QRTR_TYPE_NEW_LOOKUP, service, 0) != 0) {
		perror("qrtrt lookup");
		return 1;
	}
	printf("  Service Version Instance Node  Port\n");
	while (get(s, &pkt, sizeof(pkt), &from, 5000) == sizeof(pkt)) {
		if (pkt.cmd != QRTR_TYPE_NEW_SERVER)
			continue;
		if (pkt.server.service == 0 && pkt.server.node == 0 &&
		    pkt.server.port == 0)
			break;
		printf("%9u %7u %8u %4u %5u\n", pkt.server.service,
		    pkt.server.instance & 0xff, pkt.server.instance >> 8,
		    pkt.server.node, pkt.server.port);
		n++;
	}
	printf("%d servers\n", n);
	return 0;
}

int
main(int argc, char **argv)
{
	struct sockaddr_qrtr a, srv, cli, from, to;
	struct qrtr_ctrl_pkt pkt;
	char buf[256];
	int s, c, l, s2, x;
	ssize_t n;

	if (argc > 1 && strcmp(argv[1], "lookup") == 0)
		return lookup(argc, argv);

	s = sock();
	check(s >= 0, "socket(AF_QIPCRTR, SOCK_DGRAM)");
	if (s < 0) {
		perror("socket");
		return 1;
	}
	memset(&a, 0, sizeof(a));
	a.sq_len = sizeof(a);
	a.sq_family = AF_QIPCRTR;
	a.sq_node = 1;
	check(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0 &&
	    name(s, &srv) == 0 && srv.sq_node == 1 && srv.sq_port >= 0x4000 &&
	    srv.sq_port <= 0x7fff, "bind to port 0: an ephemeral port on node 1");
	printf("     server at %u:%u\n", srv.sq_node, srv.sq_port);
	check(ctrl(s, QRTR_TYPE_NEW_SERVER, 4242, 7) == 0,
	    "NEW_SERVER 4242:7 to the control port");

	l = sock();
	check(l >= 0 && bind(l, (struct sockaddr *)&a, sizeof(a)) == 0 &&
	    ctrl(l, QRTR_TYPE_NEW_LOOKUP, 4242, 0) == 0, "NEW_LOOKUP 4242");
	n = get(l, &pkt, sizeof(pkt), &from, 2000);
	check(n == sizeof(pkt) && pkt.cmd == QRTR_TYPE_NEW_SERVER &&
	    pkt.server.service == 4242 && pkt.server.instance == 7 &&
	    pkt.server.node == 1 && pkt.server.port == srv.sq_port &&
	    from.sq_port == QRTR_PORT_CTRL && from.sq_node == 1,
	    "the lookup names the server, from the control port");
	n = get(l, &pkt, sizeof(pkt), &from, 2000);
	check(n == sizeof(pkt) && pkt.cmd == QRTR_TYPE_NEW_SERVER &&
	    pkt.server.service == 0 && pkt.server.port == 0,
	    "then the empty end of the list");

	c = sock();
	to = srv;
	to.sq_len = sizeof(to);
	check(c >= 0 && sendto(c, "ping", 4, 0, (struct sockaddr *)&to,
	    sizeof(to)) == 4, "a datagram to the server's port");
	n = get(s, buf, sizeof(buf), &from, 2000);
	check(n == 4 && memcmp(buf, "ping", 4) == 0 && name(c, &cli) == 0 &&
	    from.sq_node == 1 && from.sq_port == cli.sq_port,
	    "it arrives, with the client's address");
	check(sendto(s, "pong!", 5, 0, (struct sockaddr *)&from,
	    sizeof(from)) == 5, "the answer");
	n = get(c, buf, 3, &from, 2000);
	check(n == 3 && memcmp(buf, "pon", 3) == 0 &&
	    from.sq_port == srv.sq_port, "a short buffer: truncated");

	s2 = sock();
	check(s2 >= 0 && bind(s2, (struct sockaddr *)&a, sizeof(a)) == 0 &&
	    ctrl(s2, QRTR_TYPE_NEW_SERVER, 4242, 8) == 0,
	    "a second server 4242:8");
	n = get(l, &pkt, sizeof(pkt), &from, 2000);
	check(n == sizeof(pkt) && pkt.cmd == QRTR_TYPE_NEW_SERVER &&
	    pkt.server.instance == 8, "the lookup is told of it");
	close(s);
	n = get(l, &pkt, sizeof(pkt), &from, 2000);
	check(n == sizeof(pkt) && pkt.cmd == QRTR_TYPE_DEL_SERVER &&
	    pkt.server.instance == 7, "closing the first: DEL_SERVER");

	to.sq_port = 0x7ff0;
	errno = 0;
	check(sendto(c, "x", 1, 0, (struct sockaddr *)&to, sizeof(to)) < 0,
	    "to a port nobody holds: refused");
	x = sock();
	name(s2, &a);
	a.sq_len = sizeof(a);
	check(x >= 0 && bind(x, (struct sockaddr *)&a, sizeof(a)) < 0 &&
	    errno == EADDRINUSE, "a bound port: EADDRINUSE");

	printf("QRTRT %d checks, %d failed\n", checks, failed);
	return failed != 0;
}
