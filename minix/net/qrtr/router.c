/*
 * The router and its name server (Linux's net/qrtr/af_qrtr.c and ns.c -
 * what goes where and why, not their code; port/clover/hw-modem.md,
 * section 5).
 *
 * Nodes: this side is QRTR_LOCAL_NODE; every other node is behind the one
 * link there is, GLINK's IPCRTR channel to the modem, which may bridge
 * others.  A packet for another node goes there with a version 1 header:
 *
 *	version (1), type, src node, src port, confirm_rx, size,
 *	dst node, dst port			(eight 32-bit words)
 *
 * and its data padded to four bytes.  The modem may also send version 2
 * headers (the 16-bit form); both are read.
 *
 * Ports: 1..0x3fff by bind() only, 0x4000..0x7fff given out; the control
 * port (QRTR_PORT_CTRL) is the name server's, which lives here and not in
 * a socket: a packet for {this node, QRTR_PORT_CTRL} - from a socket or
 * from the modem - goes to ns_rx(), and what the name server says goes
 * out as from that port.
 *
 * Flow control as Linux's: the fifth data packet in a row to a remote
 * port asks for confirm_rx, the RESUME_TX that answers it clears the
 * count; a packet that arrives with confirm_rx is answered by RESUME_TX
 * once its socket has read it.  The tenth is sent anyway, not held: a
 * sendto() with an address may not be suspended (libsockevent).
 */

#include "qrtr.h"

#include <stdlib.h>
#include <string.h>

#define PROTO_VER_1		1
#define PROTO_VER_2		3
#define HDR_V1			32
#define HDR_V2			16
#define PORT_CTRL_LEGACY	0xffff
#define EPH_MIN			0x4000
#define EPH_MAX			0x7fff
#define FLOW_LOW		5

#define MAX_SERVERS		128
#define MAX_LOOKUPS		64
#define MAX_FLOWS		64

static struct server {
	int used;
	uint32_t service, instance, node, port;
} servers[MAX_SERVERS];

static struct lookup {
	int used;
	uint32_t service, instance, port;	/* a local socket's port */
} lookups[MAX_LOOKUPS];

static struct flow {
	int used;
	uint32_t node, port, pending;
} flows[MAX_FLOWS];

static uint32_t next_eph = EPH_MIN;
static int link_up;
static uint32_t remote_nid = 0;		/* the node behind the link */

static void ns_rx(uint32_t from_node, uint32_t from_port, const void *data,
	size_t len);

/*===========================================================================*
 *				out					     *
 *===========================================================================*/
static struct flow *
flow_get(uint32_t node, uint32_t port, int make)
{
	struct flow *f = NULL;
	unsigned i;

	for (i = 0; i < MAX_FLOWS; i++) {
		if (flows[i].used && flows[i].node == node &&
		    flows[i].port == port)
			return &flows[i];
		if (!flows[i].used && f == NULL)
			f = &flows[i];
	}
	if (!make || f == NULL)
		return NULL;
	memset(f, 0, sizeof(*f));
	f->used = 1;
	f->node = node;
	f->port = port;
	return f;
}

/* To the modem's side: the header Linux sends, version 1. */
static int
remote_out(uint32_t type, uint32_t src_node, uint32_t src_port,
	uint32_t dst_node, uint32_t dst_port, const void *data, size_t len)
{
	uint32_t hdr[8];
	struct flow *f;
	uint8_t pad[4] = { 0, 0, 0, 0 };
	int confirm = 0, r;
	size_t plen = (len + 3) & ~(size_t)3;
	uint8_t *buf;

	if (!link_up)
		return ECONNRESET;
	if (type == QRTR_TYPE_DATA && (f = flow_get(dst_node, dst_port, 1)) !=
	    NULL) {
		f->pending++;
		confirm = f->pending == FLOW_LOW;
	}
	/* For a control port, the node the link reaches (qrtr_node_enqueue()). */
	if (dst_port == QRTR_PORT_CTRL)
		dst_node = remote_nid;
	hdr[0] = PROTO_VER_1;
	hdr[1] = type;
	hdr[2] = src_node;
	hdr[3] = src_port;
	hdr[4] = confirm;
	hdr[5] = (uint32_t)len;
	hdr[6] = dst_node;
	hdr[7] = dst_port;
	if (plen == len)
		return gl_send(hdr, sizeof(hdr), data, len);
	if ((buf = malloc(plen)) == NULL)
		return ENOMEM;
	memcpy(buf, data, len);
	memcpy(buf + len, pad, plen - len);
	r = gl_send(hdr, sizeof(hdr), buf, plen);
	free(buf);
	return r;
}

