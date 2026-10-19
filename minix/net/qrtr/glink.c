/*
 * GLINK to the modem over SMEM, and its one channel this side opens:
 * IPCRTR, the router's.  Facts from Linux's qcom_glink_smem.c and
 * qcom_glink_native.c (port/clover/hw-modem.md, section 5), not their code.
 *
 * The pipes: SMEM item 478 holds four words - our FIFO's tail and head,
 * the modem's FIFO's tail and head; item 479 (16 KiB) is our FIFO, which
 * this side allocates, item 480 the modem's.  We write our head and the
 * modem's tail; a command is an 8-byte header {cmd (16), param1 (16),
 * param2 (32)} and its data, the head kept on 8-byte boundaries; a write
 * is followed by the mailbox bit of the edge.
 *
 * The edge with the modem is not intentless: data goes only into a buffer
 * ("intent") the receiver has advertised.  We advertise ours when the
 * channel opens and on the modem's RX_INTENT_REQ, take the modem's as it
 * advertises them, and ask for one (RX_INTENT_REQ) when none of its is
 * large enough; a packet without an intent waits in a queue.  Done with
 * an intent, the receiver says RX_DONE, or RX_DONE_W_REUSE to keep it.
 *
 * Polled: gl_poll() runs on every tick and after every send.
 */

#include "qrtr.h"

#include <minix/fdt.h>
#include <minix/vm.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#define ITEM_DESC		478
#define ITEM_FIFO_TX		479
#define ITEM_FIFO_RX		480
#define FIFO_TX_SIZE		16384
#define FIFO_RESERVE		16	/* full + a READ_NOTIF */

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

#define GLINK_VERSION		1
#define FEATURE_INTENT_REUSE	0x1

#define CHANNEL			"IPCRTR"
#define LCID			1
#define CHUNK_MAX		8192

#define N_LINTENT		32
#define N_RINTENT		64
#define BASE_INTENTS		8
#define BASE_INTENT_SIZE	4096

#define ALIGN8(x)		(((x) + 7) & ~(size_t)7)

static struct smem smem;
static volatile uint8_t *mbox;
static unsigned mbox_off, mbox_bit;

/* The pipes, as offsets into SMEM. */
static size_t desc, tx_fifo, rx_fifo;
static uint32_t tx_len, rx_len;

/* The link and the channel. */
static uint32_t features = FEATURE_INTENT_REUSE;
static int version_ok;
static uint32_t rcid;			/* the modem's id of IPCRTR; 0: none */
static int open_sent, open_acked, remote_open, ch_up, intents_sent;
static int read_notif_sent;

static struct lintent {			/* ours: the modem writes into them */
	uint32_t id, size, off;
	int reuse, used;
	uint8_t *buf;
} lint[N_LINTENT];
static uint32_t next_liid = 1;

static struct rintent {			/* the modem's: we write into them */
	uint32_t id, size;
	int used, in_use;
} rint[N_RINTENT];
static int intent_req_out;

/* Packets waiting for an intent or for room in the FIFO. */
struct txq {
	struct txq *next;
	size_t len;
	uint8_t data[];
};
static struct txq *txq_head, *txq_tail;

/*===========================================================================*
 *				pipes					     *
 *===========================================================================*/
#define TX_TAIL		(desc + 0)
#define TX_HEAD		(desc + 4)
#define RX_TAIL		(desc + 8)
#define RX_HEAD		(desc + 12)

static void
kick(void)
{
	__sync_synchronize();
	*(volatile uint32_t *)(mbox + mbox_off) = 1u << mbox_bit;
}

static uint32_t
rx_avail(void)
{
	uint32_t head, tail;

	if (rx_fifo == 0) {
		size_t sz;

		if ((rx_fifo = smem_get(&smem, 1, ITEM_FIFO_RX, &sz)) == 0)
			return 0;
		rx_len = (uint32_t)sz;
		log_info(&qlog, "the modem's FIFO is there: %u bytes\n",
		    rx_len);
	}
	head = smem_rd32(&smem, RX_HEAD);
	tail = smem_rd32(&smem, RX_TAIL);
	if (head >= rx_len || tail >= rx_len)
		return 0;
	return head < tail ? rx_len - tail + head : head - tail;
}

