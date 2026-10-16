/*
 * GLINK over the RPM's message RAM, and an RPM request on it (glink.h).
 *
 * Followed line for line from Linux (port/clover/hw-rpm.md):
 *
 *   qcom_glink_rpm.c     the table of contents at the end of the message RAM
 *                        ("grt0"), the two FIFOs ap2r and r2ap - tail word,
 *                        head word, then the ring - 32-bit accesses only,
 *                        every write padded to 8 bytes, and at start-up this
 *                        side's two indices (tx head, rx tail) set to 0
 *   qcom_glink_native.c  the 8-byte command header (cmd, param1, param2),
 *                        VERSION / VERSION_ACK with features, OPEN with the
 *                        name, OPEN_ACK, TX_DATA with chunk and left sizes,
 *                        intentless (the RPM gives no intents and is sent no
 *                        RX_DONE), and how far each received command moves
 *                        the tail
 *   smd-rpm.c            the request: "req" header, msg_id, active set,
 *                        resource type and id, key / length / value triples;
 *                        the answer: "msg#" and, on failure, "err" with text
 *
 * Polled: the RPM's interrupt is not used, the FIFOs are looked at every
 * 100 us until the awaited thing has come or the time is up.
 */

#include <errno.h>
#include <string.h>

#include "glink.h"

#define TOC_SIZE		256
#define TOC_MAGIC		0x67727430	/* "grt0" */
#define TX_FIFO_ID		0x61703272	/* "ap2r" */
#define RX_FIFO_ID		0x72326170	/* "r2ap" */

#define CMD_VERSION		0
#define CMD_VERSION_ACK		1
#define CMD_OPEN		2
#define CMD_CLOSE		3
#define CMD_OPEN_ACK		4
#define CMD_INTENT		5
#define CMD_RX_DONE		6
#define CMD_RX_INTENT_REQ	7
#define CMD_RX_INTENT_REQ_ACK	8
#define CMD_TX_DATA		9
#define CMD_CLOSE_ACK		11
#define CMD_TX_DATA_CONT	12
#define CMD_READ_NOTIF		13
#define CMD_RX_DONE_W_REUSE	14
#define CMD_SIGNALS		15

#define GLINK_VERSION_1		1
#define NAME_MAX_LEN		32

#define RPM_SERVICE_REQUEST	0x00716572	/* "req" */
#define RPM_MSG_ERR		0x00727265	/* "err" */
#define RPM_MSG_ID		0x2367736d	/* "msg#" */

#define POLL_US			100
#define OPEN_LOCAL_AFTER_MS	1000

#define ALIGN4(x)		(((x) + 3u) & ~3u)
#define ALIGN8(x)		(((x) + 7u) & ~7u)

static char chan_name[NAME_MAX_LEN];

static uint32_t
rd(struct glink *g, uint32_t off)
{
	return *(volatile uint32_t *)(g->ram + off);
}

static void
wr(struct glink *g, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(g->ram + off) = v;
}

/*===========================================================================*
 *    the FIFOs (qcom_glink_rpm.c)                                            *
 *===========================================================================*/
int
glink_init(struct glink *g)
{
	uint32_t magic, count, i, id, off, size;
	uint32_t toc = (uint32_t)g->ram_size - TOC_SIZE;

	if (g->ram == NULL || g->ram_size < TOC_SIZE || g->kick == NULL ||
	    g->delay_us == NULL)
		return EINVAL;

	g->tx_len = g->rx_len = 0;
	magic = rd(g, toc);
	count = rd(g, toc + 4);
	if (magic != TOC_MAGIC || count > (TOC_SIZE - 8) / 12)
		return EINVAL;

	for (i = 0; i < count; i++) {
		id = rd(g, toc + 8 + 12 * i);
		off = rd(g, toc + 8 + 12 * i + 4);
		size = rd(g, toc + 8 + 12 * i + 8);
		/* Linux's test; and the ring itself must lie inside too. */
		if (off > g->ram_size || off + size > g->ram_size ||
		    off + 8 + size > g->ram_size || (off & 3) || (size & 3) ||
		    size < 64)
			continue;
		if (id == RX_FIFO_ID) {
			g->rx_tail = off;
			g->rx_head = off + 4;
			g->rx_fifo = off + 8;
			g->rx_len = size;
		} else if (id == TX_FIFO_ID) {
			g->tx_tail = off;
			g->tx_head = off + 4;
			g->tx_fifo = off + 8;
			g->tx_len = size;
		}
	}
	if (g->rx_len == 0 || g->tx_len == 0)
		return EINVAL;

	g->found[0] = rd(g, g->tx_tail);
	g->found[1] = rd(g, g->tx_head);
	g->found[2] = rd(g, g->rx_tail);
	g->found[3] = rd(g, g->rx_head);

	/* glink_rpm_probe(): this side's indices start from zero. */
	wr(g, g->tx_head, 0);
	wr(g, g->rx_tail, 0);
	return GLINK_OK;
}

