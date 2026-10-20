/*
 * The WCN3990's data path in the wcn service: HTT set up after WMI's
 * READY, frames received into the host rx ring and handed to lwip (or,
 * EAPOL, to wcnd), frames from lwip sent as native wifi (port/clover/
 * hw-wifi.md, section 5; the formats are wcn_htt.c's).
 *
 * Memory: everything the firmware reads or writes is in the service's DMA
 * arena, at the address the engines see (the SMMU's IOVA).  The rx ring's
 * entries and its index word are written by the host only; the buffers by
 * the firmware only - flushed before they are posted, invalidated before
 * they are read.
 */

#include "wcn.h"
#include "wcn_htt.h"
#include "wcn_ioc.h"

#include <minix/cachectl.h>
#include <minix/netdriver.h>

#include <stdlib.h>
#include <string.h>

#define RXBUFS		1023		/* posted (Linux: 2047; 2 MB here) */
#define TXSLOTS		256		/* frames in flight, msdu ids 0..255 */
#define QLEN		64		/* frames waiting for lwip / wcnd */

static struct htt_ring ring;
static uint8_t *ring_va;		/* HTT_RX_RING_SIZE le64 */
static uint64_t ring_da;
static volatile uint32_t *idx_va;	/* the index word, a line of its own */
static uint64_t idx_da;
static uint8_t *rx_va;
static uint64_t rx_da;
static uint8_t *frag_va;
static uint64_t frag_da;
static uint8_t *tx_va;
static uint64_t tx_da;
static int tx_busy[TXSLOTS];
static unsigned tx_next;

static int htt_up, ver_major = -1, ver_minor = -1;

/* The link: what wcnd joined. */
static struct wcn_link lnk;

/* Received frames, Ethernet, for lwip and for wcnd (EAPOL). */
struct fq {
	uint8_t data[QLEN][1600];
	uint16_t len[QLEN];
	unsigned head, tail;
};
static struct fq *netq, *eapq;

unsigned long rx_frames, rx_dropped, tx_frames, tx_dropped;

static int
fq_put(struct fq *q, const uint8_t *p, size_t len)
{
	unsigned t;

	if (q == NULL || q->tail - q->head >= QLEN || len > sizeof(q->data[0]))
		return -1;
	t = q->tail++ % QLEN;
	memcpy(q->data[t], p, len);
	q->len[t] = len;
	return 0;
}

static ssize_t
fq_get(struct fq *q, uint8_t **p)
{
	unsigned h;

	if (q == NULL || q->head == q->tail)
		return -1;
	h = q->head % QLEN;
	*p = q->data[h];
	return q->len[h];
}

static void
fq_pop(struct fq *q)
{

	q->head++;
}

/* ---- the rx ring ---- */

static void
publish_index(void)
{

	*idx_va = htt_ring_index(&ring);
	(void)sys_cachectl(CACHE_CLEAN, (void *)(uintptr_t)idx_va, 4);
}

static void
post(uint32_t tag)
{
	uint8_t *b = rx_va + (size_t)tag * HTT_RX_BUF_SIZE;
	unsigned before = ring.idx;

	/* No dirty line of the buffer may land on what the firmware puts. */
	(void)sys_cachectl(CACHE_CLEAN_INVALIDATE, b, HTT_RX_BUF_SIZE);
	if (htt_ring_post(&ring, ring_va, rx_da + (uint64_t)tag *
	    HTT_RX_BUF_SIZE, tag) != 0)
		return;
	(void)sys_cachectl(CACHE_CLEAN, ring_va + 8 * before, 8);
}

/* ---- receive ---- */

static void
rx_frame(uint8_t *p, size_t len)
{
	unsigned type;

	if (len < 14) {
		rx_dropped++;
		return;
	}
	type = p[12] << 8 | p[13];
	rx_frames++;
	if (type == 0x888e) {
		if (fq_put(eapq, p, len) != 0)
			rx_dropped++;
		return;
	}
	if (!lnk.up || fq_put(netq, p, len) != 0) {
		rx_dropped++;
		return;
	}
	netdriver_recv();
}

