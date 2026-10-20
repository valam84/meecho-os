#ifndef _WCN_HTC_H
#define _WCN_HTC_H

/*
 * HTC, the framing every WMI and HTT message of the WCN3990 carries over
 * the copy engines (port/clover/hw-wifi.md, section 5; the layouts in
 * port/clover/wifi/htc-wmi.md, section 2).
 *
 * An 8-byte header - endpoint, flags, length, control bytes - then the
 * message.  Endpoint 0 is HTC's own control: the target says READY, the
 * host connects the services it wants (WMI control, HTT data) and the
 * target names the endpoint of each; SETUP_COMPLETE_EX ends it.  Credits:
 * on this chip only WMI has them, one at a start - a WMI command waits
 * for the previous one's credit, which comes back in a received frame's
 * trailer.
 *
 * No MINIX and no copy engines here: a frame is built into a buffer and
 * handed to the caller with the pipe it goes on; a received frame is
 * parsed out of one.  port/test/wcn/htctest.c.
 */

#include <stddef.h>
#include <stdint.h>

#define HTC_HDR			8
#define HTC_MAX_EP		9	/* endpoint ids the target may name */
#define HTC_RX_MAX		4096	/* header + message, at most */

/* Services. */
#define HTC_SVC_RSVD_CTRL	0x0001
#define HTC_SVC_WMI_CONTROL	0x0100
#define HTC_SVC_HTT_DATA	0x0300

/* Control messages (endpoint 0). */
#define HTC_MSG_READY		1
#define HTC_MSG_CONNECT		2
#define HTC_MSG_CONNECT_RESP	3
#define HTC_MSG_SETUP_COMPLETE_EX 5

/* Receive flags; send flags. */
#define HTC_RXF_TRAILER		0x02
#define HTC_TXF_NEED_CREDIT	0x01

/* CONNECT flags. */
#define HTC_CONN_NO_CREDIT_FLOW	0x0008
#define HTC_CONN_RECV_ALLOC(n)	((n) << 8)

/* Trailer records. */
#define HTC_TR_CREDITS		1
#define HTC_TR_LOOKAHEAD	2
#define HTC_TR_LOOKAHEAD_BUNDLE	3

struct htc_ep {
	uint16_t svc;			/* 0: not connected */
	uint8_t ul, dl;			/* copy-engine pipes */
	uint8_t seq;
	int credit_flow;
	unsigned credits;
	unsigned max_msg;
};

struct htc {
	int ready;			/* READY came */
	unsigned credit_size;		/* bytes a credit pays for */
	unsigned target_credits;	/* what READY said (not used: 1) */
	unsigned max_ep;
	int htc_version;		/* -1: READY without the extension */
	struct htc_ep ep[HTC_MAX_EP];
	/* The last CONNECT_RESP, for the caller waiting on it. */
	int conn_done;
	uint16_t conn_svc;
	uint8_t conn_status, conn_eid;
	unsigned long dropped;		/* frames thrown away, malformed */
};

/* The pipes of a service (the map WLAN_CFG gave the firmware): 0 or -1. */
int htc_svc_pipes(uint16_t svc, uint8_t *ul, uint8_t *dl);

/* Endpoint 0 connected locally, as Linux's htc_init does. */
void htc_init(struct htc *h);

/*
 * Put an HTC header in front of the "len" bytes at buf + HTC_HDR for
 * endpoint "eid": 0 and the pipe to send on, or EAGAIN (no credit: wait
 * for a received frame), EINVAL.  Takes the credit and the sequence
 * number.
 */
int htc_frame(struct htc *h, unsigned eid, uint8_t *buf, size_t len,
	unsigned *pipe);

/* Control messages' bodies (after the header): their length. */
size_t htc_connect_msg(uint8_t *body, uint16_t svc);
size_t htc_setup_complete_msg(uint8_t *body);

/*
 * A received frame: check it, take the trailer's credits, handle endpoint
 * 0 (READY, CONNECT_RESP) here.  1 and the endpoint and message of a
 * frame for a service; 0 if it was HTC's own or carried nothing; -1 if it
 * was malformed (counted, dropped).
 */
int htc_rx(struct htc *h, const uint8_t *buf, size_t len, unsigned *eid,
	const uint8_t **msg, size_t *mlen);

/* The endpoint of a connected service, or -1. */
int htc_svc_ep(const struct htc *h, uint16_t svc);

#endif /* _WCN_HTC_H */