static int
rx_avail(struct glink *g, uint32_t *avail)
{
	uint32_t head = rd(g, g->rx_head), tail = rd(g, g->rx_tail);

	if (head >= g->rx_len || tail >= g->rx_len || (head & 3) || (tail & 3))
		return EPROTO;
	*avail = (head < tail) ? g->rx_len - tail + head : head - tail;
	return GLINK_OK;
}

/* count bytes from tail + off, rounded up to whole words. */
static void
rx_peek(struct glink *g, void *buf, uint32_t off, uint32_t count)
{
	uint32_t pos = (rd(g, g->rx_tail) + off) % g->rx_len, w;
	uint8_t *b = buf;
	unsigned i;

	for (i = 0; i < count; i += 4) {
		w = rd(g, g->rx_fifo + pos);
		memcpy(b + i, &w, (count - i >= 4) ? 4 : count - i);
		pos += 4;
		if (pos >= g->rx_len)
			pos -= g->rx_len;
	}
}

static void
rx_advance(struct glink *g, uint32_t count)
{
	uint32_t tail = rd(g, g->rx_tail) + count;

	while (tail >= g->rx_len)
		tail -= g->rx_len;
	wr(g, g->rx_tail, tail);
}

static uint32_t
tx_avail(struct glink *g)
{
	uint32_t head = rd(g, g->tx_head), tail = rd(g, g->tx_tail);

	if (head >= g->tx_len || tail >= g->tx_len)
		return 0;
	return (tail <= head) ? g->tx_len - head + tail : tail - head;
}

static void
send_read_notify(struct glink *g);

/*
 * hdr (a multiple of 4) and data, padded with zeros to 8 - what
 * glink_rpm_tx_write() puts in the ring - then the kick.  Strictly less
 * than the free space: a ring filled to the byte reads as empty.
 */
static int
tx(struct glink *g, const void *hdr, uint32_t hlen, const void *data,
	uint32_t dlen, unsigned timeout_ms)
{
	uint8_t buf[256];
	uint32_t need = ALIGN8(hlen + dlen), head, w, i;
	unsigned waited = 0;

	if ((hlen & 3) || need > sizeof(buf) || need >= g->tx_len)
		return EINVAL;
	memset(buf, 0, need);
	memcpy(buf, hdr, hlen);
	if (dlen)
		memcpy(buf + hlen, data, dlen);

	while (tx_avail(g) <= need) {
		if (!g->read_notify_sent) {
			g->read_notify_sent = 1;
			send_read_notify(g);
		}
		if (waited >= timeout_ms * 1000)
			return ETIMEDOUT;
		g->delay_us(g->ctx, POLL_US);
		waited += POLL_US;
	}
	g->read_notify_sent = 0;

	head = rd(g, g->tx_head);
	for (i = 0; i < need; i += 4) {
		memcpy(&w, buf + i, 4);
		wr(g, g->tx_fifo + head, w);
		head += 4;
		if (head >= g->tx_len)
			head -= g->tx_len;
	}
	wr(g, g->tx_head, head);
	g->kick(g->ctx);
	return GLINK_OK;
}