static void
rx_peek(void *buf, uint32_t off, size_t n)
{
	uint32_t tail = smem_rd32(&smem, RX_TAIL) + off;
	size_t first;

	if (tail >= rx_len)
		tail -= rx_len;
	first = n < rx_len - tail ? n : rx_len - tail;
	smem_read(&smem, rx_fifo + tail, buf, first);
	if (first < n)
		smem_read(&smem, rx_fifo, (uint8_t *)buf + first, n - first);
}

static void
rx_advance(size_t n)
{
	uint32_t tail = smem_rd32(&smem, RX_TAIL) + (uint32_t)n;

	if (tail >= rx_len)
		tail -= rx_len;
	smem_wr32(&smem, RX_TAIL, tail);
}

static uint32_t
tx_avail(void)
{
	uint32_t head = smem_rd32(&smem, TX_HEAD);
	uint32_t tail = smem_rd32(&smem, TX_TAIL), avail;

	avail = tail <= head ? tx_len - head + tail : tail - head;
	return avail < FIFO_RESERVE ? 0 : avail - FIFO_RESERVE;
}

static uint32_t
tx_write_one(uint32_t head, const void *data, size_t n)
{
	size_t first = n < tx_len - head ? n : tx_len - head;

	if (first)
		smem_write(&smem, tx_fifo + head, data, first);
	if (first < n)
		smem_write(&smem, tx_fifo, (const uint8_t *)data + first,
		    n - first);
	head += (uint32_t)n;
	if (head >= tx_len)
		head -= tx_len;
	return head;
}

/* One command: header and data, or nothing if there is no room (0). */
static int
tx(const void *hdr, size_t hlen, const void *data, size_t dlen)
{
	uint32_t head;
	struct { uint16_t cmd, p1; uint32_t p2; } rn = { CMD_READ_NOTIF, 0, 0 };

	if (hlen + dlen >= tx_len)
		return -1;
	if (tx_avail() < hlen + dlen) {
		if (!read_notif_sent && tx_avail() >= 8) {
			head = tx_write_one(smem_rd32(&smem, TX_HEAD), &rn, 8);
			__sync_synchronize();
			smem_wr32(&smem, TX_HEAD, head);
			kick();
			read_notif_sent = 1;
		}
		return 0;
	}
	read_notif_sent = 0;
	head = smem_rd32(&smem, TX_HEAD);
	head = tx_write_one(head, hdr, hlen);
	if (dlen)
		head = tx_write_one(head, data, dlen);
	head = (uint32_t)ALIGN8(head);
	if (head >= tx_len)
		head -= tx_len;
	__sync_synchronize();
	smem_wr32(&smem, TX_HEAD, head);
	kick();
	return 1;
}

static void
tx_cmd(uint16_t cmd, uint16_t p1, uint32_t p2)
{
	struct { uint16_t cmd, p1; uint32_t p2; } m = { cmd, p1, p2 };
	unsigned t;

	/* Control commands are small; wait a little for room rather than
	 * lose one. */
	for (t = 0; t < 100 && tx(&m, sizeof(m), NULL, 0) == 0; t++)
		micro_delay(100);
}

/*===========================================================================*
 *				intents					     *
 *===========================================================================*/
static struct lintent *
lintent_new(uint32_t size, int reuse)
{
	unsigned i;

	for (i = 0; i < N_LINTENT; i++)
		if (!lint[i].used)
			break;
	if (i == N_LINTENT || (lint[i].buf = malloc(size)) == NULL)
		return NULL;
	lint[i].used = 1;
	lint[i].id = next_liid++;
	lint[i].size = size;
	lint[i].off = 0;
	lint[i].reuse = reuse;
	return &lint[i];
}

