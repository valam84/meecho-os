#ifndef _WCN_HTT_H
#define _WCN_HTT_H

/*
 * HTT, the data-path protocol of the WCN3990 (port/clover/wifi/htc-wmi.md,
 * section 3; the data path in port/clover/wifi/sta.md, section 4): the
 * firmware's HTT op version 3 (TLV), HTT 3.56, a 64-bit target, the rx
 * descriptor of this chip, native wifi framing, no 4-address padding.
 *
 * Only the HTT payload is here - what follows the 8-byte HTC header on
 * the way out, and what arrives with that header already taken off (the
 * HTC layer strips it on CE1; CE5 delivers messages without one).  Byte 0
 * is always the message type.
 *
 * No MINIX and no transport: messages are built into a caller's buffer
 * and parsed out of one, every length checked; the rx ring bookkeeping
 * below is arithmetic over tables the caller owns.  The host stand
 * (port/test/wcn/htttest.c) holds the bytes against the layouts Linux's
 * ath10k puts on the wire.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- message types ---- */

/* Host to target. */
#define HTT_H2T_VERSION_REQ	0
#define HTT_H2T_TX_FRM		1
#define HTT_H2T_RX_RING_CFG	2
#define HTT_H2T_STATS_REQ	3
#define HTT_H2T_SYNC		4
#define HTT_H2T_AGGR_CFG	5
#define HTT_H2T_FRAG_DESC_BANK	6
#define HTT_H2T_MGMT_TX		7	/* HTT < 3 only; not built here */

/* Target to host, the TLV firmware's numbering. */
#define HTT_T2H_VERSION_CONF	0x00
#define HTT_T2H_RX_IND		0x01
#define HTT_T2H_RX_FLUSH	0x02
#define HTT_T2H_PEER_MAP	0x03
#define HTT_T2H_PEER_UNMAP	0x04
#define HTT_T2H_RX_ADDBA	0x05
#define HTT_T2H_RX_DELBA	0x06
#define HTT_T2H_TX_COMPL_IND	0x07
#define HTT_T2H_PKTLOG		0x08
#define HTT_T2H_STATS_CONF	0x09
#define HTT_T2H_RX_FRAG_IND	0x0a
#define HTT_T2H_SEC_IND		0x0b
#define HTT_T2H_TX_INSPECT_IND	0x0d
#define HTT_T2H_MGMT_TX_COMPL	0x0e
#define HTT_T2H_TX_CREDIT	0x0f
#define HTT_T2H_RX_PN_IND	0x10
#define HTT_T2H_RX_OFFLOAD	0x11
#define HTT_T2H_RX_IN_ORD_PADDR	0x12
#define HTT_T2H_CHAN_CHANGE	0x15
#define HTT_T2H_NUM		0x18	/* a type at or past this: skip */

/* ---- sizes of this machine ---- */

#define HTT_RX_RING_SIZE	2048	/* entries, le64 each */
#define HTT_RX_RING_FILL	2047	/* ath10k's fill level for this chip */
#define HTT_RX_BUF_SIZE		2048
#define HTT_RX_BUF_ALIGN	8
#define HTT_RX_DESC_SIZE	360	/* in front of every rx buffer */
#define HTT_RX_PAYLOAD_MAX	(HTT_RX_BUF_SIZE - HTT_RX_DESC_SIZE)
#define HTT_MAX_PENDING_TX	1056	/* msdu ids 0..1055 */
#define HTT_FRAG_DESC_SIZE	72	/* one per msdu id, in one bank */
#define HTT_TX_PREFETCH		52	/* 36 + 4 + 8 + 2, rounded up to 4 */
#define HTT_INVALID_PEER	0xffff
#define HTT_INVALID_MSDU	0xffff

/* The message lengths (msg_type byte included). */
#define HTT_VERSION_REQ_LEN	4
#define HTT_FRAG_BANK_LEN	64
#define HTT_RX_RING_CFG_LEN	48
#define HTT_AGGR_CFG_LEN	4
#define HTT_TX_FRM_LEN		20	/* + the prefetched start of the frame */

/* The HTT version this driver was written against. */
#define HTT_VERSION_MAJOR	3
#define HTT_VERSION_MINOR	56

/* ---- host to target ---- */

/*
 * Every builder writes into p[0..cap) and returns the message length, or 0
 * when the buffer is too small or an argument is out of range.
 */