static int
send_cmd(struct glink *g, unsigned cmd, unsigned p1, uint32_t p2)
{
	uint32_t m[2];

	m[0] = (uint32_t)(cmd & 0xffff) | (uint32_t)(p1 & 0xffff) << 16;
	m[1] = p2;
	return tx(g, m, sizeof(m), NULL, 0, 1000);
}

/* qcom_glink_send_read_notify(): written even without room to wait for. */
static void
send_read_notify(struct glink *g)
{
	uint32_t m[2] = { CMD_READ_NOTIF, 0 }, head = rd(g, g->tx_head);

	wr(g, g->tx_fifo + head, m[0]);
	head = (head + 4) % g->tx_len;
	wr(g, g->tx_fifo + head, m[1]);
	head = (head + 4) % g->tx_len;
	wr(g, g->tx_head, head);
	g->kick(g->ctx);
}

/*===========================================================================*
 *    receiving (qcom_glink_native_rx() and its deferred half)                *
 *===========================================================================*/
static int
send_open(struct glink *g)
{
	struct {
		uint32_t m[2];
		char name[NAME_MAX_LEN];
	} req;
	uint32_t nlen = (uint32_t)strlen(chan_name) + 1;

	memset(&req, 0, sizeof(req));
	req.m[0] = CMD_OPEN | (uint32_t)g->lcid << 16;
	req.m[1] = nlen;
	memcpy(req.name, chan_name, nlen);
	g->open_sent = 1;
	/* qcom_glink_send_open_req(): ALIGN(sizeof(*req) + name_len, 8). */
	return tx(g, &req, ALIGN8(8 + nlen), NULL, 0, 1000);
}

static int
send_remote_ack(struct glink *g)
{
	g->remote_ack_sent = 1;
	return send_cmd(g, CMD_OPEN_ACK, g->rcid, 0);
}