static void
advertise(const struct lintent *li)
{
	struct { uint16_t cmd, lcid; uint32_t count, size, liid; } __packed m;
	unsigned t;

	m.cmd = CMD_INTENT;
	m.lcid = LCID;
	m.count = 1;
	m.size = li->size;
	m.liid = li->id;
	for (t = 0; t < 100 && tx(&m, sizeof(m), NULL, 0) == 0; t++)
		micro_delay(100);
}

static struct lintent *
lintent_find(uint32_t id)
{
	unsigned i;

	for (i = 0; i < N_LINTENT; i++)
		if (lint[i].used && lint[i].id == id)
			return &lint[i];
	return NULL;
}

/*===========================================================================*
 *				the channel				     *
 *===========================================================================*/
static void
send_open(void)
{
	struct { uint16_t cmd, lcid; uint32_t len; char name[8]; } m;
	unsigned t;

	memset(&m, 0, sizeof(m));
	m.cmd = CMD_OPEN;
	m.lcid = LCID;
	m.len = sizeof(CHANNEL);
	memcpy(m.name, CHANNEL, sizeof(CHANNEL));
	for (t = 0; t < 100 && tx(&m, 8 + ALIGN8(sizeof(CHANNEL)), NULL, 0) ==
	    0; t++)
		micro_delay(100);
	open_sent = 1;
}

static void
channel_down(const char *why)
{
	struct txq *q;
	unsigned i;

	if (ch_up)
		log_warn(&qlog, "IPCRTR closed: %s\n", why);
	ch_up = open_sent = open_acked = remote_open = intents_sent = 0;
	rcid = 0;
	intent_req_out = 0;
	for (i = 0; i < N_LINTENT; i++)
		if (lint[i].used) {
			free(lint[i].buf);
			lint[i].used = 0;
		}
	memset(rint, 0, sizeof(rint));
	while ((q = txq_head) != NULL) {
		txq_head = q->next;
		free(q);
	}
	txq_tail = NULL;
	rt_link(0);
}

static void
channel_check_up(void)
{
	unsigned i;
	struct lintent *li;

	if (ch_up || !remote_open || !open_acked)
		return;
	ch_up = 1;
	if (!intents_sent) {
		for (i = 0; i < BASE_INTENTS; i++)
			if ((li = lintent_new(BASE_INTENT_SIZE, 1)) != NULL)
				advertise(li);
		intents_sent = 1;
	}
	log_info(&qlog, "IPCRTR open (lcid %u, rcid %u), %u intents of %u "
	    "bytes offered\n", LCID, rcid, BASE_INTENTS, BASE_INTENT_SIZE);
	rt_link(1);
}

/*===========================================================================*
 *				sending					     *
 *===========================================================================*/
/* One packet on the channel, into the smallest free intent that holds it:
 * 1 sent, 0 not now (no intent, or no room), -1 never. */
static int
send_one(const uint8_t *data, size_t len)
{
	struct { uint16_t cmd, lcid; uint32_t iid, chunk, left; } __packed h;
	struct rintent *ri = NULL;
	size_t off, n;
	unsigned i;

	for (i = 0; i < N_RINTENT; i++)
		if (rint[i].used && !rint[i].in_use && rint[i].size >= len &&
		    (ri == NULL || rint[i].size < ri->size))
			ri = &rint[i];
	if (ri == NULL) {
		if (!intent_req_out) {
			struct { uint16_t cmd, lcid; uint32_t size; } r =
			    { CMD_RX_INTENT_REQ, LCID, (uint32_t)len };
			if (tx(&r, sizeof(r), NULL, 0) == 1)
				intent_req_out = 1;
		}
		return 0;
	}
	if (tx_avail() < sizeof(h) + len + 8 * (len / CHUNK_MAX + 1))
		return 0;
	ri->in_use = 1;
	for (off = 0; off < len; off += n) {
		n = len - off > CHUNK_MAX ? CHUNK_MAX : len - off;
		h.cmd = off == 0 ? CMD_TX_DATA : CMD_TX_DATA_CONT;
		h.lcid = LCID;
		h.iid = ri->id;
		h.chunk = (uint32_t)n;
		h.left = (uint32_t)(len - off - n);
		if (tx(&h, sizeof(h), data + off, n) != 1) {
			log_warn(&qlog, "FIFO full in the middle of a packet\n");
			return -1;
		}
	}
	return 1;
}