/* To a socket of this node; the name server's port to ns_rx(). */
static int
local_out(uint32_t src_node, uint32_t src_port, uint32_t dst_port,
	const void *data, size_t len, int confirm_rx, struct qsock *self)
{
	struct qsock *qs;

	if (dst_port == QRTR_PORT_CTRL) {
		ns_rx(src_node, src_port, data, len);
		return OK;
	}
	if ((qs = qs_by_port(dst_port)) == NULL || qs == self)
		return ENODEV;
	qs_deliver(qs, src_node, src_port, data, len, confirm_rx);
	return OK;
}

/* A control packet from the name server (its port) to an address. */
static void
ns_send(uint32_t node, uint32_t port, const struct qrtr_ctrl_pkt *pkt)
{
	if (node == QRTR_NODE_BCAST) {
		(void)remote_out(pkt->cmd, QRTR_LOCAL_NODE, QRTR_PORT_CTRL,
		    QRTR_NODE_BCAST, QRTR_PORT_CTRL, pkt, sizeof(*pkt));
		return;
	}
	if (node == QRTR_LOCAL_NODE) {
		if (port != QRTR_PORT_CTRL)
			(void)local_out(QRTR_LOCAL_NODE, QRTR_PORT_CTRL, port,
			    pkt, sizeof(*pkt), 0, NULL);
		return;
	}
	(void)remote_out(pkt->cmd, QRTR_LOCAL_NODE, QRTR_PORT_CTRL, node,
	    port, pkt, sizeof(*pkt));
}

/*===========================================================================*
 *				rt_send					     *
 *===========================================================================*/
int
rt_send(struct qsock *qs, uint32_t dst_node, uint32_t dst_port,
	const void *data, size_t len)
{
	uint32_t type = QRTR_TYPE_DATA;
	int r;

	if (len > QRTR_MAX_PACKET)
		return EMSGSIZE;
	if (qs->port == 0 && (r = rt_bind(qs, 0)) != OK)
		return r;
	if (dst_node == QRTR_NODE_BCAST) {
		if (dst_port != QRTR_PORT_CTRL)
			return ENOTCONN;
		/* Linux's qrtr_bcast_enqueue(): every node, and ours. */
		(void)remote_out(type, QRTR_LOCAL_NODE, qs->port,
		    QRTR_NODE_BCAST, QRTR_PORT_CTRL, data, len);
		return local_out(QRTR_LOCAL_NODE, qs->port, QRTR_PORT_CTRL,
		    data, len, 0, qs);
	}
	if (dst_node == QRTR_LOCAL_NODE)
		return local_out(QRTR_LOCAL_NODE, qs->port, dst_port, data,
		    len, 0, qs);
	return remote_out(type, QRTR_LOCAL_NODE, qs->port, dst_node, dst_port,
	    data, len);
}

/* A socket read a packet that asked for it. */
void
rt_resume_tx(uint32_t local_port, uint32_t remote_node, uint32_t remote_port)
{
	struct qrtr_ctrl_pkt pkt;

	if (remote_node == QRTR_LOCAL_NODE)
		return;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_RESUME_TX;
	pkt.client.node = QRTR_LOCAL_NODE;
	pkt.client.port = local_port;
	(void)remote_out(QRTR_TYPE_RESUME_TX, QRTR_LOCAL_NODE, local_port,
	    remote_node, remote_port, &pkt, sizeof(pkt));
}

/*===========================================================================*
 *				ports					     *
 *===========================================================================*/