static void
in_ord(const uint8_t *m, size_t len)
{
	struct htt_in_ord io;
	struct htt_rxd d;
	uint64_t pa;
	unsigned i, blen, fwd;
	uint32_t tag;
	size_t off, plen, rest;
	uint8_t *b;
	long e;

	if (htt_in_ord(m, len, &io) != 0) {
		rx_dropped++;
		return;
	}
	for (i = 0; i < io.count; i++) {
		htt_in_ord_msdu(&io, i, &pa, &blen, &fwd);
		if (htt_ring_take(&ring, pa, &tag) != 0) {
			log_warn(&wlog, "rx: the firmware names %llx, not "
			    "ours\n", (unsigned long long)pa);
			continue;
		}
		b = rx_va + (size_t)tag * HTT_RX_BUF_SIZE;
		(void)sys_cachectl(CACHE_INVALIDATE, b, HTT_RX_BUF_SIZE);
		if (io.offload || htt_rxd_parse(b, HTT_RX_BUF_SIZE, &d) != 0 ||
		    htt_rxd_bad(&d) || htt_rxd_payload(&d, HTT_RX_BUF_SIZE,
		    blen, &off, &plen, &rest) != 0 || rest != 0) {
			rx_dropped++;
		} else if (d.decap == HTT_DECAP_NWIFI) {
			if ((e = htt_nwifi_to_eth(b + off, plen)) < 0)
				rx_dropped++;
			else
				rx_frame(b + off + e, plen - (size_t)e);
		} else if (d.decap == HTT_DECAP_ETH2)
			rx_frame(b + off, plen);
		else
			rx_dropped++;
		post(tag);
	}
	publish_index();
}

static void
tx_done(const uint8_t *m, size_t len)
{
	struct htt_tx_compl c;
	unsigned i, id;

	if (htt_tx_compl(m, len, &c) != 0)
		return;
	for (i = 0; i < c.count; i++) {
		id = htt_tx_compl_id(&c, i);
		if (id < TXSLOTS)
			tx_busy[id] = 0;
	}
	netdriver_send();	/* room again */
}

/* Every HTT message the firmware sends comes here. */
void
data_htt(const uint8_t *m, size_t len)
{
	struct htt_peer_map pm;
	unsigned maj, min;

	switch (htt_t2h_type(m, len)) {
	case HTT_T2H_VERSION_CONF:
		if (htt_version_conf(m, len, &maj, &min) == 0) {
			ver_major = (int)maj;
			ver_minor = (int)min;
		}
		break;
	case HTT_T2H_RX_IN_ORD_PADDR:
		in_ord(m, len);
		break;
	case HTT_T2H_TX_COMPL_IND:
		tx_done(m, len);
		break;
	case HTT_T2H_PEER_MAP:
		if (htt_peer_map(m, len, &pm) == 0)
			log_info(&wlog, "peer %u: %02x:%02x:%02x:%02x:%02x:%02x "
			    "on vdev %u\n", pm.peer_id, pm.addr[0], pm.addr[1],
			    pm.addr[2], pm.addr[3], pm.addr[4], pm.addr[5],
			    pm.vdev);
		break;
	case HTT_T2H_RX_IND:
		/* Not the path this firmware takes (in-order receive); said
		 * once so that it is seen if it does. */
		{
			static int said;

			if (!said++)
				log_warn(&wlog, "rx: an RX_IND - not handled\n");
		}
		break;
	default:
		break;
	}
}

/* ---- setup ---- */