static void
flush(void)
{
	struct txq *q;
	int r;

	while ((q = txq_head) != NULL && ch_up) {
		if ((r = send_one(q->data, q->len)) == 0)
			break;
		txq_head = q->next;
		if (txq_head == NULL)
			txq_tail = NULL;
		free(q);
	}
}

int
gl_send(const void *hdr, size_t hlen, const void *data, size_t dlen)
{
	struct txq *q;

	if (!ch_up)
		return ENETDOWN;
	if ((q = malloc(sizeof(*q) + hlen + dlen)) == NULL)
		return ENOMEM;
	q->next = NULL;
	q->len = hlen + dlen;
	memcpy(q->data, hdr, hlen);
	if (dlen)
		memcpy(q->data + hlen, data, dlen);
	if (txq_tail != NULL)
		txq_tail->next = q;
	else
		txq_head = q;
	txq_tail = q;
	flush();
	return OK;
}

int
gl_up(void)
{
	return ch_up;
}

/*===========================================================================*
 *				receiving				     *
 *===========================================================================*/
/* One command from the modem: 1 handled (and the FIFO advanced), 0 not
 * all of it there yet. */
static int
rx_one(uint32_t avail)
{
	struct { uint16_t cmd, p1; uint32_t p2; } m;
	struct { uint16_t cmd, p1; uint32_t p2, chunk, left; } __packed d;
	struct lintent *li;
	char name[33];
	uint32_t n, i, k, pair[2];

	rx_peek(&m, 0, 8);
	switch (m.cmd) {
	case CMD_VERSION:
		rx_advance(8);
		if (m.p1 == GLINK_VERSION)
			features &= m.p2;
		if (m.p1 != 0)
			tx_cmd(CMD_VERSION_ACK, GLINK_VERSION, features);
		version_ok = 1;
		log_info(&qlog, "GLINK version %u from the modem, features "
		    "%x\n", m.p1, m.p2);
		return 1;
	case CMD_VERSION_ACK:
		rx_advance(8);
		if (m.p1 == GLINK_VERSION && m.p2 != features) {
			features &= m.p2;
			tx_cmd(CMD_VERSION, GLINK_VERSION, features);
		}
		version_ok = 1;
		return 1;
	case CMD_OPEN:
		n = m.p2 & 0xffff;
		if (avail < 8 + ALIGN8(n))
			return 0;
		memset(name, 0, sizeof(name));
		rx_peek(name, 8, n < 32 ? n : 32);
		rx_advance(8 + ALIGN8(n));
		if (strcmp(name, CHANNEL) != 0) {
			log_debug(&qlog, "the modem opens \"%s\" (rcid %u); "
			    "not ours\n", name, m.p1);
			return 1;
		}
		rcid = m.p1;
		remote_open = 1;
		tx_cmd(CMD_OPEN_ACK, (uint16_t)rcid, 0);
		if (!open_sent)
			send_open();
		channel_check_up();
		return 1;
	case CMD_OPEN_ACK:
		rx_advance(8);
		if (m.p1 == LCID) {
			open_acked = 1;
			channel_check_up();
		}
		return 1;
	case CMD_CLOSE:
		rx_advance(8);
		tx_cmd(CMD_CLOSE_ACK, m.p1, 0);
		if (m.p1 == rcid && rcid != 0)
			channel_down("the modem closed it");
		return 1;
	case CMD_CLOSE_ACK:
	case CMD_SIGNALS:
	case CMD_RX_INTENT_REQ_ACK:
		rx_advance(8);
		if (m.cmd == CMD_RX_INTENT_REQ_ACK && m.p2 == 0) {
			log_warn(&qlog, "the modem refused an intent\n");
			intent_req_out = 0;
		}
		return 1;
	case CMD_READ_NOTIF:
		rx_advance(8);
		kick();
		return 1;
	case CMD_INTENT:
		n = 8 + 8 * m.p2;
		if (avail < n)
			return 0;
		for (i = 0; i < m.p2; i++) {
			rx_peek(pair, 8 + 8 * i, 8);
			for (k = 0; k < N_RINTENT && rint[k].used; k++)
				;
			if (k == N_RINTENT || m.p1 != rcid)
				continue;
			rint[k].used = 1;
			rint[k].in_use = 0;
			rint[k].size = pair[0];
			rint[k].id = pair[1];
		}
		rx_advance(ALIGN8(n));
		intent_req_out = 0;
		return 1;
	case CMD_RX_DONE:
	case CMD_RX_DONE_W_REUSE:
		rx_advance(8);
		for (k = 0; k < N_RINTENT; k++)
			if (rint[k].used && rint[k].id == m.p2) {
				rint[k].in_use = 0;
				if (m.cmd == CMD_RX_DONE)
					rint[k].used = 0;
				break;
			}
		return 1;
	case CMD_RX_INTENT_REQ:
		rx_advance(8);
		li = lintent_new(m.p2, 0);
		if (li != NULL)
			advertise(li);
		tx_cmd(CMD_RX_INTENT_REQ_ACK, LCID, li != NULL);
		return 1;
	case CMD_TX_DATA:
	case CMD_TX_DATA_CONT:
		if (avail < sizeof(d))
			return 0;
		rx_peek(&d, 0, sizeof(d));
		if (avail < sizeof(d) + d.chunk)
			return 0;
		li = (d.p1 == rcid) ? lintent_find(d.p2) : NULL;
		if (li == NULL || li->size - li->off < d.chunk) {
			log_warn(&qlog, "data for %s intent %u (%u bytes): "
			    "dropped\n", li == NULL ? "an unknown" : "a short",
			    d.p2, d.chunk);
		} else {
			rx_peek(li->buf + li->off, sizeof(d), d.chunk);
			li->off += d.chunk;
			if (d.left == 0) {
				rx_advance(ALIGN8(sizeof(d) + d.chunk));
				rt_from_remote(li->buf, li->off);
				li->off = 0;
				tx_cmd(li->reuse ? CMD_RX_DONE_W_REUSE :
				    CMD_RX_DONE, LCID, li->id);
				if (!li->reuse) {
					free(li->buf);
					li->used = 0;
				}
				return 1;
			}
		}
		rx_advance(ALIGN8(sizeof(d) + d.chunk));
		return 1;
	default:
		log_warn(&qlog, "unknown GLINK command %u; skipping 8 bytes\n",
		    m.cmd);
		rx_advance(8);
		return 1;
	}
}