size_t htt_version_req(uint8_t *p, size_t cap);

/* The fragment descriptor bank: ndesc descriptors of 72 bytes at paddr. */
size_t htt_frag_desc_bank(uint8_t *p, size_t cap, uint64_t paddr,
	unsigned ndesc);

/*
 * The host rx ring: ring_paddr - the array of ring_len le64 buffer
 * addresses; idx_paddr - the le32 word that holds the host's fill index
 * (Linux's alloc_idx, the "firmware index shadow"); idx - its value now.
 * Buffer size, enabled fields and descriptor offsets are this chip's.
 */
size_t htt_rx_ring_cfg(uint8_t *p, size_t cap, uint64_t idx_paddr,
	uint64_t ring_paddr, unsigned ring_len, unsigned idx);

/* A-MPDU 1..64 and A-MSDU 1..31 subframes; ath10k's are 64 and 3. */
size_t htt_aggr_cfg(uint8_t *p, size_t cap, unsigned ampdu, unsigned amsdu);

/* TX_FRM's packet types (flags0 bits 5-7). */
#define HTT_PKT_RAW		0
#define HTT_PKT_NWIFI		1
#define HTT_PKT_ETH		2
#define HTT_PKT_MGMT		3

/* TX_FRM's extended TIDs: 0..15 for QoS data. */
#define HTT_TID_NONQOS		16	/* non-QoS data, multicast */
#define HTT_TID_MGMT		17

struct htt_tx {
	unsigned pkt;		/* HTT_PKT_* */
	unsigned vdev;		/* 0..63 */
	unsigned tid;		/* 0..31: 0..15, or HTT_TID_* */
	unsigned id;		/* msdu id: the completion names it */
	unsigned len;		/* frame length */
	uint64_t frags_paddr;	/* frag desc of this id; MGMT: the frame */
	unsigned freq;		/* MHz, 0: the vdev's home channel */
	int no_encrypt;		/* EAPOL before the keys, or any frame
				   sent without Protected */
	int csum;		/* checksum offload (L3 and L4) */
};

/*
 * TX_FRM: the 20 bytes (type and descriptor) and, if frame is given, the
 * prefetch the firmware's classifier reads - the first
 * htt_tx_prefetch(len) bytes of the frame, zeros past its end.  With
 * frame NULL the caller sends the prefetch as a second gather piece.
 * Packet types NWIFI and ETH go with a fragment descriptor
 * (htt_frag_desc()); MGMT names the frame itself - the HTT path for
 * management frames, a fallback for a firmware that does not offer
 * WMI_SERVICE_MGMT_TX_WMI (sta.md, 3.3).  RAW is not used by this driver.
 */
size_t htt_tx_frm(uint8_t *p, size_t cap, const struct htt_tx *t,
	const uint8_t *frame);
size_t htt_tx_prefetch(size_t len);

/* The bank's descriptor for msdu id. */
uint64_t htt_frag_paddr(uint64_t bank, unsigned id);

/* A 72-byte fragment descriptor: one fragment, the frame at paddr. */
int htt_frag_desc(uint8_t *p, size_t cap, uint64_t paddr, unsigned len,
	int csum);

/*
 * The native wifi header of a data frame to the AP (ToDS), with the
 * LLC/SNAP and EtherType after it: 24 + 8 bytes, no QoS control (the TID
 * goes in the descriptor).  Protected when protect; the firmware puts the
 * IV and MIC.  Returns 32, or 0.
 */
#define HTT_NWIFI_HDR		24
#define HTT_SNAP_LEN		8
size_t htt_nwifi_hdr(uint8_t *p, size_t cap, const uint8_t bssid[6],
	const uint8_t sa[6], const uint8_t da[6], uint16_t ethertype,
	int protect);

/* ---- target to host ---- */

/* The type of a message, -1 if empty. */
int htt_t2h_type(const uint8_t *m, size_t len);

/* Each parser: 0, or EINVAL when the message is short or not its type. */
int htt_version_conf(const uint8_t *m, size_t len, unsigned *major,
	unsigned *minor);

struct htt_peer_map {
	unsigned vdev;
	unsigned peer_id;
	uint8_t addr[6];
};

int htt_peer_map(const uint8_t *m, size_t len, struct htt_peer_map *pm);
int htt_peer_unmap(const uint8_t *m, size_t len, unsigned *peer_id);

