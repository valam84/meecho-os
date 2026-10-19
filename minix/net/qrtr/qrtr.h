/*
 * qrtr: Qualcomm's IPC router as a socket family (AF_QIPCRTR) - the
 * transport to the modem's services (GLINK over SMEM, glink.c), the router
 * and its name server (router.c), the sockets (qrtr.c).
 * port/clover/hw-modem.md, sections 1 and 5.
 */
#ifndef _QRTR_H
#define _QRTR_H

#include <minix/drivers.h>
#include <minix/sockevent.h>
#include <minix/log.h>

#include <sys/socket.h>
#include <netqrtr/qrtr.h>

#include <stdint.h>

#include "smem.h"

#define QRTR_LOCAL_NODE		1	/* this side, as Linux's qrtr_local_nid */
#define QRTR_MAX_PACKET		65536

extern struct log qlog;

/* A datagram waiting in a socket. */
struct qpkt {
	struct qpkt *next;
	uint32_t src_node, src_port;
	int confirm_rx;
	size_t len;
	uint8_t data[];
};

struct qsock {
	struct sock sock;		/* first: libsockevent's */
	int used;
	uint32_t port;			/* 0: not bound yet */
	int connected;
	uint32_t peer_node, peer_port;
	struct qpkt *head, *tail;
	unsigned qlen;
};

/* qrtr.c */
struct qsock *qs_by_port(uint32_t port);
void qs_deliver(struct qsock *qs, uint32_t src_node, uint32_t src_port,
	const void *data, size_t len, int confirm_rx);

/* router.c */
void rt_init(void);
int rt_bind(struct qsock *qs, uint32_t port);
void rt_unbind(struct qsock *qs);
int rt_send(struct qsock *qs, uint32_t dst_node, uint32_t dst_port,
	const void *data, size_t len);
void rt_resume_tx(uint32_t local_port, uint32_t remote_node,
	uint32_t remote_port);
void rt_from_remote(const uint8_t *pkt, size_t len);
void rt_link(int up);

/* glink.c */
int gl_init(void);
void gl_poll(void);
int gl_up(void);
int gl_send(const void *hdr, size_t hlen, const void *data, size_t dlen);

#endif /* _QRTR_H */