void
gl_poll(void)
{
	uint32_t avail;
	unsigned n;

	if (desc == 0)
		return;
	for (n = 0; n < 1000; n++) {
		if ((avail = rx_avail()) < 8 || rx_one(avail) == 0)
			break;
	}
	flush();
}

/*===========================================================================*
 *				gl_init					     *
 *===========================================================================*/
static struct {
	u64_t smem_pa, smem_sz, lock_pa, lock_sz, mbox_pa, mbox_sz;
	uint32_t smem_ph, lock_ph, lock_id, mbox_ph, mbox_bit;
	int found_smem, found_edge;
} dt;

static uint32_t
prop_u32(const struct fdt_node *n, const char *name, unsigned i, uint32_t def)
{
	const uint8_t *p;
	unsigned len;

	if ((p = fdt_getprop(n, name, &len)) == NULL || len < 4 * (i + 1))
		return def;
	return (uint32_t)fdt_read_cells(p + 4 * i, 1);
}

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	(void)cookie; (void)depth;
	if (fdt_node_is_compatible(n, "qcom,smem")) {
		dt.found_smem = 1;
		dt.smem_ph = prop_u32(n, "memory-region", 0, 0);
		dt.lock_ph = prop_u32(n, "hwlocks", 0, 0);
		dt.lock_id = prop_u32(n, "hwlocks", 1, 0);
	} else if (strncmp(name, "glink-edge", 10) == 0 &&
	    prop_u32(n, "qcom,remote-pid", 0, ~0u) == 1) {
		dt.found_edge = 1;
		dt.mbox_ph = prop_u32(n, "mboxes", 0, 0);
		dt.mbox_bit = prop_u32(n, "mboxes", 1, ~0u);
	}
	return 0;
}