static int
rx_one(struct glink *g, uint32_t avail, int *progress)
{
	uint32_t m[4], cmd, p1, p2, extra, chunk, left;
	uint8_t tmp[264];
	char name[NAME_MAX_LEN + 8];
	int r = GLINK_OK;

	*progress = 0;
	if (avail < 8)
		return GLINK_OK;
	rx_peek(g, m, 0, 8);
	cmd = m[0] & 0xffff;
	p1 = m[0] >> 16;
	p2 = m[1];

	switch (cmd) {
	case CMD_VERSION:
		/* qcom_glink_receive_version() */
		if (p1 == 0)
			break;
		if (p1 == GLINK_VERSION_1)
			g->features &= p2;
		rx_advance(g, 8);
		*progress = 1;
		r = send_cmd(g, CMD_VERSION_ACK, GLINK_VERSION_1, g->features);
		g->version_ok = 1;
		return r;
	case CMD_VERSION_ACK:
		/* qcom_glink_receive_version_ack() */
		rx_advance(g, 8);
		*progress = 1;
		if (p1 == 0)
			return EPROTO;		/* negotiation failed */
		if (p1 == GLINK_VERSION_1 && p2 == g->features) {
			g->version_ok = 1;
			return GLINK_OK;
		}
		if (p1 == GLINK_VERSION_1)
			g->features &= p2;
		return send_cmd(g, CMD_VERSION, GLINK_VERSION_1, g->features);
	case CMD_OPEN:
		/* deferred with param2 & 0xffff aligned to 8 as extra */
		extra = ALIGN8(p2 & 0xffff);
		if (extra > NAME_MAX_LEN + 8)
			return EPROTO;
		if (avail < 8 + extra)
			return GLINK_OK;	/* the name is not all in yet */
		memset(name, 0, sizeof(name));
		rx_peek(g, name, 8, extra);
		name[sizeof(name) - 1] = '\0';
		rx_advance(g, 8 + extra);
		*progress = 1;
		if (strcmp(name, chan_name) != 0) {
			g->stray++;
			return GLINK_OK;
		}
		g->rcid = p1;
		g->remote_ack_sent = 0;
		if (!g->open_sent) {
			/* qcom_glink_create_remote(): ack theirs, open ours */
			if ((r = send_remote_ack(g)) != GLINK_OK)
				return r;
			return send_open(g);
		}
		if (g->open_acked)	/* _create_local(): after our ack */
			return send_remote_ack(g);
		return GLINK_OK;
	case CMD_OPEN_ACK:
		rx_advance(g, 8);
		*progress = 1;
		if (g->open_sent && p1 == g->lcid) {
			g->open_acked = 1;
			if (g->rcid != 0 && !g->remote_ack_sent)
				return send_remote_ack(g);
		}
		return GLINK_OK;
	case CMD_CLOSE:
		rx_advance(g, 8);
		*progress = 1;
		if (g->rcid != 0 && p1 == g->rcid) {
			/* qcom_glink_rx_close(): the remote closed ours */
			r = send_cmd(g, CMD_CLOSE_ACK, g->rcid, 0);
			g->rcid = 0;
			g->remote_ack_sent = 0;
			return r;
		}
		g->closes++;		/* Linux: WARN, no ack */
		return GLINK_OK;
	case CMD_CLOSE_ACK:
	case CMD_RX_DONE:
	case CMD_RX_DONE_W_REUSE:
	case CMD_RX_INTENT_REQ:
	case CMD_RX_INTENT_REQ_ACK:
	case CMD_SIGNALS:
		rx_advance(g, 8);
		*progress = 1;
		return GLINK_OK;
	case CMD_READ_NOTIF:
		rx_advance(g, 8);
		*progress = 1;
		g->kick(g->ctx);
		return GLINK_OK;
	case CMD_INTENT:
		extra = 8 + 8 * p2;
		if (p2 > 64)
			return EPROTO;
		if (avail < extra)
			return GLINK_OK;
		rx_advance(g, ALIGN8(extra));
		*progress = 1;
		return GLINK_OK;
	case CMD_TX_DATA:
	case CMD_TX_DATA_CONT:
		if (avail < 16)
			return GLINK_OK;
		rx_peek(g, m, 0, 16);
		chunk = m[2];
		left = m[3];
		if (chunk > g->rx_len || chunk > sizeof(tmp) - 8)
			return EPROTO;
		if (avail < 16 + chunk)
			return GLINK_OK;
		if (g->rcid != 0 && p1 == g->rcid) {
			if (cmd == CMD_TX_DATA)
				g->reply_len = 0;
			rx_peek(g, tmp, 16, ALIGN4(chunk));
			if (g->reply_len + chunk <= sizeof(g->reply)) {
				memcpy(g->reply + g->reply_len, tmp, chunk);
				g->reply_len += chunk;
			}
			if (left == 0)
				g->reply_done = 1;
		} else
			g->stray++;
		rx_advance(g, ALIGN8(16 + chunk));
		*progress = 1;
		return GLINK_OK;
	default:
		return EPROTO;
	}
	/* VERSION 0: nothing to answer. */
	rx_advance(g, 8);
	*progress = 1;
	return GLINK_OK;
}

static int
rx_all(struct glink *g)
{
	uint32_t avail;
	int r, progress;

	for (;;) {
		if ((r = rx_avail(g, &avail)) != GLINK_OK)
			return r;
		if ((r = rx_one(g, avail, &progress)) != GLINK_OK)
			return r;
		if (!progress)
			return GLINK_OK;
	}
}

/*===========================================================================*
 *    the channel and the request                                             *
 *===========================================================================*/