int
data_start(void)
{
	uint8_t msg[64];
	unsigned i, t;
	size_t n;
	void *va;
	int r;

	if (htt_up)
		return EBUSY;
	if (ring_va == NULL) {
		if (hw_take(HTT_RX_RING_SIZE * 8, &va, &ring_da) != 0 ||
		    (ring_va = va, hw_take(64, &va, &idx_da)) != 0 ||
		    (idx_va = va, hw_take((size_t)RXBUFS * HTT_RX_BUF_SIZE, &va,
		    &rx_da)) != 0 || (rx_va = va,
		    hw_take(HTT_MAX_PENDING_TX * HTT_FRAG_DESC_SIZE, &va,
		    &frag_da)) != 0 || (frag_va = va,
		    hw_take(TXSLOTS * 2048, &va, &tx_da)) != 0) {
			log_warn(&wlog, "HTT: no DMA memory\n");
			return ENOMEM;
		}
		tx_va = va;
		netq = calloc(1, sizeof(*netq));
		eapq = calloc(1, sizeof(*eapq));
		if (netq == NULL || eapq == NULL)
			return ENOMEM;
	}

	/* The version first: 3.56 expected. */
	n = htt_version_req(msg, sizeof(msg));
	if ((r = hw_send((unsigned)htt_ep, msg, n)) != OK)
		return r;
	for (t = 0; t < 1000 && ver_major < 0; t++) {
		hw_poll();
		micro_delay(1000);
	}
	if (ver_major < 0) {
		log_warn(&wlog, "HTT: no VERSION_CONF\n");
		return ETIMEDOUT;
	}
	log_info(&wlog, "HTT %d.%d\n", ver_major, ver_minor);

	/* The fragment descriptors, one per msdu id. */
	memset(frag_va, 0, HTT_MAX_PENDING_TX * HTT_FRAG_DESC_SIZE);
	(void)sys_cachectl(CACHE_CLEAN, frag_va,
	    HTT_MAX_PENDING_TX * HTT_FRAG_DESC_SIZE);
	n = htt_frag_desc_bank(msg, sizeof(msg), frag_da, HTT_MAX_PENDING_TX);
	if (n == 0 || (r = hw_send((unsigned)htt_ep, msg, n)) != OK)
		return n == 0 ? EINVAL : r;

	/* The rx ring, full, then its configuration. */
	if (htt_ring_init(&ring, HTT_RX_RING_SIZE, RXBUFS, 1) != 0)
		return EINVAL;
	memset(ring_va, 0, HTT_RX_RING_SIZE * 8);
	(void)sys_cachectl(CACHE_CLEAN, ring_va, HTT_RX_RING_SIZE * 8);
	for (i = 0; i < RXBUFS; i++)
		post(i);
	publish_index();
	n = htt_rx_ring_cfg(msg, sizeof(msg), idx_da, ring_da,
	    HTT_RX_RING_SIZE, htt_ring_index(&ring));
	if (n == 0 || (r = hw_send((unsigned)htt_ep, msg, n)) != OK)
		return n == 0 ? EINVAL : r;

	n = htt_aggr_cfg(msg, sizeof(msg), 64, 3);
	if (n == 0 || (r = hw_send((unsigned)htt_ep, msg, n)) != OK)
		return n == 0 ? EINVAL : r;
	htt_up = 1;
	log_info(&wlog, "HTT: %u rx buffers, %u tx slots\n", RXBUFS, TXSLOTS);
	return OK;
}

/* ---- send ---- */

/*
 * One Ethernet frame to the air: as native wifi to the joined BSS (to-DS),
 * from a tx slot, through its fragment descriptor.  EAPOL before the keys
 * goes unencrypted.  0, EAGAIN (no slot), EIO (no link), EINVAL.
 */
