/*
 * Qualcomm's IPC router (QRTR): datagrams between ports of the processors
 * of a Qualcomm SoC - the modem's and the DSPs' services, and this side's.
 *
 *	int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
 *
 * The socket family is served by the qrtr service (minix/net/qrtr); what
 * follows are the names and values of Linux's <linux/qrtr.h>, so that code
 * written for Linux's AF_QIPCRTR builds unchanged, with BSD's sq_len in
 * front of sq_family as in every BSD socket address.  Semantics are
 * Linux's too: port/clover/hw-modem.md, section 1, in the MEECHO tree.
 *
 *   - a node is a processor: the modem is 0, this side is 1;
 *     getsockname() names this socket's node and port;
 *   - bind() with port 0, or the first send without one, takes a free
 *     port;
 *   - a QRTR_TYPE_NEW_LOOKUP control packet sent to {own node,
 *     QRTR_PORT_CTRL} asks the name server: every matching server comes
 *     back as a QRTR_TYPE_NEW_SERVER packet from that address, then one
 *     with every field 0; new and departing servers follow as they come;
 *     QRTR_TYPE_NEW_SERVER sent there announces a service of one's own.
 */
#ifndef _NETQRTR_QRTR_H
#define _NETQRTR_QRTR_H

#include <sys/types.h>
#include <sys/socket.h>

#define QRTR_NODE_BCAST		0xffffffffu
#define QRTR_PORT_CTRL		0xfffffffeu

struct sockaddr_qrtr {
	uint8_t		sq_len;		/* sizeof(struct sockaddr_qrtr) */
	sa_family_t	sq_family;	/* AF_QIPCRTR */
	uint32_t	sq_node;
	uint32_t	sq_port;
};

enum qrtr_pkt_type {
	QRTR_TYPE_DATA		= 1,
	QRTR_TYPE_HELLO		= 2,
	QRTR_TYPE_BYE		= 3,
	QRTR_TYPE_NEW_SERVER	= 4,
	QRTR_TYPE_DEL_SERVER	= 5,
	QRTR_TYPE_DEL_CLIENT	= 6,
	QRTR_TYPE_RESUME_TX	= 7,
	QRTR_TYPE_EXIT		= 8,
	QRTR_TYPE_PING		= 9,
	QRTR_TYPE_NEW_LOOKUP	= 10,
	QRTR_TYPE_DEL_LOOKUP	= 11,
};

/* A control packet: all fields little-endian, as on the wire. */
struct qrtr_ctrl_pkt {
	uint32_t cmd;

	union {
		struct {
			uint32_t service;
			uint32_t instance;
			uint32_t node;
			uint32_t port;
		} server;

		struct {
			uint32_t node;
			uint32_t port;
		} client;
	};
} __packed;

#endif /* _NETQRTR_QRTR_H */
