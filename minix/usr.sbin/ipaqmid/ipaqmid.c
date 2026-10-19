/*
 * ipaqmid: the IPA's handshake with the modem, over QMI on QRTR, for the
 * IPA service (minix/drivers/net/qcom_ipa) - the service owns the hardware,
 * this daemon the sockets (<minix/qcom_ipa.h>).  port/clover/hw-modem.md,
 * section 9.
 *
 * The protocol is the one Linux's IPA drivers speak (ipa2-lite's
 * ipa-qmi.c, GPL - the message numbers, their fields and the order):
 *
 *   - we serve QMI service 0x31 version 1 instance 1, and look up the
 *     modem's 0x31 version 1 instance 2;
 *   - when the modem's appears, we send it INIT_DRIVER (0x21): where in
 *     the IPA's SRAM its headers, tables and own memory are, the endpoint
 *     for control commands (5), whether its microcontroller is already
 *     loaded;
 *   - the modem sends our service INDICATION_REGISTER (0x20); we answer;
 *   - once the modem has accepted INIT_DRIVER and the microcontroller has
 *     said it is up (the service sees that), we send the modem's service
 *     the INIT_COMPLETE indication (0x22) and tell the IPA service that
 *     the data path may start.
 *
 * The modem going away (DEL_SERVER of its 0x31) starts it all again.
 *
 *	ipaqmid [-v]
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netqrtr/qrtr.h>
#include <minix/qcom_ipa.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define IPA_QMI_SVC		0x31
#define HOST_INSTANCE		(1 | 1 << 8)	/* version 1, instance 1 */
#define MODEM_INSTANCE		(1 | 2 << 8)	/* version 1, instance 2 */

#define QMI_REQUEST		0
#define QMI_RESPONSE		2
#define QMI_INDICATION		4

#define MSG_INDICATION_REGISTER	0x20
#define MSG_INIT_DRIVER		0x21
#define MSG_INIT_COMPLETE	0x22

#define PLATFORM_MSM_ANDROID	3
#define CTRL_DEST_ENDPOINT	5

static int verbose;
static int dev = -1;