/* TX_COMPL_IND's status. */
#define HTT_TX_OK		0
#define HTT_TX_DISCARD		1
#define HTT_TX_NO_ACK		2
#define HTT_TX_POSTPONE		3

struct htt_tx_compl {
	unsigned status;	/* HTT_TX_* */
	unsigned tid;
	int tid_invalid;
	unsigned count;
	const uint8_t *ids;	/* count le16 msdu ids */
	const uint8_t *rssi;	/* count le16 ACK RSSI, or NULL */
};

int htt_tx_compl(const uint8_t *m, size_t len, struct htt_tx_compl *c);
unsigned htt_tx_compl_id(const struct htt_tx_compl *c, unsigned i);
int htt_tx_compl_rssi(const struct htt_tx_compl *c, unsigned i);

/* RX_IN_ORD_PADDR_IND, 64-bit: msdus each found by physical address. */
struct htt_in_ord {
	unsigned tid;
	int offload;		/* buffers carry htt_rx_offload(), no desc */
	int frag;
	unsigned peer_id;
	unsigned vdev;
	unsigned count;
	const uint8_t *msdus;	/* count records of 12 bytes */
};

int htt_in_ord(const uint8_t *m, size_t len, struct htt_in_ord *io);
void htt_in_ord_msdu(const struct htt_in_ord *io, unsigned i,
	uint64_t *paddr, unsigned *len, unsigned *fw_desc);

/* RX_IND: the msdus are the next buffers of the ring, in ring order. */
struct htt_rx_ind {
	unsigned tid;
	unsigned peer_id;
	int flush_valid, release_valid;
	unsigned flush_start, flush_end;
	unsigned release_start, release_end;
	unsigned nranges;
	const uint8_t *fw_desc;	/* one byte per msdu */
	unsigned fw_desc_len;
	const uint8_t *ranges;	/* nranges of {mpdu count, status, 2 pad} */
	unsigned mpdus;		/* the sum of the counts */
};

#define HTT_RX_IND_MPDU_OK	1	/* a range's status */

int htt_rx_ind(const uint8_t *m, size_t len, struct htt_rx_ind *ri);

/* SEC_IND: the cipher the firmware installed for a peer. */
struct htt_sec_ind {
	unsigned type;		/* HTT_SEC_* */
	int unicast;
	unsigned peer_id;
	uint8_t michael[8];
};

#define HTT_SEC_NONE		0
#define HTT_SEC_TKIP		4
#define HTT_SEC_CCMP		6

int htt_sec_ind(const uint8_t *m, size_t len, struct htt_sec_ind *s);

/* An offloaded msdu (in-order with offload): a 7-byte header, then it. */
struct htt_rx_offload {
	unsigned len, peer_id, vdev, tid, fw_desc;
	size_t off;		/* where the frame starts in the buffer */
};

int htt_rx_offload(const uint8_t *buf, size_t blen, struct htt_rx_offload *o);

/* ---- the rx descriptor of this chip ---- */

/* attention, the bits a host acts on */
#define HTT_ATT_FIRST_MPDU	(1u << 0)
#define HTT_ATT_LAST_MPDU	(1u << 1)
#define HTT_ATT_MCAST		(1u << 2)
#define HTT_ATT_PEER_INVALID	(1u << 3)
#define HTT_ATT_MGMT		(1u << 8)
#define HTT_ATT_CTRL		(1u << 9)
#define HTT_ATT_OVERFLOW	(1u << 16)
#define HTT_ATT_MSDU_LEN_ERR	(1u << 17)
#define HTT_ATT_MPDU_LEN_ERR	(1u << 27)
#define HTT_ATT_TKIP_MIC_ERR	(1u << 28)
#define HTT_ATT_DECRYPT_ERR	(1u << 29)
#define HTT_ATT_FCS_ERR		(1u << 30)
#define HTT_ATT_MSDU_DONE	(1u << 31)

/* mpdu_start's cipher */
#define HTT_ENC_CCMP		6
#define HTT_ENC_NONE		7

/* msdu_start's decap format */
#define HTT_DECAP_RAW		0
#define HTT_DECAP_NWIFI		1
#define HTT_DECAP_ETH2		2
#define HTT_DECAP_8023		3