int
rt_bind(struct qsock *qs, uint32_t port)
{
	uint32_t p;
	unsigned n;

	if (port == 0) {
		for (n = 0; n <= EPH_MAX - EPH_MIN; n++) {
			p = next_eph++;
			if (next_eph > EPH_MAX)
				next_eph = EPH_MIN;
			if (qs_by_port(p) == NULL)
				break;
		}
		if (n > EPH_MAX - EPH_MIN)
			return EADDRINUSE;
		port = p;
	} else if (port == QRTR_PORT_CTRL || port > 0xffff) {
		return EADDRNOTAVAIL;	/* the name server's, or no port */
	} else if (qs_by_port(port) != NULL) {
		return qs_by_port(port) == qs ? OK : EADDRINUSE;
	}
	if (qs->port != 0)
		rt_unbind(qs);
	qs->port = port;
	return OK;
}

/* qrtr_port_remove(): DEL_CLIENT to every node, and to our name server. */
void
rt_unbind(struct qsock *qs)
{
	struct qrtr_ctrl_pkt pkt;

	if (qs->port == 0)
		return;
	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_DEL_CLIENT;
	pkt.client.node = QRTR_LOCAL_NODE;
	pkt.client.port = qs->port;
	(void)remote_out(QRTR_TYPE_DEL_CLIENT, QRTR_LOCAL_NODE, qs->port,
	    QRTR_NODE_BCAST, QRTR_PORT_CTRL, &pkt, sizeof(pkt));
	ns_rx(QRTR_LOCAL_NODE, qs->port, &pkt, sizeof(pkt));
	qs->port = 0;
}

/*===========================================================================*
 *				from the modem				     *
 *===========================================================================*/
void
rt_from_remote(const uint8_t *p, size_t len)
{
	uint32_t w[8], type, src_node, src_port, dst_node, dst_port, size;
	int confirm;
	size_t hlen;
	struct qrtr_ctrl_pkt ctl;
	struct flow *f;

	if (len < 4 || (len & 3) != 0)
		goto bad;
	switch (p[0]) {
	case PROTO_VER_1:
		if (len < HDR_V1)
			goto bad;
		memcpy(w, p, HDR_V1);
		type = w[1];
		src_node = w[2];
		src_port = w[3];
		confirm = w[4] != 0;
		size = w[5];
		dst_node = w[6];
		dst_port = w[7];
		hlen = HDR_V1;
		break;
	case PROTO_VER_2: {
		uint16_t h16[4];

		if (len < HDR_V2)
			goto bad;
		type = p[1];
		confirm = p[2] & 1;
		hlen = HDR_V2 + p[3];
		memcpy(&size, p + 4, 4);
		memcpy(h16, p + 8, 8);
		src_node = h16[0];
		src_port = h16[1] == 0xfffe ? QRTR_PORT_CTRL : h16[1];
		dst_node = h16[2];
		dst_port = h16[3] == 0xfffe ? QRTR_PORT_CTRL : h16[3];
		break;
	}
	default:
		goto bad;
	}
	if (dst_port == PORT_CTRL_LEGACY)
		dst_port = QRTR_PORT_CTRL;
	if (type == QRTR_TYPE_HELLO)
		remote_nid = src_node;
	if (size == 0 || len != ((size + 3) & ~3u) + hlen)
		goto bad;
	p += hlen;

	if (type == QRTR_TYPE_RESUME_TX) {
		if (size >= sizeof(ctl)) {
			memcpy(&ctl, p, sizeof(ctl));
			if ((f = flow_get(ctl.client.node, ctl.client.port, 0))
			    != NULL)
				f->pending = 0;
		}
		return;
	}
	if (dst_port != QRTR_PORT_CTRL && type != QRTR_TYPE_DATA)
		goto bad;
	if (dst_port == QRTR_PORT_CTRL) {
		ns_rx(src_node, src_port, p, size);
		return;
	}
	if (dst_node != QRTR_LOCAL_NODE && dst_node != QRTR_NODE_BCAST)
		log_debug(&qlog, "a packet for node %u, not ours\n", dst_node);
	if (local_out(src_node, src_port, dst_port, p, size, confirm, NULL) !=
	    OK) {
		log_debug(&qlog, "%u:%u -> port %u: nobody there\n", src_node,
		    src_port, dst_port);
		/* Its RESUME_TX now, or that port's flow would stall. */
		if (confirm)
			rt_resume_tx(dst_port, src_node, src_port);
	}
	return;
bad:
	log_warn(&qlog, "a malformed packet from the modem (%zu bytes)\n", len);
}

