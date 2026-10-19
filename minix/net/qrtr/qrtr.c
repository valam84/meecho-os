/*
 * qrtr: AF_QIPCRTR sockets (<netqrtr/qrtr.h>) - datagrams to the services
 * of the Mi Pad 4's modem through Qualcomm's IPC router, and between
 * processes of this side.  The semantics are Linux's (af_qrtr.c); the
 * router and name server are router.c, the link to the modem glink.c.
 * port/clover/hw-modem.md, sections 1 and 5.
 *
 * Without a modem in the tree the service still serves its sockets:
 * processes of this side can find each other through the name server.
 *
 * Arguments: log=N (0..5, 3 by default).
 */

#include "qrtr.h"

#include <minix/sysutil.h>
#include <minix/timers.h>

#include <stdlib.h>
#include <string.h>

#define NR_SOCKS	64
#define QUEUE_MAX	64		/* datagrams a socket holds */

struct log qlog = {
	.name = "qrtr",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static struct qsock socks[NR_SOCKS];
static minix_timer_t poll_timer;
static int have_link;

static const struct sockevent_ops qrtr_ops;

/*===========================================================================*
 *				ports, delivery				     *
 *===========================================================================*/
struct qsock *
qs_by_port(uint32_t port)
{
	unsigned i;

	for (i = 0; i < NR_SOCKS; i++)
		if (socks[i].used && socks[i].port == port)
			return &socks[i];
	return NULL;
}

void
qs_deliver(struct qsock *qs, uint32_t src_node, uint32_t src_port,
	const void *data, size_t len, int confirm_rx)
{
	struct qpkt *p;

	if (qs->qlen >= QUEUE_MAX ||
	    (p = malloc(sizeof(*p) + len)) == NULL) {
		log_debug(&qlog, "port %u: queue full, a packet dropped\n",
		    qs->port);
		if (confirm_rx)
			rt_resume_tx(qs->port, src_node, src_port);
		return;
	}
	p->next = NULL;
	p->src_node = src_node;
	p->src_port = src_port;
	p->confirm_rx = confirm_rx;
	p->len = len;
	memcpy(p->data, data, len);
	if (qs->tail != NULL)
		qs->tail->next = p;
	else
		qs->head = p;
	qs->tail = p;
	qs->qlen++;
	sockevent_raise(&qs->sock, SEV_RECV);
}

/*===========================================================================*
 *				addresses				     *
 *===========================================================================*/
static int
addr_get(const struct sockaddr *addr, socklen_t len, uint32_t *node,
	uint32_t *port)
{
	struct sockaddr_qrtr sq;

	if (len < (socklen_t)sizeof(sq))
		return EINVAL;
	memcpy(&sq, addr, sizeof(sq));
	if (sq.sq_family != AF_QIPCRTR)
		return EAFNOSUPPORT;
	*node = sq.sq_node;
	*port = sq.sq_port;
	return OK;
}

static void
addr_put(struct sockaddr *addr, socklen_t *len, uint32_t node, uint32_t port)
{
	struct sockaddr_qrtr sq;

	memset(&sq, 0, sizeof(sq));
	sq.sq_len = sizeof(sq);
	sq.sq_family = AF_QIPCRTR;
	sq.sq_node = node;
	sq.sq_port = port;
	memcpy(addr, &sq, sizeof(sq));
	*len = sizeof(sq);
}

/*===========================================================================*
 *				operations				     *
 *===========================================================================*/
static int
qrtr_bind(struct sock *sock, const struct sockaddr *addr, socklen_t len,
	endpoint_t user_endpt __unused)
{
	struct qsock *qs = (struct qsock *)sock;
	uint32_t node, port;
	int r;

	if ((r = addr_get(addr, len, &node, &port)) != OK)
		return r;
	if (node != QRTR_LOCAL_NODE)
		return EADDRNOTAVAIL;
	return rt_bind(qs, port);
}

static int
qrtr_connect(struct sock *sock, const struct sockaddr *addr, socklen_t len,
	endpoint_t user_endpt __unused)
{
	struct qsock *qs = (struct qsock *)sock;
	uint32_t node, port;
	int r;

	if ((r = addr_get(addr, len, &node, &port)) != OK)
		return r;
	if (qs->port == 0 && (r = rt_bind(qs, 0)) != OK)
		return r;
	qs->connected = 1;
	qs->peer_node = node;
	qs->peer_port = port;
	return OK;
}

static int
qrtr_send(struct sock *sock, const struct sockdriver_data *data, size_t len,
	size_t *off, const struct sockdriver_data *ctl __unused,
	socklen_t ctl_len, socklen_t *ctl_off __unused,
	const struct sockaddr *addr, socklen_t addr_len,
	endpoint_t user_endpt __unused, int flags, size_t min __unused)
{
	struct qsock *qs = (struct qsock *)sock;
	uint32_t node, port;
	uint8_t *buf;
	int r;

	if (ctl_len != 0 || (flags & ~MSG_DONTWAIT) != 0)
		return EINVAL;
	if (len > QRTR_MAX_PACKET)
		return EMSGSIZE;
	if (addr != NULL) {
		if ((r = addr_get(addr, addr_len, &node, &port)) != OK)
			return r;
	} else if (qs->connected) {
		node = qs->peer_node;
		port = qs->peer_port;
	} else
		return ENOTCONN;

	if ((buf = malloc(len ? len : 1)) == NULL)
		return ENOMEM;
	if (len > 0 && (r = sockdriver_copyin(data, 0, buf, len)) != OK) {
		free(buf);
		return r;
	}
	r = rt_send(qs, node, port, buf, len);
	free(buf);
	if (r != OK)
		return r;
	*off = len;
	gl_poll();		/* the FIFO, now rather than at the next tick */
	return OK;
}

static int
qrtr_recv(struct sock *sock, const struct sockdriver_data *data, size_t len,
	size_t *off, const struct sockdriver_data *ctl __unused,
	socklen_t ctl_len __unused, socklen_t *ctl_off __unused,
	struct sockaddr *addr, socklen_t *addr_len,
	endpoint_t user_endpt __unused, int flags, size_t min __unused,
	int *rflags)
{
	struct qsock *qs = (struct qsock *)sock;
	struct qpkt *p;
	size_t n;
	int r;

	if (qs->port == 0)
		return EADDRNOTAVAIL;
	if ((p = qs->head) == NULL)
		return SUSPEND;
	n = p->len < len ? p->len : len;
	if (n > 0 && (r = sockdriver_copyout(data, 0, p->data, n)) != OK)
		return r;
	if (n < p->len)
		*rflags |= MSG_TRUNC;
	*off = n;
	addr_put(addr, addr_len, p->src_node, p->src_port);
	if (flags & MSG_PEEK)
		return OK;
	qs->head = p->next;
	if (qs->head == NULL)
		qs->tail = NULL;
	qs->qlen--;
	if (p->confirm_rx)
		rt_resume_tx(qs->port, p->src_node, p->src_port);
	free(p);
	return OK;
}

static int
qrtr_test_recv(struct sock *sock, size_t min __unused, size_t *size)
{
	struct qsock *qs = (struct qsock *)sock;

	if (qs->head == NULL)
		return SUSPEND;
	if (size != NULL)
		*size = qs->head->len;
	return OK;
}

static int
qrtr_getsockname(struct sock *sock, struct sockaddr *addr, socklen_t *len)
{
	struct qsock *qs = (struct qsock *)sock;

	addr_put(addr, len, QRTR_LOCAL_NODE, qs->port);
	return OK;
}

static int
qrtr_getpeername(struct sock *sock, struct sockaddr *addr, socklen_t *len)
{
	struct qsock *qs = (struct qsock *)sock;

	if (!qs->connected)
		return ENOTCONN;
	addr_put(addr, len, qs->peer_node, qs->peer_port);
	return OK;
}

static int
qrtr_close(struct sock *sock, int force __unused)
{
	struct qsock *qs = (struct qsock *)sock;
	struct qpkt *p;

	rt_unbind(qs);
	while ((p = qs->head) != NULL) {
		qs->head = p->next;
		free(p);
	}
	qs->tail = NULL;
	qs->qlen = 0;
	return OK;
}

static void
qrtr_free(struct sock *sock)
{
	struct qsock *qs = (struct qsock *)sock;

	qs->used = 0;
	qs->port = 0;
	qs->connected = 0;
}

static const struct sockevent_ops qrtr_ops = {
	.sop_bind		= qrtr_bind,
	.sop_connect		= qrtr_connect,
	.sop_send		= qrtr_send,
	.sop_recv		= qrtr_recv,
	.sop_test_recv		= qrtr_test_recv,
	.sop_getsockname	= qrtr_getsockname,
	.sop_getpeername	= qrtr_getpeername,
	.sop_close		= qrtr_close,
	.sop_free		= qrtr_free,
};

static sockid_t
qrtr_socket(int domain, int type, int protocol, endpoint_t user_endpt __unused,
	struct sock **sockp, const struct sockevent_ops **ops)
{
	unsigned i;