int
glink_open(struct glink *g, const char *name, unsigned timeout_ms)
{
	unsigned t, since_version = 0;
	int r;

	if (strlen(name) + 1 > NAME_MAX_LEN)
		return EINVAL;
	strcpy(chan_name, name);
	g->features = 0;
	g->lcid = 1;		/* idr_alloc_cyclic() from RPM_GLINK_CID_MIN */
	g->rcid = 0;
	g->version_sent = g->version_ok = 0;
	g->open_sent = g->open_acked = g->remote_ack_sent = 0;

	/* qcom_glink_native_probe(): the first word is ours. */
	if ((r = send_cmd(g, CMD_VERSION, GLINK_VERSION_1, g->features)) !=
	    GLINK_OK)
		return r;
	g->version_sent = 1;

	for (t = 0; t < timeout_ms * 1000; t += POLL_US) {
		if ((r = rx_all(g)) != GLINK_OK)
			return r;
		if (g->open_acked && g->rcid != 0 && g->remote_ack_sent)
			return GLINK_OK;
		/*
		 * The RPM opens "rpm_requests" itself once the versions agree;
		 * if it has not after a second, open it from here the way
		 * qcom_glink_create_local() does.
		 */
		if (g->version_ok && !g->open_sent) {
			since_version += POLL_US;
			if (since_version >= OPEN_LOCAL_AFTER_MS * 1000 &&
			    (r = send_open(g)) != GLINK_OK)
				return r;
		}
		g->delay_us(g->ctx, POLL_US);
	}
	return ETIMEDOUT;
}

int
glink_rpm_request(struct glink *g, uint32_t type, uint32_t id,
	const uint32_t *keys, const uint32_t *values, unsigned nkv,
	unsigned timeout_ms)
{
	static uint32_t msg_id = 1;
	uint32_t hdr[4], pkt[7 + 3 * 8], size, off, mtype, mlen, len;
	unsigned i, t;
	int r;

	if (nkv == 0 || nkv > 8 || g->rcid == 0)
		return EINVAL;
	pkt[0] = RPM_SERVICE_REQUEST;
	pkt[1] = 20 + 12 * nkv;		/* request + payload */
	pkt[2] = msg_id++;
	pkt[3] = 0;			/* QCOM_SMD_RPM_ACTIVE_STATE */
	pkt[4] = type;
	pkt[5] = id;
	pkt[6] = 12 * nkv;
	for (i = 0; i < nkv; i++) {
		pkt[7 + 3 * i] = keys[i];
		pkt[8 + 3 * i] = 4;
		pkt[9 + 3 * i] = values[i];
	}
	size = 28 + 12 * nkv;

	hdr[0] = CMD_TX_DATA | (uint32_t)g->lcid << 16;
	hdr[1] = 0;			/* intentless: no intent id */
	hdr[2] = size;			/* chunk */
	hdr[3] = 0;			/* left */
	g->reply_done = 0;
	g->reply_len = 0;
	g->rpm_err[0] = '\0';
	if ((r = tx(g, hdr, sizeof(hdr), pkt, size, timeout_ms)) != GLINK_OK)
		return r;

	for (t = 0; !g->reply_done; t += POLL_US) {
		if (t >= timeout_ms * 1000)
			return ETIMEDOUT;
		if ((r = rx_all(g)) != GLINK_OK)
			return r;
		if (!g->reply_done)
			g->delay_us(g->ctx, POLL_US);
	}

	/* qcom_smd_rpm_callback() */
	if (g->reply_len < 8)
		return EPROTO;
	memcpy(&mtype, g->reply, 4);
	memcpy(&len, g->reply + 4, 4);
	if (mtype != RPM_SERVICE_REQUEST || len < 8 || 8 + len > g->reply_len)
		return EPROTO;
	r = GLINK_OK;
	for (off = 8; off + 8 <= 8 + len; off += 8 + ALIGN4(mlen)) {
		memcpy(&mtype, g->reply + off, 4);
		memcpy(&mlen, g->reply + off + 4, 4);
		if (mlen > len)
			return EPROTO;
		if (mtype == RPM_MSG_ERR) {
			size_t n = mlen < sizeof(g->rpm_err) - 1 ? mlen :
			    sizeof(g->rpm_err) - 1;

			if (off + 8 + n > g->reply_len)
				n = g->reply_len - off - 8;
			memcpy(g->rpm_err, g->reply + off + 8, n);
			g->rpm_err[n] = '\0';
			r = strcmp(g->rpm_err, "resource does not exist") == 0 ?
			    ENXIO : EINVAL;
		}
	}
	return r;
}