int
data_send(const uint8_t *eth, size_t len, int no_encrypt)
{
	struct htt_tx t;
	uint8_t msg[HTT_TX_FRM_LEN + HTT_TX_PREFETCH + 16], *f;
	unsigned k, type;
	size_t h, n;
	int r;

	if (!htt_up || !lnk.up)
		return EIO;
	if (len < 14 || len - 14 + HTT_NWIFI_HDR + HTT_SNAP_LEN > 2048)
		return EINVAL;
	for (k = 0; k < TXSLOTS && tx_busy[(tx_next + k) % TXSLOTS]; k++)
		;
	if (k == TXSLOTS) {
		hw_poll();
		return EAGAIN;
	}
	k = (tx_next + k) % TXSLOTS;
	tx_next = k + 1;
	f = tx_va + (size_t)k * 2048;
	type = eth[12] << 8 | eth[13];
	h = htt_nwifi_hdr(f, 2048, lnk.bssid, eth + 6, eth, type, 0);
	if (h == 0)
		return EINVAL;
	memcpy(f + h, eth + 14, len - 14);
	n = h + len - 14;
	(void)sys_cachectl(CACHE_CLEAN, f, n);
	if (htt_frag_desc(frag_va + (size_t)k * HTT_FRAG_DESC_SIZE,
	    HTT_FRAG_DESC_SIZE, tx_da + (uint64_t)k * 2048, n, 0) != 0)
		return EINVAL;
	(void)sys_cachectl(CACHE_CLEAN, frag_va + (size_t)k *
	    HTT_FRAG_DESC_SIZE, HTT_FRAG_DESC_SIZE);
	memset(&t, 0, sizeof(t));
	t.pkt = HTT_PKT_NWIFI;
	t.vdev = lnk.vdev;
	t.tid = (eth[0] & 1) ? HTT_TID_NONQOS : 0;
	t.id = k;
	t.len = n;
	t.frags_paddr = htt_frag_paddr(frag_da, k);
	t.freq = 0;
	t.no_encrypt = no_encrypt;
	if ((n = htt_tx_frm(msg, sizeof(msg), &t, f)) == 0)
		return EINVAL;
	tx_busy[k] = 1;
	if ((r = hw_send((unsigned)htt_ep, msg, n)) != OK) {
		tx_busy[k] = 0;
		return r;
	}
	tx_frames++;
	return OK;
}

/* ---- the link, lwip, wcnd ---- */

int
data_link(const struct wcn_link *l)
{

	lnk = *l;
	log_info(&wlog, "link %s: vdev %u, BSS %02x:%02x:%02x:%02x:%02x:%02x\n",
	    lnk.up ? "up" : "down", lnk.vdev, lnk.bssid[0], lnk.bssid[1],
	    lnk.bssid[2], lnk.bssid[3], lnk.bssid[4], lnk.bssid[5]);
	return OK;
}

int
data_link_up(void)
{

	return lnk.up && lnk.authorized;
}

ssize_t
data_net_recv(struct netdriver_data *data, size_t max)
{
	uint8_t *p;
	ssize_t n;

	if ((n = fq_get(netq, &p)) < 0)
		return SUSPEND;
	if ((size_t)n > max)
		n = (ssize_t)max;
	netdriver_copyout(data, 0, p, (size_t)n);
	fq_pop(netq);
	return n;
}

int
data_net_send(struct netdriver_data *data, size_t size)
{
	static uint8_t buf[1600];
	int r;

	if (!data_link_up())
		return EIO;
	if (size > sizeof(buf))
		return EINVAL;
	netdriver_copyin(data, 0, buf, size);
	if ((r = data_send(buf, size, 0)) == EAGAIN)
		return SUSPEND;
	if (r != OK)
		tx_dropped++;
	return r == OK ? OK : r;
}

/* EAPOL from wcnd (an Ethernet frame), unencrypted until the keys. */
int
data_eapol_send(const uint8_t *eth, size_t len)
{

	return data_send(eth, len, !lnk.authorized);
}

ssize_t
data_eapol_recv(uint8_t *buf, size_t size, unsigned ms)
{
	uint8_t *p;
	ssize_t n;
	unsigned t;

	for (t = 0; ; t++) {
		if ((n = fq_get(eapq, &p)) >= 0) {
			if ((size_t)n > size)
				n = (ssize_t)size;
			memcpy(buf, p, (size_t)n);
			fq_pop(eapq);
			return n;
		}
		if (t >= ms)
			return -ETIMEDOUT;
		hw_poll();
		micro_delay(1000);
	}
}