	if (domain != PF_QIPCRTR)
		return EAFNOSUPPORT;
	if (type != SOCK_DGRAM)
		return EPROTOTYPE;
	if (protocol != 0)
		return EPROTONOSUPPORT;
	for (i = 0; i < NR_SOCKS && socks[i].used; i++)
		;
	if (i == NR_SOCKS)
		return ENOBUFS;
	memset(&socks[i], 0, sizeof(socks[i]));
	socks[i].used = 1;
	*sockp = &socks[i].sock;
	*ops = &qrtr_ops;
	return (sockid_t)i;
}

/*===========================================================================*
 *				main					     *
 *===========================================================================*/
static void
poll_tick(int arg __unused)
{
	gl_poll();
	set_timer(&poll_timer, 1, poll_tick, 0);
}

static int
qrtr_init(int type __unused, sef_init_info_t *info __unused)
{
	long lv;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		qlog.log_level = (int)lv;
	rt_init();
	sockevent_init(qrtr_socket);
	have_link = gl_init() == OK;
	init_timer(&poll_timer);
	if (have_link)
		set_timer(&poll_timer, 1, poll_tick, 0);
	return OK;
}

int
main(int argc, char **argv)
{
	message m;
	int r, ipc_status;

	env_setargs(argc, argv);
	sef_setcb_init_fresh(qrtr_init);
	sef_startup();
	for (;;) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK) {
			if (r == EINTR)
				continue;
			panic("qrtr: sef_receive_status: %d", r);
		}
		sockevent_process(&m, ipc_status);
	}
	return 0;
}
