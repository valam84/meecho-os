/*
 * HTC over the WCN3990's copy engines (wcn_htc.h).  The order and the
 * numbers are Linux's htc.c for this chip: credits only for WMI, one to
 * begin with; every other service connected without credit flow.
 */

#include <errno.h>
#include <string.h>

#include "wcn_htc.h"

static uint16_t
get16(const uint8_t *p)
{

	return p[0] | p[1] << 8;
}

static void
put16(uint8_t *p, uint16_t v)
{

	p[0] = v;
	p[1] = v >> 8;
}

int
htc_svc_pipes(uint16_t svc, uint8_t *ul, uint8_t *dl)
{

	/* The map given to the firmware in WLAN_CFG (wcn_wlfw.c). */
	switch (svc) {
	case HTC_SVC_RSVD_CTRL:
		*ul = 0;
		*dl = 2;
		return 0;
	case HTC_SVC_WMI_CONTROL:
	case 0x0101: case 0x0102: case 0x0103: case 0x0104:
		*ul = 3;
		*dl = 2;
		return 0;
	case HTC_SVC_HTT_DATA:
		*ul = 4;
		*dl = 1;
		return 0;
	default:
		/* Receive-only services (HTT data 2 and 3, the packet log)
		 * cannot be connected: HTC needs both directions. */
		return -1;
	}
}

void
htc_init(struct htc *h)
{

	memset(h, 0, sizeof(*h));
	h->htc_version = -1;
	/* Endpoint 0, HTC's own, without an exchange and without credits. */
	h->ep[0].svc = HTC_SVC_RSVD_CTRL;
	(void)htc_svc_pipes(HTC_SVC_RSVD_CTRL, &h->ep[0].ul, &h->ep[0].dl);
	h->ep[0].max_msg = 256;
}

int
htc_frame(struct htc *h, unsigned eid, uint8_t *buf, size_t len,
	unsigned *pipe)
{
	struct htc_ep *e;
	unsigned cost = 0;
	uint8_t flags = 0;

	if (eid >= HTC_MAX_EP || (e = &h->ep[eid])->svc == 0 ||
	    len > 0xffff)
		return EINVAL;
	if (e->credit_flow) {
		if (h->credit_size == 0)
			return EAGAIN;
		cost = (len + HTC_HDR + h->credit_size - 1) / h->credit_size;
		if (e->credits < cost)
			return EAGAIN;
		e->credits -= cost;
		flags |= HTC_TXF_NEED_CREDIT;
	}
	buf[0] = eid;
	buf[1] = flags;
	put16(buf + 2, len);
	buf[4] = 0;
	buf[5] = e->seq++;
	buf[6] = buf[7] = 0;
	*pipe = e->ul;
	return 0;
}

size_t
htc_connect_msg(uint8_t *b, uint16_t svc)
{
	uint16_t flags;

	/* All credits to WMI control (one: RECV_ALLOC 1), none to others. */
	flags = svc == HTC_SVC_WMI_CONTROL ? HTC_CONN_RECV_ALLOC(1) :
	    HTC_CONN_NO_CREDIT_FLOW;
	put16(b, HTC_MSG_CONNECT);
	put16(b + 2, svc);
	put16(b + 4, flags);
	b[6] = b[7] = 0;
	return 8;
}

size_t
htc_setup_complete_msg(uint8_t *b)
{

	memset(b, 0, 12);
	put16(b, HTC_MSG_SETUP_COMPLETE_EX);
	return 12;
}

int
htc_svc_ep(const struct htc *h, uint16_t svc)
{
	unsigned i;

	for (i = 0; i < HTC_MAX_EP; i++)
		if (h->ep[i].svc == svc)
			return (int)i;
	return -1;
}

/* Endpoint 0: READY and CONNECT_RESP.  0, or -1 if malformed. */
static int
control(struct htc *h, const uint8_t *m, size_t n)
{
	struct htc_ep *e;
	unsigned eid, max;
	uint16_t svc;

	if (n < 2)
		return -1;
	switch (get16(m)) {
	case HTC_MSG_READY:
		if (n < 8 || get16(m + 4) == 0)
			return -1;
		h->target_credits = get16(m + 2);
		h->credit_size = get16(m + 4);
		h->max_ep = m[6];
		/* The extension is told by the length only. */
		if (n >= 12)
			h->htc_version = m[8];
		h->ready = 1;
		return 0;
	case HTC_MSG_CONNECT_RESP:
		if (n < 8)
			return -1;
		svc = get16(m + 2);
		h->conn_done = 1;
		h->conn_svc = svc;
		h->conn_status = m[4];
		h->conn_eid = m[5];
		eid = m[5];
		max = get16(m + 6);
		if (m[4] != 0)
			return 0;		/* refused: the caller reads it */
		if (eid == 0 || eid >= HTC_MAX_EP || max == 0 ||
		    h->ep[eid].svc != 0) {
			h->conn_status = 0xff;	/* not usable */
			return 0;
		}
		e = &h->ep[eid];
		if (htc_svc_pipes(svc, &e->ul, &e->dl) != 0) {
			h->conn_status = 0xff;
			return 0;
		}
		e->svc = svc;
		e->max_msg = max;
		e->seq = 0;
		e->credit_flow = svc == HTC_SVC_WMI_CONTROL;
		e->credits = e->credit_flow ? 1 : 0;
		return 0;
	default:
		return 0;			/* nothing we wait for */
	}
}

/* The trailer: credits back to their endpoints.  0 or -1. */
static int
trailer(struct htc *h, const uint8_t *t, size_t n)
{
	size_t at = 0, l, i;

	while (at < n) {
		if (n - at < 4)
			return -1;
		l = t[at + 1];
		if (l > n - at - 4)
			return -1;
		switch (t[at]) {
		case HTC_TR_CREDITS:
			if (l % 4 != 0)
				return -1;
			for (i = 0; i < l; i += 4) {
				unsigned eid = t[at + 4 + i];

				if (eid < HTC_MAX_EP && h->ep[eid].svc != 0 &&
				    h->ep[eid].credit_flow)
					h->ep[eid].credits += t[at + 5 + i];
			}
			break;
		case HTC_TR_LOOKAHEAD_BUNDLE:
			/* A count of 0 or past a bundle: the frame is bad. */
			if (l == 0 || l / 4 > 32)
				return -1;
			break;
		default:
			/* LOOKAHEAD and others: checked only by length. */
			break;
		}
		at += 4 + l;
	}
	return 0;
}

int
htc_rx(struct htc *h, const uint8_t *buf, size_t len, unsigned *eid,
	const uint8_t **msg, size_t *mlen)
{
	size_t n, tl;
	unsigned e;

	if (len < HTC_HDR)
		goto bad;
	e = buf[0];
	n = get16(buf + 2);
	if (n > len - HTC_HDR || n + HTC_HDR > HTC_RX_MAX)
		goto bad;
	if (e >= HTC_MAX_EP || h->ep[e].svc == 0)
		goto bad;
	if (buf[1] & HTC_RXF_TRAILER) {
		tl = buf[4];
		if (tl < 4 || tl > n)
			goto bad;
		if (trailer(h, buf + HTC_HDR + n - tl, tl) != 0)
			goto bad;
		n -= tl;
	}
	if (n == 0)
		return 0;
	if (e == 0)
		return control(h, buf + HTC_HDR, n) == 0 ? 0 : (h->dropped++,
		    -1);
	*eid = e;
	*msg = buf + HTC_HDR;
	*mlen = n;
	return 1;
bad:
	h->dropped++;
	return -1;
}