static void
delay_us(unsigned us)
{
	micro_delay(us);
}

int
gl_init(void)
{
	volatile uint8_t *base, *lock;
	void *dtb;
	size_t sz;
	int r;

	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	(void)fdt_walk(dtb, find_cb, NULL);
	if (!dt.found_smem || !dt.found_edge) {
		free(dtb);
		log_info(&qlog, "no %s in the tree; QRTR without a modem\n",
		    !dt.found_smem ? "SMEM" : "GLINK edge to the modem");
		return ENXIO;
	}
	r = fdt_phandle_reg(dtb, dt.smem_ph, 0, &dt.smem_pa, &dt.smem_sz) |
	    fdt_phandle_reg(dtb, dt.lock_ph, 0, &dt.lock_pa, &dt.lock_sz) |
	    fdt_phandle_reg(dtb, dt.mbox_ph, 0, &dt.mbox_pa, &dt.mbox_sz);
	free(dtb);
	if (r != 0 || dt.mbox_bit > 31 || dt.mbox_sz < 0x10 ||
	    dt.lock_sz < (dt.lock_id + 1) * 0x1000u) {
		log_warn(&qlog, "the modem's GLINK edge is not one this reads\n");
		return ENXIO;
	}
	/* The APCS IPC register of the SDM660's "hmss-global" block. */
	mbox_off = 8;
	mbox_bit = dt.mbox_bit;

	if ((base = vm_map_phys(SELF, (void *)(phys_bytes)dt.smem_pa,
	    (size_t)dt.smem_sz)) == MAP_FAILED ||
	    (lock = vm_map_phys(SELF, (void *)(phys_bytes)dt.lock_pa,
	    (size_t)dt.lock_sz)) == MAP_FAILED ||
	    (mbox = vm_map_phys(SELF, (void *)(phys_bytes)dt.mbox_pa,
	    (size_t)dt.mbox_sz)) == MAP_FAILED) {
		log_warn(&qlog, "cannot map SMEM, its lock or the mailbox; "
		    "not granted by RS?\n");
		return EPERM;
	}
	memset(&smem, 0, sizeof(smem));
	smem.base = base;
	smem.size = (size_t)dt.smem_sz;
	smem.lock = (volatile uint32_t *)(lock + dt.lock_id * 0x1000);
	smem.delay_us = delay_us;
	if (smem_init(&smem) != 0) {
		log_warn(&qlog, "SMEM: %s\n", smem.why);
		return EIO;
	}

	if (smem_alloc(&smem, 1, ITEM_DESC, 32) < 0 ||
	    (desc = smem_get(&smem, 1, ITEM_DESC, &sz)) == 0 || sz != 32 ||
	    smem_alloc(&smem, 1, ITEM_FIFO_TX, FIFO_TX_SIZE) < 0 ||
	    (tx_fifo = smem_get(&smem, 1, ITEM_FIFO_TX, &sz)) == 0) {
		log_warn(&qlog, "GLINK's SMEM items: %s\n",
		    smem.why ? smem.why : "not there after allocating");
		desc = 0;
		return EIO;
	}
	tx_len = (uint32_t)sz;
	smem_wr32(&smem, RX_TAIL, 0);
	smem_wr32(&smem, TX_HEAD, 0);
	log_info(&qlog, "GLINK to the modem: SMEM %u, our FIFO %u bytes, "
	    "mailbox bit %u\n", smem.version, tx_len, mbox_bit);
	tx_cmd(CMD_VERSION, GLINK_VERSION, features);
	return OK;
}