struct htt_rxd {
	uint32_t att;		/* HTT_ATT_* */
	unsigned fw_desc;	/* byte 0, as the firmware's indication left it */
	unsigned chained;	/* buffers following with this msdu's rest */
	/* mpdu_start */
	unsigned enc;		/* HTT_ENC_* */
	int encrypted;
	unsigned seq;
	unsigned peer_idx;
	unsigned tid;
	int from_ds, to_ds;
	/* msdu_start */
	unsigned msdu_len;
	unsigned decap;		/* HTT_DECAP_* */
	unsigned msdu_num;
	/* msdu_end */
	int first_msdu, last_msdu;
	unsigned l3pad_field;	/* [?] ath10k ignores it on this chip */
	/* mpdu_end */
	uint32_t mpdu_end;
	const uint8_t *hdr;	/* rx_hdr_status: the original 802.11 header */
	int decrypted;		/* as ath10k decides it */
};

/* Parse the descriptor at buf[0..360).  0, or EINVAL when blen < 360. */
int htt_rxd_parse(const uint8_t *buf, size_t blen, struct htt_rxd *d);

/* The frame should be dropped: not done, an FCS, cipher or MIC error. */
int htt_rxd_bad(const struct htt_rxd *d);

/*
 * Where the msdu lies in its buffer of blen bytes: *off and *len in this
 * buffer, *rest - bytes left for chained buffers (the RX_IND path; they
 * carry no descriptor).  ind_len: the length an in-order indication gave,
 * or HTT_LEN_DESC to take the descriptor's (0 on a length error, as
 * ath10k).  EIO when MSDU_DONE is not set, EINVAL on a short buffer.
 */
#define HTT_LEN_DESC		0xffffffffu
int htt_rxd_payload(const struct htt_rxd *d, size_t blen, unsigned ind_len,
	size_t *off, size_t *len, size_t *rest);

/*
 * A native wifi msdu (decap NWIFI) made an Ethernet frame in place: the
 * 802.11 header and an RFC 1042 / bridge-tunnel SNAP give way to
 * destination, source and EtherType; any other LLC stays and the frame is
 * 802.3 with a length.  Returns the offset in p where the Ethernet frame
 * starts (its length is len minus that), or -1.
 */
long htt_nwifi_to_eth(uint8_t *p, size_t len);

/* ---- the host rx ring ---- */

/*
 * The ring of buffer addresses the firmware takes from, and the table of
 * buffers the host has posted, found again by physical address (in-order
 * indications) or in ring order (RX_IND).  The caller owns the memory the
 * firmware reads: htt_ring_post() writes the le64 entry into ring[] and
 * says the index to publish; the caller cleans the cache, then writes
 * htt_ring_index() into the index word.  tag is the caller's name for the
 * buffer (its slot in a pool).
 */
#define HTT_RING_HASH		(2 * HTT_RX_RING_SIZE)

struct htt_ring {
	unsigned size;		/* a power of two, <= HTT_RX_RING_SIZE */
	unsigned level;		/* posted buffers to keep, < size */
	unsigned idx;		/* the next entry to post: alloc_idx */
	unsigned rd;		/* the next entry RX_IND takes */
	unsigned fill;		/* buffers posted and not taken */
	int in_ord;		/* buffers come back by address */
	struct { uint64_t paddr; uint32_t tag; int used; } slot[HTT_RX_RING_SIZE];
	struct { uint64_t paddr; uint32_t tag; uint32_t slot; int used; }
	    hash[HTT_RING_HASH];
};

int htt_ring_init(struct htt_ring *r, unsigned size, unsigned level,
	int in_ord);
unsigned htt_ring_deficit(const struct htt_ring *r);
/* 0; EEXIST - the address is posted already; ENOSPC - no room. */
int htt_ring_post(struct htt_ring *r, uint8_t *ring, uint64_t paddr,
	uint32_t tag);
unsigned htt_ring_index(const struct htt_ring *r);
/* Is an index word read back a valid index?  1 or 0. */
int htt_ring_index_ok(const struct htt_ring *r, uint32_t v);
/* 0 and the tag; ENOENT - not posted (the firmware named a stranger). */
int htt_ring_take(struct htt_ring *r, uint64_t paddr, uint32_t *tag);
/* RX_IND: the next buffer in ring order. 0, or ENOENT when none. */
int htt_ring_next(struct htt_ring *r, uint8_t *ring, uint64_t *paddr,
	uint32_t *tag);

#endif /* _WCN_HTT_H */
