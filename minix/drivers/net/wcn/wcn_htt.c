/*
 * HTT messages and the rx side of the data path (wcn_htt.h).  Layouts are
 * Linux ath10k's for this chip, taken as numbers: htt.h for the messages,
 * rx_desc.h and htt_rx_desc_v2 for the descriptor (offsets computed by a
 * compiler from those declarations; port/clover/wifi/sta.md, 4.6).  All
 * multi-byte fields are little-endian and nothing is aligned: the
 * messages are read and written byte by byte.
 */

#include <errno.h>
#include <string.h>

#include "wcn_htt.h"

/* ---- bytes ---- */

static void
w16(uint8_t *p, unsigned v)
{

	p[0] = v;
	p[1] = v >> 8;
}

static void
w32(uint8_t *p, uint32_t v)
{

	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
w64(uint8_t *p, uint64_t v)
{

	w32(p, (uint32_t)v);
	w32(p + 4, (uint32_t)(v >> 32));
}

static unsigned
r16(const uint8_t *p)
{

	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t
r32(const uint8_t *p)
{

	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static uint64_t
r64(const uint8_t *p)
{

	return r32(p) | (uint64_t)r32(p + 4) << 32;
}

/* ---- host to target ---- */

size_t
htt_version_req(uint8_t *p, size_t cap)
{

	if (cap < HTT_VERSION_REQ_LEN)
		return 0;
	memset(p, 0, HTT_VERSION_REQ_LEN);
	p[0] = HTT_H2T_VERSION_REQ;
	return HTT_VERSION_REQ_LEN;
}

/*
 * FRAG_DESC_BANK_CFG, the 64-bit form: type, info, number of banks,
 * descriptor size; four le64 bank addresses at 4; four {le16 min, le16
 * max} id ranges at 36; the queue state block at 52 (le32 address, le16
 * peers, le16 tids, record size, multiplier, 2 pad).  One bank; info 0 -
 * this firmware has no peer flow control, so the queue state is unused
 * but for the record size ath10k always puts (1).
 */
size_t
htt_frag_desc_bank(uint8_t *p, size_t cap, uint64_t paddr, unsigned ndesc)
{

	if (cap < HTT_FRAG_BANK_LEN || ndesc == 0 || ndesc > 0x10000 ||
	    paddr == 0)
		return 0;
	memset(p, 0, HTT_FRAG_BANK_LEN);
	p[0] = HTT_H2T_FRAG_DESC_BANK;
	p[1] = 0;
	p[2] = 1;
	p[3] = HTT_FRAG_DESC_SIZE;
	w64(p + 4, paddr);
	w16(p + 36, 0);
	w16(p + 38, ndesc - 1);
	p[60] = 1;		/* record size */
	p[61] = 0;		/* record multiplier */
	return HTT_FRAG_BANK_LEN;
}

/*
 * The rx descriptor of this chip, in 4-byte words, in the order the ring
 * setup carries them: 802.11 header (rx_hdr_status), msdu payload, ppdu
 * start, ppdu end, mpdu start, mpdu end, msdu start, msdu end, attention,
 * frag info.
 */
static const uint16_t rxd_words[10] = { 74, 90, 27, 37, 4, 26, 7, 12, 1, 2 };

/*
 * RX_RING_CFG, the 64-bit form: type, number of rings, le16 reserved;
 * then one ring - le64 index word address, le64 ring address, le16
 * entries (Linux's comment says words; it sends the count of entries),
 * le16 buffer size, le16 flags, le16 the index now, ten le16 offsets.
 * Flags: all sixteen - every descriptor part and every kind of frame.
 */
size_t
htt_rx_ring_cfg(uint8_t *p, size_t cap, uint64_t idx_paddr,
	uint64_t ring_paddr, unsigned ring_len, unsigned idx)
{
	unsigned i;

	if (cap < HTT_RX_RING_CFG_LEN || ring_len == 0 ||
	    ring_len > HTT_RX_RING_SIZE || idx >= ring_len ||
	    idx_paddr == 0 || ring_paddr == 0)
		return 0;
	memset(p, 0, HTT_RX_RING_CFG_LEN);
	p[0] = HTT_H2T_RX_RING_CFG;
	p[1] = 1;
	w64(p + 4, idx_paddr);
	w64(p + 12, ring_paddr);
	w16(p + 20, ring_len);
	w16(p + 22, HTT_RX_BUF_SIZE);
	w16(p + 24, 0xffff);
	w16(p + 26, idx);
	for (i = 0; i < 10; i++)
		w16(p + 28 + 2 * i, rxd_words[i]);
	return HTT_RX_RING_CFG_LEN;
}

/* AGGR_CFG, the form with a reserved byte (ath10k's v2, 64-bit targets). */
size_t
htt_aggr_cfg(uint8_t *p, size_t cap, unsigned ampdu, unsigned amsdu)
{

	if (cap < HTT_AGGR_CFG_LEN || ampdu == 0 || ampdu > 64 ||
	    amsdu == 0 || amsdu > 31)
		return 0;
	p[0] = HTT_H2T_AGGR_CFG;
	p[1] = ampdu;
	p[2] = amsdu;
	p[3] = 0;
	return HTT_AGGR_CFG_LEN;
}

/* TX_FRM's flag bits. */
#define F0_MAC_HDR	0x01
#define F0_NO_ENCRYPT	0x04
#define F0_PKT_SHIFT	5
#define F1_TID_SHIFT	6
#define F1_POSTPONED	0x0800
#define F1_CSUM_L3	0x2000
#define F1_CSUM_L4	0x4000

size_t
htt_tx_prefetch(size_t len)
{

	if (len > HTT_TX_PREFETCH)
		len = HTT_TX_PREFETCH;
	return (len + 3) & ~(size_t)3;
}

/*
 * TX_FRM, the 64-bit descriptor (19 bytes after the type): flags0;
 * le16 flags1 - vdev in bits 0-5, extended TID in 6-10, POSTPONED always
 * (the firmware then never asks the host to inspect a frame), checksum
 * offload; le16 length; le16 msdu id; le64 fragment descriptor address;
 * le16 peer id (invalid: the firmware finds the peer), le16 frequency -
 * the form for HTT 3.4 and later on TLV firmware.
 */
size_t
htt_tx_frm(uint8_t *p, size_t cap, const struct htt_tx *t,
	const uint8_t *frame)
{
	unsigned f0, f1;
	size_t pre, n;

	if (t->pkt > HTT_PKT_MGMT || t->vdev > 63 || t->tid > 31 ||
	    t->id > 0xffff || t->len == 0 || t->len > 0xffff ||
	    t->freq > 0xffff || t->frags_paddr == 0)
		return 0;
	pre = frame != NULL ? htt_tx_prefetch(t->len) : 0;
	if (cap < HTT_TX_FRM_LEN + pre)
		return 0;

	f0 = t->pkt << F0_PKT_SHIFT;
	if (t->pkt != HTT_PKT_ETH)
		f0 |= F0_MAC_HDR;
	if (t->no_encrypt)
		f0 |= F0_NO_ENCRYPT;
	f1 = t->vdev | t->tid << F1_TID_SHIFT | F1_POSTPONED;
	if (t->csum)
		f1 |= F1_CSUM_L3 | F1_CSUM_L4;

	p[0] = HTT_H2T_TX_FRM;
	p[1] = f0;
	w16(p + 2, f1);
	w16(p + 4, t->len);
	w16(p + 6, t->id);
	w64(p + 8, t->frags_paddr);
	w16(p + 16, HTT_INVALID_PEER);
	w16(p + 18, t->freq);
	if (pre != 0) {
		n = t->len < pre ? t->len : pre;
		memcpy(p + HTT_TX_FRM_LEN, frame, n);
		memset(p + HTT_TX_FRM_LEN + n, 0, pre - n);
	}
	return HTT_TX_FRM_LEN + pre;
}

uint64_t
htt_frag_paddr(uint64_t bank, unsigned id)
{

	return bank + (uint64_t)HTT_FRAG_DESC_SIZE * id;
}

/*
 * The 64-bit msdu extension descriptor: five le32 TSO words, le16 IP id,
 * flags, reserved; then six fragments of {le32 address low, le16 address
 * high, le16 length} from 24.  Checksum offload: bits 16-20 of the fourth
 * TSO word (IPv4, UDP over v4 and v6, TCP over v4 and v6).
 */
int
htt_frag_desc(uint8_t *p, size_t cap, uint64_t paddr, unsigned len, int csum)
{

	if (cap < HTT_FRAG_DESC_SIZE || len == 0 || len > 0xffff ||
	    paddr == 0 || paddr >> 48 != 0)
		return EINVAL;
	memset(p, 0, HTT_FRAG_DESC_SIZE);
	if (csum)
		w32(p + 12, 0x1fu << 16);
	w32(p + 24, (uint32_t)paddr);
	w16(p + 28, (unsigned)(paddr >> 32));
	w16(p + 30, len);
	return 0;
}

static const uint8_t rfc1042[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static const uint8_t tunnel[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

size_t
htt_nwifi_hdr(uint8_t *p, size_t cap, const uint8_t bssid[6],
	const uint8_t sa[6], const uint8_t da[6], uint16_t ethertype,
	int protect)
{

	if (cap < HTT_NWIFI_HDR + HTT_SNAP_LEN)
		return 0;
	memset(p, 0, HTT_NWIFI_HDR);
	p[0] = 0x08;			/* data */
	p[1] = 0x01;			/* to DS */
	if (protect)
		p[1] |= 0x40;
	memcpy(p + 4, bssid, 6);
	memcpy(p + 10, sa, 6);
	memcpy(p + 16, da, 6);
	/* Sequence 0: [?] the firmware numbers data frames itself. */
	memcpy(p + HTT_NWIFI_HDR, rfc1042, 6);
	p[HTT_NWIFI_HDR + 6] = ethertype >> 8;
	p[HTT_NWIFI_HDR + 7] = ethertype;
	return HTT_NWIFI_HDR + HTT_SNAP_LEN;
}

/* ---- target to host ---- */

int
htt_t2h_type(const uint8_t *m, size_t len)
{

	if (len < 1)
		return -1;
	return m[0];
}

/* VERSION_CONF: minor at 1, major at 2, a reserved byte. */
int
htt_version_conf(const uint8_t *m, size_t len, unsigned *major,
	unsigned *minor)
{

	if (len < 3 || m[0] != HTT_T2H_VERSION_CONF)
		return EINVAL;
	*minor = m[1];
	*major = m[2];
	return 0;
}

/* PEER_MAP: vdev at 1, le16 peer id at 2, the address at 4, 2 reserved. */
int
htt_peer_map(const uint8_t *m, size_t len, struct htt_peer_map *pm)
{

	if (len < 10 || m[0] != HTT_T2H_PEER_MAP)
		return EINVAL;
	pm->vdev = m[1];
	pm->peer_id = r16(m + 2);
	memcpy(pm->addr, m + 4, 6);
	return 0;
}

/* PEER_UNMAP: reserved at 1, le16 peer id at 2. */
int
htt_peer_unmap(const uint8_t *m, size_t len, unsigned *peer_id)
{

	if (len < 4 || m[0] != HTT_T2H_PEER_UNMAP)
		return EINVAL;
	*peer_id = r16(m + 2);
	return 0;
}

/*
 * TX_COMPL_IND: flags at 1 (status bits 0-2, TID 3-6, TID invalid 7), the
 * number of msdus at 2, flags2 at 3, le16 msdu ids from 4.  On this chip
 * bit 3 of flags2 says ACK RSSI follow: the id list is then padded to an
 * even count with 0xffff, bits 0 and 1 add a retries record and a
 * timestamp (2 le16 each), and then come count le16 RSSI.  [?] Bit 3 is
 * also PPDU_DURATION_PRESENT in ath10k's generic flags, and ath10k reads
 * airtime records after the RSSI as well on this chip; they are ignored
 * here.  Linux checks no length; the ids must fit here, the RSSI are
 * reported only when they fit too.
 */
int
htt_tx_compl(const uint8_t *m, size_t len, struct htt_tx_compl *c)
{
	size_t words;

	if (len < 4 || m[0] != HTT_T2H_TX_COMPL_IND)
		return EINVAL;
	c->status = m[1] & 0x07;
	c->tid = (m[1] >> 3) & 0x0f;
	c->tid_invalid = (m[1] & 0x80) != 0;
	c->count = m[2];
	if ((len - 4) / 2 < c->count)
		return EINVAL;
	c->ids = m + 4;
	c->rssi = NULL;
	if (m[3] & 0x08) {
		words = c->count + (c->count & 1);
		if (m[3] & 0x01)
			words += 2;
		if (m[3] & 0x02)
			words += 2;
		words += c->count;
		if ((len - 4) / 2 >= words)
			c->rssi = m + 4 + 2 * (words - c->count);
	}
	return 0;
}

unsigned
htt_tx_compl_id(const struct htt_tx_compl *c, unsigned i)
{

	if (i >= c->count)
		return HTT_INVALID_MSDU;
	return r16(c->ids + 2 * i);
}

int
htt_tx_compl_rssi(const struct htt_tx_compl *c, unsigned i)
{

	if (c->rssi == NULL || i >= c->count)
		return -1;
	return r16(c->rssi + 2 * i);
}

/*
 * RX_IN_ORD_PADDR_IND: info at 1 (TID bits 0-4, offload 5, fragment 6),
 * le16 peer id at 2, vdev at 4, reserved, le16 msdu count at 6; from 8
 * the records of the 64-bit form: le64 address, le16 length, the
 * firmware's descriptor byte, reserved - 12 bytes.  (ath10k measures the
 * records as 8 bytes even in this form; here they are counted whole.)
 */
#define IN_ORD_REC	12

int
htt_in_ord(const uint8_t *m, size_t len, struct htt_in_ord *io)
{

	if (len < 8 || m[0] != HTT_T2H_RX_IN_ORD_PADDR)
		return EINVAL;
	io->tid = m[1] & 0x1f;
	io->offload = (m[1] & 0x20) != 0;
	io->frag = (m[1] & 0x40) != 0;
	io->peer_id = r16(m + 2);
	io->vdev = m[4];
	io->count = r16(m + 6);
	if ((len - 8) / IN_ORD_REC < io->count)
		return EINVAL;
	io->msdus = m + 8;
	return 0;
}

void
htt_in_ord_msdu(const struct htt_in_ord *io, unsigned i, uint64_t *paddr,
	unsigned *len, unsigned *fw_desc)
{
	const uint8_t *e;

	if (i >= io->count) {
		*paddr = 0;
		*len = 0;
		*fw_desc = 0;
		return;
	}
	e = io->msdus + IN_ORD_REC * i;
	*paddr = r64(e);
	*len = r16(e + 8);
	*fw_desc = e[10];
}

/*
 * RX_IND (the path without full reorder): info0 at 1 (TID bits 0-4,
 * flush valid 5, release valid 6), le16 peer id at 2, le32 info1 at 4
 * (flush start 0-5, end 6-11, release start 12-17, end 18-23, number of
 * mpdu ranges 24-31); a 36-byte ppdu block at 8; le16 length of the
 * firmware descriptors at 44; those bytes from 48; the ranges after them,
 * rounded up to 4, four bytes each.
 */
int
htt_rx_ind(const uint8_t *m, size_t len, struct htt_rx_ind *ri)
{
	uint32_t info1;
	size_t at;
	unsigned i;

	if (len < 48 || m[0] != HTT_T2H_RX_IND)
		return EINVAL;
	ri->tid = m[1] & 0x1f;
	ri->flush_valid = (m[1] & 0x20) != 0;
	ri->release_valid = (m[1] & 0x40) != 0;
	ri->peer_id = r16(m + 2);
	info1 = r32(m + 4);
	ri->flush_start = info1 & 0x3f;
	ri->flush_end = (info1 >> 6) & 0x3f;
	ri->release_start = (info1 >> 12) & 0x3f;
	ri->release_end = (info1 >> 18) & 0x3f;
	ri->nranges = info1 >> 24;
	ri->fw_desc_len = r16(m + 44);
	if (len - 48 < ri->fw_desc_len)
		return EINVAL;
	ri->fw_desc = m + 48;
	at = 48 + (((size_t)ri->fw_desc_len + 3) & ~(size_t)3);
	if (at > len || (len - at) / 4 < ri->nranges)
		return EINVAL;
	ri->ranges = m + at;
	ri->mpdus = 0;
	for (i = 0; i < ri->nranges; i++)
		ri->mpdus += ri->ranges[4 * i];
	return 0;
}

/*
 * SEC_IND: flags at 1 (cipher bits 0-6, unicast 7), le16 peer id at 2,
 * the Michael key at 4 (TKIP), 16 bytes of WAPI counter after it.
 */
int
htt_sec_ind(const uint8_t *m, size_t len, struct htt_sec_ind *s)
{

	if (len < 12 || m[0] != HTT_T2H_SEC_IND)
		return EINVAL;
	s->type = m[1] & 0x7f;
	s->unicast = (m[1] & 0x80) != 0;
	s->peer_id = r16(m + 2);
	memcpy(s->michael, m + 4, 8);
	return 0;
}

/* The offload header: le16 length, le16 peer, vdev, TID, fw descriptor. */
int
htt_rx_offload(const uint8_t *buf, size_t blen, struct htt_rx_offload *o)
{

	if (blen < 7)
		return EINVAL;
	o->len = r16(buf);
	o->peer_id = r16(buf + 2);
	o->vdev = buf[4];
	o->tid = buf[5];
	o->fw_desc = buf[6];
	o->off = 7;
	if (blen - 7 < o->len)
		return EINVAL;
	return 0;
}

/* ---- the rx descriptor ---- */

/*
 * Byte offsets in the 360-byte descriptor (htt_rx_desc_v2): the
 * firmware's byte 0; attention 4; frag info 8 (ring 0..3 more counts,
 * then 4..7); mpdu start 16 (info0, pn, info1); msdu start 28 (info0,
 * flow crc, info1, info2, info3); msdu end 48 (checksums, key id, filter,
 * 10 bytes WAPI PN, info0 at 64; this chip's part from 68 - IPv6 crc, TCP
 * seq and ack, info1 at 80, ...); mpdu end 104; ppdu start 108; ppdu end
 * 148; rx_hdr_status 296; the msdu 360.
 */
#define RXD_ATT		4
#define RXD_FRAG	8
#define RXD_MPDU_START	16
#define RXD_MSDU_START	28
#define RXD_MSDU_END	48
#define RXD_MPDU_END	104
#define RXD_HDR		296

int
htt_rxd_parse(const uint8_t *b, size_t blen, struct htt_rxd *d)
{
	uint32_t v;

	if (blen < HTT_RX_DESC_SIZE)
		return EINVAL;
	d->fw_desc = b[0];
	d->att = r32(b + RXD_ATT);
	d->chained = b[RXD_FRAG + 2];		/* ring 2's more count */

	v = r32(b + RXD_MPDU_START);
	d->peer_idx = v & 0x7ff;
	d->from_ds = (v >> 11) & 1;
	d->to_ds = (v >> 12) & 1;
	d->encrypted = (v >> 13) & 1;
	d->seq = (v >> 16) & 0xfff;
	d->enc = v >> 28;
	d->tid = r32(b + RXD_MPDU_START + 8) >> 28;

	v = r32(b + RXD_MSDU_START);
	d->msdu_len = v & 0x3fff;
	v = r32(b + RXD_MSDU_START + 8);
	d->msdu_num = v & 0xff;
	d->decap = (v >> 8) & 3;

	v = r32(b + RXD_MSDU_END + 16);
	d->first_msdu = (v >> 14) & 1;
	d->last_msdu = (v >> 15) & 1;
	d->l3pad_field = (r32(b + RXD_MSDU_END + 32) >> 10) & 7;

	d->mpdu_end = r32(b + RXD_MPDU_END);
	d->hdr = b + RXD_HDR;
	d->decrypted = d->enc != HTT_ENC_NONE &&
	    (d->att & (HTT_ATT_FCS_ERR | HTT_ATT_DECRYPT_ERR |
	    HTT_ATT_PEER_INVALID)) == 0;
	return 0;
}

int
htt_rxd_bad(const struct htt_rxd *d)
{

	return (d->att & HTT_ATT_MSDU_DONE) == 0 ||
	    (d->att & (HTT_ATT_FCS_ERR | HTT_ATT_DECRYPT_ERR |
	    HTT_ATT_TKIP_MIC_ERR)) != 0;
}

/*
 * No layer-3 padding on this chip: ath10k has no pad accessor for it and
 * puts the frame right after the descriptor.  [?] msdu_end carries the
 * field anyway (l3pad_field); if the firmware ever fills it, this is the
 * place to add it to the offset.
 */
int
htt_rxd_payload(const struct htt_rxd *d, size_t blen, unsigned ind_len,
	size_t *off, size_t *len, size_t *rest)
{
	size_t want, room;

	if (blen < HTT_RX_DESC_SIZE)
		return EINVAL;
	if ((d->att & HTT_ATT_MSDU_DONE) == 0)
		return EIO;
	if (ind_len == HTT_LEN_DESC)
		want = d->att & (HTT_ATT_MSDU_LEN_ERR | HTT_ATT_MPDU_LEN_ERR) ?
		    0 : d->msdu_len;
	else
		want = ind_len;
	room = blen;
	if (room > HTT_RX_BUF_SIZE)
		room = HTT_RX_BUF_SIZE;
	room -= HTT_RX_DESC_SIZE;
	*off = HTT_RX_DESC_SIZE;
	*len = want < room ? want : room;
	*rest = want - *len;
	return 0;
}

long
htt_nwifi_to_eth(uint8_t *p, size_t len)
{
	unsigned fc, tods, fromds;
	size_t h, start;
	uint8_t da[6], sa[6];

	if (len < 24)
		return -1;
	fc = r16(p);
	if (((fc >> 2) & 3) != 2)		/* not data */
		return -1;
	tods = (fc >> 8) & 1;
	fromds = (fc >> 9) & 1;
	h = 24;
	if (tods && fromds)
		h += 6;
	if (fc & 0x80) {			/* QoS subtype */
		h += 2;
		if (fc & 0x8000)		/* order: HT control */
			h += 4;
	}
	if (len < h)
		return -1;
	memcpy(da, p + (tods ? 16 : 4), 6);
	if (!fromds)
		memcpy(sa, p + 10, 6);
	else if (!tods)
		memcpy(sa, p + 16, 6);
	else
		memcpy(sa, p + 24, 6);

	if (len - h >= HTT_SNAP_LEN && (memcmp(p + h, rfc1042, 6) == 0 ||
	    memcmp(p + h, tunnel, 6) == 0)) {
		start = h + HTT_SNAP_LEN - 14;	/* EtherType stays put */
	} else {
		if (len - h > 0xffff)
			return -1;
		start = h - 14;
		p[start + 12] = (len - h) >> 8;
		p[start + 13] = len - h;
	}
	memcpy(p + start, da, 6);
	memcpy(p + start + 6, sa, 6);
	return (long)start;
}

/* ---- the host rx ring ---- */

static unsigned
hslot(uint64_t paddr)
{
	uint64_t x = paddr >> 3;

	x ^= x >> 17;
	x *= 0x9e3779b97f4a7c15ull;
	return (unsigned)(x >> 40) & (HTT_RING_HASH - 1);
}

static int
hfind(const struct htt_ring *r, uint64_t paddr)
{
	unsigned i, n;

	for (i = hslot(paddr), n = 0; n < HTT_RING_HASH;
	    i = (i + 1) & (HTT_RING_HASH - 1), n++) {
		if (!r->hash[i].used)
			return -1;
		if (r->hash[i].paddr == paddr)
			return (int)i;
	}
	return -1;
}

/* Linear probing; removal shifts the run back, so no tombstones. */
static void
hdel(struct htt_ring *r, unsigned i)
{
	unsigned j, k;

	r->hash[i].used = 0;
	for (j = (i + 1) & (HTT_RING_HASH - 1); r->hash[j].used;
	    j = (j + 1) & (HTT_RING_HASH - 1)) {
		k = hslot(r->hash[j].paddr);
		/* Leave j if its home lies cyclically in (i, j]. */
		if (i <= j ? (i < k && k <= j) : (i < k || k <= j))
			continue;
		r->hash[i] = r->hash[j];
		r->hash[j].used = 0;
		i = j;
	}
}

int
htt_ring_init(struct htt_ring *r, unsigned size, unsigned level, int in_ord)
{

	if (size < 2 || size > HTT_RX_RING_SIZE || (size & (size - 1)) != 0 ||
	    level == 0 || level >= size)
		return EINVAL;
	memset(r, 0, sizeof(*r));
	r->size = size;
	r->level = level;
	r->in_ord = in_ord;
	return 0;
}

unsigned
htt_ring_deficit(const struct htt_ring *r)
{

	return r->fill < r->level ? r->level - r->fill : 0;
}

/*
 * In order, the firmware has taken an entry by the time the host comes
 * round to it again, and the buffer is known by its address alone - a
 * slot still marked is overwritten (ath10k does the same).  In ring
 * order the slot must have been taken.
 */
int
htt_ring_post(struct htt_ring *r, uint8_t *ring, uint64_t paddr, uint32_t tag)
{
	unsigned i;

	if (paddr == 0 || r->fill >= r->size - 1)
		return ENOSPC;
	if (!r->in_ord && r->slot[r->idx].used)
		return ENOSPC;
	if (hfind(r, paddr) >= 0)
		return EEXIST;
	for (i = hslot(paddr); r->hash[i].used; i = (i + 1) & (HTT_RING_HASH - 1))
		;
	r->hash[i].paddr = paddr;
	r->hash[i].tag = tag;
	r->hash[i].slot = r->idx;
	r->hash[i].used = 1;
	r->slot[r->idx].paddr = paddr;
	r->slot[r->idx].tag = tag;
	r->slot[r->idx].used = 1;
	w64(ring + 8 * (size_t)r->idx, paddr);
	r->idx = (r->idx + 1) & (r->size - 1);
	r->fill++;
	return 0;
}

unsigned
htt_ring_index(const struct htt_ring *r)
{

	return r->idx;
}

int
htt_ring_index_ok(const struct htt_ring *r, uint32_t v)
{

	return v < r->size;
}

int
htt_ring_take(struct htt_ring *r, uint64_t paddr, uint32_t *tag)
{
	int h;
	unsigned s;

	h = hfind(r, paddr);
	if (h < 0)
		return ENOENT;
	*tag = r->hash[h].tag;
	s = r->hash[h].slot;
	if (r->slot[s].used && r->slot[s].paddr == paddr)
		r->slot[s].used = 0;
	hdel(r, (unsigned)h);
	r->fill--;
	return 0;
}

int
htt_ring_next(struct htt_ring *r, uint8_t *ring, uint64_t *paddr,
	uint32_t *tag)
{
	int h;

	if (!r->slot[r->rd].used)
		return ENOENT;
	*paddr = r->slot[r->rd].paddr;
	*tag = r->slot[r->rd].tag;
	r->slot[r->rd].used = 0;
	h = hfind(r, *paddr);
	if (h >= 0)
		hdel(r, (unsigned)h);
	w64(ring + 8 * (size_t)r->rd, 0);
	r->rd = (r->rd + 1) & (r->size - 1);
	r->fill--;
	return 0;
}