/*===========================================================================*
 *				the name server				     *
 *===========================================================================*/
static void
ctrl_server(struct qrtr_ctrl_pkt *pkt, uint32_t cmd, const struct server *s)
{
	memset(pkt, 0, sizeof(*pkt));
	pkt->cmd = cmd;
	if (s != NULL) {
		pkt->server.service = s->service;
		pkt->server.instance = s->instance;
		pkt->server.node = s->node;
		pkt->server.port = s->port;
	}
}

static void
lookups_notify(const struct server *s, uint32_t cmd)
{
	struct qrtr_ctrl_pkt pkt;
	unsigned i;

	ctrl_server(&pkt, cmd, s);
	for (i = 0; i < MAX_LOOKUPS; i++) {
		if (!lookups[i].used)
			continue;
		if (lookups[i].service && lookups[i].service != s->service)
			continue;
		if (lookups[i].instance && lookups[i].instance != s->instance)
			continue;
		ns_send(QRTR_LOCAL_NODE, lookups[i].port, &pkt);
	}
}

static void
server_del(struct server *s, int bcast)
{
	struct qrtr_ctrl_pkt pkt;

	if (s->node == QRTR_LOCAL_NODE && bcast) {
		ctrl_server(&pkt, QRTR_TYPE_DEL_SERVER, s);
		ns_send(QRTR_NODE_BCAST, QRTR_PORT_CTRL, &pkt);
	}
	lookups_notify(s, QRTR_TYPE_DEL_SERVER);
	log_info(&qlog, "server %u:%u at %u:%u gone\n", s->service,
	    s->instance, s->node, s->port);
	s->used = 0;
}

static struct server *
server_find(uint32_t node, uint32_t port)
{
	unsigned i;

	for (i = 0; i < MAX_SERVERS; i++)
		if (servers[i].used && servers[i].node == node &&
		    servers[i].port == port)
			return &servers[i];
	return NULL;
}

/* Linux's server_match(): service 0 any; instance with its filter. */
static int
server_match(const struct server *s, uint32_t service, uint32_t instance)
{
	if (service != 0 && s->service != service)
		return 0;
	if (instance == 0)
		return 1;
	return s->instance == instance;
}

static void
announce_local(uint32_t node)
{
	struct qrtr_ctrl_pkt pkt;
	unsigned i;

	for (i = 0; i < MAX_SERVERS; i++)
		if (servers[i].used && servers[i].node == QRTR_LOCAL_NODE) {
			ctrl_server(&pkt, QRTR_TYPE_NEW_SERVER, &servers[i]);
			ns_send(node, QRTR_PORT_CTRL, &pkt);
		}
}