static void
say(const char *fmt, ...)
{
	va_list ap;
	time_t t = time(NULL);
	char ts[16];

	strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
	printf("%s ipaqmid: ", ts);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

/*===========================================================================*
 *				QMI messages				     *
 *===========================================================================*/
struct msg {
	uint8_t b[256];
	size_t n;
};

static void
msg_start(struct msg *m, int type, uint16_t txn, uint16_t id)
{
	m->b[0] = (uint8_t)type;
	m->b[1] = txn & 0xff;
	m->b[2] = txn >> 8;
	m->b[3] = id & 0xff;
	m->b[4] = id >> 8;
	m->n = 7;
}

static void
put_tlv(struct msg *m, uint8_t type, const void *v, uint16_t len)
{
	m->b[m->n++] = type;
	m->b[m->n++] = len & 0xff;
	m->b[m->n++] = len >> 8;
	memcpy(m->b + m->n, v, len);
	m->n += len;
	m->b[5] = (uint8_t)((m->n - 7) & 0xff);
	m->b[6] = (uint8_t)((m->n - 7) >> 8);
}

static void
put_u32(struct msg *m, uint8_t type, uint32_t v)
{
	uint8_t le[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff,
	    v >> 24 };

	put_tlv(m, type, le, 4);
}

static void
put_pair(struct msg *m, uint8_t type, uint32_t a, uint32_t b)
{
	uint8_t le[8];
	int i;

	for (i = 0; i < 4; i++) {
		le[i] = (a >> (8 * i)) & 0xff;
		le[4 + i] = (b >> (8 * i)) & 0xff;
	}
	put_tlv(m, type, le, 8);
}

/* The standard result TLV (0x02): result, error. */
static void
put_result(struct msg *m, uint16_t result, uint16_t error)
{
	uint8_t le[4] = { result & 0xff, result >> 8, error & 0xff,
	    error >> 8 };

	put_tlv(m, 0x02, le, 4);
}

/* The result TLV of a message, or -1 if it has none. */
static int
get_result(const uint8_t *p, size_t n, unsigned *error)
{
	size_t i, tl;

	if (n < 7)
		return -1;
	tl = p[5] | p[6] << 8;
	for (i = 7; i + 3 <= n && i + 3 <= 7 + tl; ) {
		unsigned t = p[i], l = p[i + 1] | p[i + 2] << 8;

		if (i + 3 + l > n)
			break;
		if (t == 0x02 && l >= 4) {
			*error = p[i + 5] | p[i + 6] << 8;
			return p[i + 3] | p[i + 4] << 8;
		}
		i += 3 + l;
	}
	return -1;
}

/*===========================================================================*
 *				sockets					     *
 *===========================================================================*/
static int
qsock(struct sockaddr_qrtr *me)
{
	socklen_t len = sizeof(*me);
	int s;

	if ((s = socket(AF_QIPCRTR, SOCK_DGRAM, 0)) < 0)
		err(1, "socket(AF_QIPCRTR)");
	memset(me, 0, sizeof(*me));
	me->sq_len = sizeof(*me);
	me->sq_family = AF_QIPCRTR;
	me->sq_node = 1;			/* this side of the router */
	if (bind(s, (struct sockaddr *)me, sizeof(*me)) != 0 ||
	    getsockname(s, (struct sockaddr *)me, &len) != 0)
		err(1, "bind");
	return s;
}

static int
send_to(int s, uint32_t node, uint32_t port, const void *p, size_t n)
{
	struct sockaddr_qrtr to;

	memset(&to, 0, sizeof(to));
	to.sq_len = sizeof(to);
	to.sq_family = AF_QIPCRTR;
	to.sq_node = node;
	to.sq_port = port;
	if (sendto(s, p, n, 0, (struct sockaddr *)&to, sizeof(to)) !=
	    (ssize_t)n) {
		say("sendto %u:%u: %s\n", node, port, strerror(errno));
		return -1;
	}
	return 0;
}

static void
ctrl(int s, const struct sockaddr_qrtr *me, uint32_t cmd, uint32_t svc,
	uint32_t inst)
{
	struct qrtr_ctrl_pkt pkt;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cmd;
	pkt.server.service = svc;
	pkt.server.instance = inst;
	pkt.server.node = me->sq_node;
	pkt.server.port = me->sq_port;
	(void)send_to(s, me->sq_node, QRTR_PORT_CTRL, &pkt, sizeof(pkt));
}

/*===========================================================================*
 *				the handshake				     *
 *===========================================================================*/
static struct {
	int found;			/* the modem's 0x31 is there */
	uint32_t node, port;
	uint16_t txn;
	int init_sent, modem_ready;
	int ind_requested, ind_sent;
	int done;
} hs;

static int
info(struct ipa_info *in)
{
	if (ioctl(dev, IPAIOC_INFO, in) != 0) {
		say("IPAIOC_INFO: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static void
send_init_driver(int s)
{
	struct ipa_info in;
	struct msg m;
	uint8_t skip;

	if (info(&in) != 0)
		return;
	skip = (in.state & IPA_ST_UC) ? 1 : 0;
	msg_start(&m, QMI_REQUEST, ++hs.txn, MSG_INIT_DRIVER);
	put_u32(&m, 0x10, PLATFORM_MSM_ANDROID);
	put_pair(&m, 0x11, in.hdr_start, in.hdr_end);
	put_pair(&m, 0x12, in.rt4_start, in.rt4_end);
	put_pair(&m, 0x13, in.rt6_start, in.rt6_end);
	put_u32(&m, 0x14, in.ft4_start);
	put_u32(&m, 0x15, in.ft6_start);
	if (in.mdm_size != 0)
		put_pair(&m, 0x16, in.mdm_start, in.mdm_size);
	put_u32(&m, 0x17, CTRL_DEST_ENDPOINT);
	put_tlv(&m, 0x18, &skip, 1);
	if (in.comp_end > in.comp_start)
		put_pair(&m, 0x1a, in.comp_start, in.comp_end);
	say("INIT_DRIVER to %u:%u (txn %u, %zu bytes; hdr %x..%x, rt4 %x/%u, "
	    "rt6 %x/%u, ft4 %x, ft6 %x, mdm %x+%x, comp %x..%x, skip_uc_load "
	    "%u)\n", hs.node, hs.port, hs.txn, m.n, in.hdr_start, in.hdr_end,
	    in.rt4_start, in.rt4_end, in.rt6_start, in.rt6_end, in.ft4_start,
	    in.ft6_start, in.mdm_start, in.mdm_size, in.comp_start,
	    in.comp_end, skip);
	if (send_to(s, hs.node, hs.port, m.b, m.n) == 0)
		hs.init_sent = 1;
}

static void
reset_state(void)
{
	int v = 0;

	if (hs.done)
		(void)ioctl(dev, IPAIOC_MODEM, &v);
	memset(&hs, 0, sizeof(hs));
}

/* Everything there: tell the modem, then the service. */
static void
maybe_ready(int srv)
{
	struct ipa_info in;
	struct msg m;
	int v = 1;

	if (!hs.modem_ready || hs.done || info(&in) != 0 ||
	    !(in.state & IPA_ST_UC))
		return;
	if (hs.ind_requested && !hs.ind_sent) {
		msg_start(&m, QMI_INDICATION, 0, MSG_INIT_COMPLETE);
		put_result(&m, 0, 0);
		if (send_to(srv, hs.node, hs.port, m.b, m.n) == 0) {
			hs.ind_sent = 1;
			say("INIT_COMPLETE indication sent\n");
		}
	}
	/* The first boot needs the indication; Linux waits for it too. */
	if (!hs.ind_sent)
		return;
	if (ioctl(dev, IPAIOC_MODEM, &v) != 0)
		say("IPAIOC_MODEM: %s\n", strerror(errno));
	hs.done = 1;
	say("modem handshake complete, data path enabled\n");
}

static void
client_packet(int cli, const uint8_t *p, size_t n,
	const struct sockaddr_qrtr *from)
{
	const struct qrtr_ctrl_pkt *pkt = (const void *)p;
	unsigned e;
	int r;

	if (from->sq_port == QRTR_PORT_CTRL) {
		if (n < sizeof(*pkt) || pkt->server.service != IPA_QMI_SVC)
			return;
		if (pkt->cmd == QRTR_TYPE_NEW_SERVER && pkt->server.port != 0 &&
		    pkt->server.instance == MODEM_INSTANCE && !hs.found) {
			hs.found = 1;
			hs.node = pkt->server.node;
			hs.port = pkt->server.port;
			say("modem IPA QMI service found (node %u port %u)\n",
			    hs.node, hs.port);
			send_init_driver(cli);
		} else if (pkt->cmd == QRTR_TYPE_DEL_SERVER && hs.found &&
		    pkt->server.node == hs.node &&
		    pkt->server.port == hs.port) {
			say("the modem's IPA service went away\n");
			reset_state();
		}
		return;
	}
	if (!hs.found || from->sq_node != hs.node ||
	    from->sq_port != hs.port || n < 7)
		return;
	if (p[0] == QMI_RESPONSE && (p[3] | p[4] << 8) == MSG_INIT_DRIVER &&
	    (p[1] | p[2] << 8) == hs.txn) {
		r = get_result(p, n, &e);
		if (r == 0) {
			say("modem accepted INIT_DRIVER\n");
			hs.modem_ready = 1;
		} else
			say("modem refused INIT_DRIVER: result %d error %u\n",
			    r, e);
	} else if (verbose)
		say("client: type %u msg %#x, %zu bytes\n", p[0],
		    p[3] | p[4] << 8, n);
}

static void
server_packet(int srv, const uint8_t *p, size_t n,
	const struct sockaddr_qrtr *from)
{
	struct msg m;

	if (from->sq_port == QRTR_PORT_CTRL || n < 7)
		return;
	if (p[0] == QMI_REQUEST &&
	    (p[3] | p[4] << 8) == MSG_INDICATION_REGISTER) {
		msg_start(&m, QMI_RESPONSE, (uint16_t)(p[1] | p[2] << 8),
		    MSG_INDICATION_REGISTER);
		put_result(&m, 0, 0);
		if (send_to(srv, from->sq_node, from->sq_port, m.b, m.n) == 0) {
			hs.ind_requested = 1;
			say("INDICATION_REGISTER from %u:%u answered\n",
			    from->sq_node, from->sq_port);
		}
	} else if (verbose)
		say("server: type %u msg %#x from %u:%u, %zu bytes\n", p[0],
		    p[3] | p[4] << 8, from->sq_node, from->sq_port, n);
}

int
main(int argc, char **argv)
{
	struct sockaddr_qrtr cme, sme, from;
	struct pollfd pf[2];
	struct ipa_info in;
	socklen_t fl;
	uint8_t buf[1024];
	ssize_t n;
	int cli, srv, i, c;

	while ((c = getopt(argc, argv, "v")) != -1)
		if (c == 'v')
			verbose = 1;
		else {
			fprintf(stderr, "usage: ipaqmid [-v]\n");
			return 2;
		}

	if ((dev = open("/dev/ipa", O_RDWR)) < 0)
		err(1, "/dev/ipa");
	/* The service lays the SRAM out first; INIT_DRIVER says where. */
	for (i = 0; info(&in) == 0 && !(in.state & IPA_ST_SRAM); i++) {
		if (i == 0)
			say("waiting for the IPA service's SRAM\n");
		sleep(1);
	}

	srv = qsock(&sme);
	cli = qsock(&cme);
	ctrl(srv, &sme, QRTR_TYPE_NEW_SERVER, IPA_QMI_SVC, HOST_INSTANCE);
	ctrl(cli, &cme, QRTR_TYPE_NEW_LOOKUP, IPA_QMI_SVC, MODEM_INSTANCE);
	say("serving 0x31 at 1:%u, looking for the modem's\n", sme.sq_port);

	pf[0].fd = cli;
	pf[1].fd = srv;
	pf[0].events = pf[1].events = POLLIN;
	for (;;) {
		if (poll(pf, 2, 1000) < 0 && errno != EINTR)
			err(1, "poll");
		for (i = 0; i < 2; i++) {
			if (!(pf[i].revents & POLLIN))
				continue;
			fl = sizeof(from);
			n = recvfrom(pf[i].fd, buf, sizeof(buf), 0,
			    (struct sockaddr *)&from, &fl);
			if (n <= 0)
				continue;
			if (i == 0)
				client_packet(cli, buf, (size_t)n, &from);
			else
				server_packet(srv, buf, (size_t)n, &from);
		}
		maybe_ready(srv);
	}
}