static void
ns_rx(uint32_t from_node, uint32_t from_port, const void *data, size_t len)
{
	struct qrtr_ctrl_pkt in, out;
	struct server *s;
	uint32_t node, port;
	unsigned i;

	if (len < 4)
		return;
	memset(&in, 0, sizeof(in));
	memcpy(&in, data, len < sizeof(in) ? len : sizeof(in));

	switch (in.cmd) {
	case QRTR_TYPE_HELLO:
		/* A node came up: hello back, and our servers. */
		memset(&out, 0, sizeof(out));
		out.cmd = QRTR_TYPE_HELLO;
		ns_send(from_node, QRTR_PORT_CTRL, &out);
		announce_local(from_node);
		log_info(&qlog, "node %u says hello\n", from_node);
		break;

	case QRTR_TYPE_BYE:
		for (i = 0; i < MAX_SERVERS; i++)
			if (servers[i].used && servers[i].node == from_node)
				server_del(&servers[i], 1);
		log_info(&qlog, "node %u says bye\n", from_node);
		break;

	case QRTR_TYPE_NEW_SERVER:
		node = in.server.node;
		port = in.server.port;
		if (from_node == QRTR_LOCAL_NODE) {
			node = from_node;
			port = from_port;
		}
		if (in.server.service == 0 || port == 0)
			break;
		if ((s = server_find(node, port)) == NULL) {
			for (i = 0; i < MAX_SERVERS && servers[i].used; i++)
				;
			if (i == MAX_SERVERS) {
				log_warn(&qlog, "server table full\n");
				break;
			}
			s = &servers[i];
		}
		s->used = 1;
		s->service = in.server.service;
		s->instance = in.server.instance;
		s->node = node;
		s->port = port;
		log_info(&qlog, "server %u:%u at %u:%u\n", s->service,
		    s->instance, node, port);
		if (node == QRTR_LOCAL_NODE) {
			ctrl_server(&out, QRTR_TYPE_NEW_SERVER, s);
			ns_send(QRTR_NODE_BCAST, QRTR_PORT_CTRL, &out);
		}
		lookups_notify(s, QRTR_TYPE_NEW_SERVER);
		break;

	case QRTR_TYPE_DEL_SERVER:
		node = in.server.node;
		port = in.server.port;
		if (from_node == QRTR_LOCAL_NODE) {
			if (from_port != port && port != 0)
				break;
			node = from_node;
			port = from_port;
		}
		if ((s = server_find(node, port)) != NULL)
			server_del(s, 1);
		break;

	case QRTR_TYPE_DEL_CLIENT:
		node = in.client.node;
		port = in.client.port;
		if (from_node != node ||
		    (from_node == QRTR_LOCAL_NODE && from_port != port))
			break;
		for (i = 0; i < MAX_LOOKUPS; i++)
			if (lookups[i].used && node == QRTR_LOCAL_NODE &&
			    lookups[i].port == port)
				lookups[i].used = 0;
		if ((s = server_find(node, port)) != NULL)
			server_del(s, 0);
		/* Our servers learn of it. */
		memset(&out, 0, sizeof(out));
		out.cmd = QRTR_TYPE_DEL_CLIENT;
		out.client.node = node;
		out.client.port = port;
		for (i = 0; i < MAX_SERVERS; i++)
			if (servers[i].used &&
			    servers[i].node == QRTR_LOCAL_NODE &&
			    !(node == QRTR_LOCAL_NODE &&
			    servers[i].port == port))
				ns_send(QRTR_LOCAL_NODE, servers[i].port,
				    &out);
		break;

	case QRTR_TYPE_NEW_LOOKUP:
		if (from_node != QRTR_LOCAL_NODE)
			break;
		for (i = 0; i < MAX_LOOKUPS && lookups[i].used; i++)
			;
		if (i < MAX_LOOKUPS) {
			lookups[i].used = 1;
			lookups[i].service = in.server.service;
			lookups[i].instance = in.server.instance;
			lookups[i].port = from_port;
		} else
			log_warn(&qlog, "lookup table full\n");
		for (i = 0; i < MAX_SERVERS; i++)
			if (servers[i].used && server_match(&servers[i],
			    in.server.service, in.server.instance)) {
				ctrl_server(&out, QRTR_TYPE_NEW_SERVER,
				    &servers[i]);
				ns_send(QRTR_LOCAL_NODE, from_port, &out);
			}
		ctrl_server(&out, QRTR_TYPE_NEW_SERVER, NULL);
		ns_send(QRTR_LOCAL_NODE, from_port, &out);
		break;

	case QRTR_TYPE_DEL_LOOKUP:
		for (i = 0; i < MAX_LOOKUPS; i++)
			if (lookups[i].used && from_node == QRTR_LOCAL_NODE &&
			    lookups[i].port == from_port &&
			    lookups[i].service == in.server.service &&
			    (!lookups[i].instance ||
			    lookups[i].instance == in.server.instance))
				lookups[i].used = 0;
		break;

	default:	/* EXIT, PING, RESUME_TX, anything else */
		break;
	}
}

/*===========================================================================*
 *				rt_link, rt_init			     *
 *===========================================================================*/
/* The link to the modem came up, or went: its servers are gone then. */
void
rt_link(int up)
{
	unsigned i;

	link_up = up;
	if (up)
		return;
	for (i = 0; i < MAX_SERVERS; i++)
		if (servers[i].used && servers[i].node != QRTR_LOCAL_NODE)
			server_del(&servers[i], 0);
	memset(flows, 0, sizeof(flows));
}

void
rt_init(void)
{
	memset(servers, 0, sizeof(servers));
	memset(lookups, 0, sizeof(lookups));
	memset(flows, 0, sizeof(flows));
}
