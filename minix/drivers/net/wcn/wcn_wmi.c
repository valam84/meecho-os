/*
 * WMI-TLV commands and events (wcn_wmi.h).  The layouts are Linux's
 * ath10k structures, measured by a compiler; the values the commands
 * carry are what ath10k puts there for a WCN3990 with the services this
 * tablet's firmware announces (port/clover/wifi/htc-wmi.md, 4.4;
 * port/clover/wifi/sta.md).  All words little-endian.
 */

#include <errno.h>
#include <string.h>

#include "wcn_wmi.h"

/* ---- writing ---- */

static void
put(struct wmi_buf *b, const void *v, size_t n)
{

	if (b->err || b->cap - b->len < n) {
		b->err = 1;
		return;
	}
	memcpy(b->p + b->len, v, n);
	b->len += n;
}

static void
zero(struct wmi_buf *b, size_t n)
{

	if (b->err || b->cap - b->len < n) {
		b->err = 1;
		return;
	}
	memset(b->p + b->len, 0, n);
	b->len += n;
}

static void
put16(struct wmi_buf *b, uint16_t v)
{
	uint8_t x[2] = { v, v >> 8 };

	put(b, x, 2);
}

static void
put32(struct wmi_buf *b, uint32_t v)
{
	uint8_t x[4] = { v, v >> 8, v >> 16, v >> 24 };

	put(b, x, 4);
}

static void
put64(struct wmi_buf *b, uint64_t v)
{

	put32(b, (uint32_t)v);
	put32(b, (uint32_t)(v >> 32));
}

/* wmi_mac_addr: six bytes and two of padding. */
static void
putmac(struct wmi_buf *b, const uint8_t *mac)
{

	if (mac != NULL)
		put(b, mac, 6);
	else
		zero(b, 6);
	zero(b, 2);
}

static void
begin(struct wmi_buf *b, uint32_t id)
{

	b->len = 0;
	b->err = 0;
	put32(b, id);
}

static size_t
end(struct wmi_buf *b)
{

	return b->err ? 0 : b->len;
}

/* A TLV whose value is written next: its header, the length patched by close. */
static size_t
tlv_open(struct wmi_buf *b, uint16_t tag)
{

	put16(b, 0);
	put16(b, tag);
	return b->len;
}

static void
tlv_close(struct wmi_buf *b, size_t at)
{
	size_t n;

	if (b->err)
		return;
	n = b->len - at;
	if (n > 0xffff) {
		b->err = 1;
		return;
	}
	b->p[at - 4] = n;
	b->p[at - 3] = n >> 8;
}

/* An array of bytes, zero-padded to a multiple of four. */
static void
tlv_bytes(struct wmi_buf *b, const uint8_t *v, size_t n)
{
	size_t at;

	at = tlv_open(b, WMI_TAG_ARRAY_BYTE);
	if (n > 0)
		put(b, v, n);
	zero(b, (4 - (n & 3)) & 3);
	tlv_close(b, at);
}

static size_t
round4(size_t n)
{

	return (n + 3) & ~(size_t)3;
}

/*
 * INIT's resource configuration, word by word in the order of
 * wmi_tlv_resource_config: ath10k's values for WCN3990 (hw_params:
 * 33 peers, AST skid 16, 2 WDS entries; TLV targets: 4 vdevs, 1056 MSDU
 * descriptors, 22 WoW patterns).  Words 2, 3 and 43 depend on services
 * and are set in wmi_init.
 */
static const uint32_t resource_cfg[44] = {
	4,		/*  0 num_vdevs */
	33,		/*  1 num_peers */
	0,		/*  2 num_offload_peers: 4 with RX_FULL_REORDER */
	0,		/*  3 num_offload_reorder_bufs: likewise */
	2,		/*  4 num_peer_keys */
	66,		/*  5 num_tids: two per peer */
	16,		/*  6 ast_skid_limit */
	7,		/*  7 tx_chain_mask */
	7,		/*  8 rx_chain_mask */
	100, 100, 100,	/*  9-11 rx_timeout_pri[0..2], ms */
	40,		/* 12 rx_timeout_pri[3] */
	WMI_TXRX_NATIVE_WIFI,	/* 13 rx_decap_mode */
	4,		/* 14 scan_max_pending_reqs */
	4,		/* 15 bmiss_offload_max_vdev */
	4,		/* 16 roam_offload_max_vdev */
	8,		/* 17 roam_offload_max_ap_profiles */
	0, 0, 0,	/* 18-20 multicast groups, table, mcast2ucast */
	0x400,		/* 21 tx_dbg_log_size */
	2,		/* 22 num_wds_entries */
	0, 0, 0, 0,	/* 23-26 dma burst, aggr delim, defrag check, vow */
	2,		/* 27 gtk_offload_max_vdev */
	1056,		/* 28 num_msdu_desc */
	2,		/* 29 max_frag_entries */
	1,		/* 30 num_tdls_vdevs */
	0x20,		/* 31 num_tdls_conn_table_entries */
	2,		/* 32 beacon_tx_offload_max_vdev */
	5,		/* 33 num_multicast_filter_entries */
	22,		/* 34 num_wow_filters */
	6,		/* 35 num_keep_alive_pattern */
	0,		/* 36 keep_alive_pattern_size */
	1, 1,		/* 37-38 max TDLS sleep / buffer STA */
	0,		/* 39 wmi_send_separate */
	0, 0, 0,	/* 40-42 OCB vdevs, channels, schedules */
	0x200,		/* 43 host_capab: MGMT_BUNDLE_TX_COMPL */
};

#define CAPAB_TX_ACK_RSSI	0x40000		/* host_capab bit 18 */

size_t
wmi_init(struct wmi_buf *b, unsigned flags, const struct wmi_mem_chunk *c,
	unsigned n)
{
	size_t at, arr;
	unsigned i;
	uint32_t v;

	if (n > WMI_MAX_MEM_REQS || (n > 0 && c == NULL))
		return 0;
	begin(b, WMI_CMD_INIT);
	at = tlv_open(b, WMI_TAG_INIT_CMD);
	put32(b, WMI_ABI_VER0);
	put32(b, WMI_ABI_VER1);
	put32(b, WMI_ABI_NS0);
	put32(b, WMI_ABI_NS1);
	put32(b, WMI_ABI_NS2);
	put32(b, WMI_ABI_NS3);
	put32(b, n);			/* num_host_mem_chunks */
	tlv_close(b, at);

	at = tlv_open(b, WMI_TAG_RESOURCE_CONFIG);
	for (i = 0; i < 44; i++) {
		v = resource_cfg[i];
		if ((i == 2 || i == 3) && (flags & WMI_INIT_RX_REORDER))
			v = 4;		/* = num_vdevs */
		if (i == 43 && (flags & WMI_INIT_TX_ACK_RSSI))
			v |= CAPAB_TX_ACK_RSSI;
		put32(b, v);
	}
	tlv_close(b, at);

	/* The chunks: an array of TLVs, present (empty) with none. */
	arr = tlv_open(b, WMI_TAG_ARRAY_STRUCT);
	for (i = 0; i < n; i++) {
		/*
		 * Without SUPPORT_EXTEND_ADDRESS the firmware reads 32 bits
		 * of address; ath10k sends the low half of a higher one, this
		 * refuses it.
		 */
		if (!(flags & WMI_INIT_EXT_ADDR) && (c[i].paddr >> 32) != 0) {
			b->err = 1;
			break;
		}
		at = tlv_open(b, WMI_TAG_HOST_MEMORY_CHUNK);
		put32(b, c[i].req_id);
		put32(b, (uint32_t)c[i].paddr);
		put32(b, c[i].len);
		put32(b, (flags & WMI_INIT_EXT_ADDR) ?
		    (uint32_t)(c[i].paddr >> 32) : 0);	/* ptr_high */
		tlv_close(b, at);
	}
	tlv_close(b, arr);
	return end(b);
}

size_t
wmi_pdev_set_param(struct wmi_buf *b, uint32_t id, uint32_t value)
{
	size_t at;

	begin(b, WMI_CMD_PDEV_SET_PARAM);
	at = tlv_open(b, WMI_TAG_PDEV_SET_PARAM_CMD);
	put32(b, 0);			/* pdev_id */
	put32(b, id);
	put32(b, value);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_pdev_set_regdomain(struct wmi_buf *b, uint32_t rd, uint32_t rd2g,
	uint32_t rd5g, uint32_t ctl2g, uint32_t ctl5g)
{
	size_t at;

	/* The TLV form carries no DFS region. */
	begin(b, WMI_CMD_PDEV_SET_REGDOMAIN);
	at = tlv_open(b, WMI_TAG_PDEV_SET_REGDOMAIN_CMD);
	put32(b, 0);			/* pdev_id */
	put32(b, rd);
	put32(b, rd2g);
	put32(b, rd5g);
	put32(b, ctl2g);
	put32(b, ctl5g);
	tlv_close(b, at);
	return end(b);
}

/* struct wmi_channel, 24 bytes. */
static void
put_chan(struct wmi_buf *b, const struct wmi_chan *c)
{
	uint8_t pw[8];

	if (c->mode > WMI_CHAN_MODE_MASK ||
	    (c->flags & WMI_CHAN_MODE_MASK) != 0) {
		b->err = 1;
		return;
	}
	put32(b, c->freq);
	put32(b, c->center1);
	put32(b, c->center2);
	put32(b, c->mode | c->flags);
	pw[0] = c->min_power;
	pw[1] = c->max_power;
	pw[2] = c->reg_power;
	pw[3] = 0;			/* reg_classid: ath10k leaves 0 */
	pw[4] = c->antenna_max;
	pw[5] = c->max_power;		/* max_tx_power: ath10k copies max_power */
	pw[6] = pw[7] = 0;
	put(b, pw, 8);
}

size_t
wmi_scan_chan_list(struct wmi_buf *b, const struct wmi_chan *c, unsigned n)
{
	size_t at, arr;
	unsigned i;

	if (n > 0 && c == NULL)
		return 0;
	begin(b, WMI_CMD_SCAN_CHAN_LIST);
	at = tlv_open(b, WMI_TAG_SCAN_CHAN_LIST_CMD);
	put32(b, n);
	tlv_close(b, at);
	arr = tlv_open(b, WMI_TAG_ARRAY_STRUCT);
	for (i = 0; i < n; i++) {
		at = tlv_open(b, WMI_TAG_CHANNEL);
		put_chan(b, &c[i]);
		tlv_close(b, at);
	}
	tlv_close(b, arr);
	return end(b);
}

void
wmi_scan_defaults(struct wmi_scan *s)
{
	static const uint8_t bcast[1][6] = {
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };

	memset(s, 0, sizeof(*s));
	s->scan_id = 0;			/* ATH10K_SCAN_ID */
	s->req_id = 1;
	s->priority = WMI_SCAN_PRIORITY_LOW;
	s->notify = WMI_SCAN_EV_STARTED | WMI_SCAN_EV_COMPLETED |
	    WMI_SCAN_EV_BSS_CHANNEL | WMI_SCAN_EV_FOREIGN_CHANNEL |
	    WMI_SCAN_EV_FOREIGN_EXIT | WMI_SCAN_EV_DEQUEUED;
	s->dwell_active = 50;
	s->dwell_passive = 150;
	s->min_rest = 50;
	s->max_rest = 500;
	s->repeat_probe = 0;
	s->probe_spacing = 0;
	s->idle_time = 0;
	s->max_scan_time = 20000;	/* the host waits this + 200 ms */
	s->probe_delay = 5;
	s->flags = WMI_SCAN_CHAN_STAT_EVENT;
	s->bssids = bcast;
	s->nbssid = 1;
}

size_t
wmi_start_scan(struct wmi_buf *b, const struct wmi_scan *s)
{
	size_t at;
	unsigned i;

	if (s->nchan > WMI_SCAN_MAX_CHANNELS ||
	    s->nssid > WMI_SCAN_MAX_SSIDS ||
	    s->nbssid > WMI_SCAN_MAX_BSSIDS ||
	    s->ie_len > WMI_SCAN_MAX_IE ||
	    s->scan_id > WMI_SCAN_ID_MAX || s->req_id > WMI_SCAN_ID_MAX ||
	    (s->nchan > 0 && s->freqs == NULL) ||
	    (s->nssid > 0 && s->ssids == NULL) ||
	    (s->nbssid > 0 && s->bssids == NULL) ||
	    (s->ie_len > 0 && s->ie == NULL))
		return 0;
	for (i = 0; i < s->nssid; i++)
		if (s->ssids[i].len > WMI_SSID_MAX)
			return 0;

	begin(b, WMI_CMD_START_SCAN);
	at = tlv_open(b, WMI_TAG_START_SCAN_CMD);
	put32(b, WMI_SCAN_ID_PREFIX | s->scan_id);
	put32(b, WMI_SCAN_ID_PREFIX | s->req_id);
	put32(b, s->vdev_id);
	put32(b, s->priority);
	put32(b, s->notify);
	put32(b, s->dwell_active);
	put32(b, s->dwell_passive);
	put32(b, s->min_rest);
	put32(b, s->max_rest);
	put32(b, s->repeat_probe);
	put32(b, s->probe_spacing);
	put32(b, s->idle_time);
	put32(b, s->max_scan_time);
	put32(b, s->probe_delay);
	/* WMI-TLV firmware reads FILTER_PROBE_REQ the other way round. */
	put32(b, s->flags ^ WMI_SCAN_FILTER_PROBE_REQ);
	put32(b, s->burst_duration);
	put32(b, s->nchan);
	put32(b, s->nbssid);		/* BSSIDs before SSIDs here... */
	put32(b, s->nssid);
	put32(b, (uint32_t)s->ie_len);
	put32(b, 3);			/* num_probes, always */
	putmac(b, s->mac_addr);
	putmac(b, s->mac_mask);
	tlv_close(b, at);

	at = tlv_open(b, WMI_TAG_ARRAY_UINT32);
	for (i = 0; i < s->nchan; i++)
		put32(b, s->freqs[i]);
	tlv_close(b, at);

	at = tlv_open(b, WMI_TAG_ARRAY_FIXED_STRUCT);	/* ...SSIDs first here */
	for (i = 0; i < s->nssid; i++) {
		put32(b, s->ssids[i].len);
		put(b, s->ssids[i].ssid, s->ssids[i].len);
		zero(b, WMI_SSID_MAX - s->ssids[i].len);
	}
	tlv_close(b, at);

	at = tlv_open(b, WMI_TAG_ARRAY_FIXED_STRUCT);
	for (i = 0; i < s->nbssid; i++)
		putmac(b, s->bssids[i]);
	tlv_close(b, at);

	tlv_bytes(b, s->ie, s->ie_len);
	return end(b);
}

size_t
wmi_stop_scan(struct wmi_buf *b, uint32_t req_type, uint32_t scan_id,
	uint32_t req_id, uint32_t vdev_id)
{
	size_t at;

	if (req_id > WMI_SCAN_ID_MAX ||
	    (req_type == WMI_SCAN_STOP_ONE && scan_id > WMI_SCAN_ID_MAX))
		return 0;
	begin(b, WMI_CMD_STOP_SCAN);
	at = tlv_open(b, WMI_TAG_STOP_SCAN_CMD);
	put32(b, WMI_SCAN_ID_PREFIX | req_id);
	put32(b, WMI_SCAN_ID_PREFIX | scan_id);
	put32(b, req_type);
	/*
	 * ath10k takes scan_id and vdev_id from one union, so for STOP_ONE
	 * it sends vdev_id = scan_id (0 = 0 for its one scan).  Here the
	 * caller says both.
	 */
	put32(b, vdev_id);
	tlv_close(b, at);
	return end(b);
}

/* Commands that are one TLV of words: id, tag, words. */
static size_t
words(struct wmi_buf *b, uint32_t id, uint16_t tag, const uint32_t *w,
	unsigned n)
{
	size_t at;
	unsigned i;

	begin(b, id);
	at = tlv_open(b, tag);
	for (i = 0; i < n; i++)
		put32(b, w[i]);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_vdev_create(struct wmi_buf *b, uint32_t vdev_id, uint32_t type,
	uint32_t subtype, const uint8_t mac[6])
{
	size_t at;

	begin(b, WMI_CMD_VDEV_CREATE);
	at = tlv_open(b, WMI_TAG_VDEV_CREATE_CMD);
	put32(b, vdev_id);
	put32(b, type);
	put32(b, subtype);
	putmac(b, mac);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_vdev_delete(struct wmi_buf *b, uint32_t vdev_id)
{

	return words(b, WMI_CMD_VDEV_DELETE, WMI_TAG_VDEV_DELETE_CMD,
	    &vdev_id, 1);
}

size_t
wmi_vdev_set_param(struct wmi_buf *b, uint32_t vdev_id, uint32_t id,
	uint32_t value)
{
	uint32_t w[3] = { vdev_id, id, value };

	return words(b, WMI_CMD_VDEV_SET_PARAM, WMI_TAG_VDEV_SET_PARAM_CMD,
	    w, 3);
}

size_t
wmi_vdev_start(struct wmi_buf *b, const struct wmi_vdev_start *v)
{
	size_t at;

	if (v->ssid_len > WMI_SSID_MAX ||
	    (v->ssid_len > 0 && v->ssid == NULL) ||
	    ((v->flags & WMI_VDEV_START_HIDDEN_SSID) && v->ssid == NULL))
		return 0;
	begin(b, WMI_CMD_VDEV_START_REQUEST);
	at = tlv_open(b, WMI_TAG_VDEV_START_REQUEST_CMD);
	put32(b, v->vdev_id);
	put32(b, 0);			/* requestor_id */
	put32(b, v->bcn_intval);
	put32(b, v->dtim_period);
	put32(b, v->flags);
	put32(b, v->ssid != NULL ? (uint32_t)v->ssid_len : 0);
	if (v->ssid != NULL)
		put(b, v->ssid, v->ssid_len);
	zero(b, WMI_SSID_MAX - (v->ssid != NULL ? v->ssid_len : 0));
	put32(b, 0);			/* bcn_tx_rate */
	put32(b, 0);			/* bcn_tx_power */
	put32(b, 0);			/* num_noa_descr */
	put32(b, 0);			/* disable_hw_ack */
	tlv_close(b, at);
	at = tlv_open(b, WMI_TAG_CHANNEL);
	put_chan(b, &v->chan);
	tlv_close(b, at);
	at = tlv_open(b, WMI_TAG_ARRAY_STRUCT);	/* NoA descriptors: none */
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_vdev_stop(struct wmi_buf *b, uint32_t vdev_id)
{

	return words(b, WMI_CMD_VDEV_STOP, WMI_TAG_VDEV_STOP_CMD, &vdev_id, 1);
}

size_t
wmi_vdev_up(struct wmi_buf *b, uint32_t vdev_id, uint32_t aid,
	const uint8_t bssid[6])
{
	size_t at;

	begin(b, WMI_CMD_VDEV_UP);
	at = tlv_open(b, WMI_TAG_VDEV_UP_CMD);
	put32(b, vdev_id);
	put32(b, aid);
	putmac(b, bssid);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_vdev_down(struct wmi_buf *b, uint32_t vdev_id)
{

	return words(b, WMI_CMD_VDEV_DOWN, WMI_TAG_VDEV_DOWN_CMD, &vdev_id, 1);
}

size_t
wmi_peer_create(struct wmi_buf *b, uint32_t vdev_id, const uint8_t mac[6],
	uint32_t type)
{
	size_t at;

	begin(b, WMI_CMD_PEER_CREATE);
	at = tlv_open(b, WMI_TAG_PEER_CREATE_CMD);
	put32(b, vdev_id);
	putmac(b, mac);
	put32(b, type);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_peer_delete(struct wmi_buf *b, uint32_t vdev_id, const uint8_t mac[6])
{
	size_t at;

	begin(b, WMI_CMD_PEER_DELETE);
	at = tlv_open(b, WMI_TAG_PEER_DELETE_CMD);
	put32(b, vdev_id);
	putmac(b, mac);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_peer_set_param(struct wmi_buf *b, uint32_t vdev_id, const uint8_t mac[6],
	uint32_t id, uint32_t value)
{
	size_t at;

	begin(b, WMI_CMD_PEER_SET_PARAM);
	at = tlv_open(b, WMI_TAG_PEER_SET_PARAM_CMD);
	put32(b, vdev_id);
	putmac(b, mac);
	put32(b, id);
	put32(b, value);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_peer_assoc(struct wmi_buf *b, const struct wmi_peer_assoc *a)
{
	size_t at;

	if (a->mpdu_density > 16 || a->nlegacy > WMI_MAX_RATES ||
	    a->nht > WMI_MAX_RATES ||
	    (a->nlegacy > 0 && a->legacy == NULL) ||
	    (a->nht > 0 && a->ht == NULL))
		return 0;
	begin(b, WMI_CMD_PEER_ASSOC);
	at = tlv_open(b, WMI_TAG_PEER_ASSOC_CMD);
	putmac(b, a->mac);
	put32(b, a->vdev_id);
	put32(b, a->reassoc ? 0 : 1);	/* new_assoc */
	put32(b, a->aid);
	put32(b, a->flags);
	put32(b, a->caps);
	put32(b, a->listen_intval);
	put32(b, a->ht_caps);
	put32(b, a->max_mpdu);
	put32(b, a->mpdu_density);
	put32(b, a->rate_caps);
	put32(b, a->nss);
	put32(b, a->vht_caps);
	put32(b, a->phy_mode);
	put32(b, a->ht_info[0]);
	put32(b, a->ht_info[1]);
	put32(b, a->nlegacy);
	put32(b, a->nht);
	tlv_close(b, at);
	tlv_bytes(b, a->legacy, a->nlegacy);
	tlv_bytes(b, a->ht, a->nht);
	at = tlv_open(b, WMI_TAG_VHT_RATE_SET);
	put32(b, a->vht_rx_max);
	put32(b, a->vht_rx_mcs);
	put32(b, a->vht_tx_max);
	put32(b, a->vht_tx_mcs);
	tlv_close(b, at);
	return end(b);
}

size_t
wmi_vdev_install_key(struct wmi_buf *b, const struct wmi_key *k)
{
	size_t at;

	/* ath10k's two checks, then the limits of wmi.h it does not check. */
	if ((k->cipher == WMI_CIPHER_NONE) != (k->key == NULL) ||
	    k->idx > WMI_MAX_KEY_INDEX || k->key_len > WMI_MAX_KEY_LEN ||
	    (k->cipher == WMI_CIPHER_NONE && k->key_len != 0))
		return 0;
	begin(b, WMI_CMD_VDEV_INSTALL_KEY);
	at = tlv_open(b, WMI_TAG_VDEV_INSTALL_KEY_CMD);
	put32(b, k->vdev_id);
	putmac(b, k->mac);
	put32(b, k->idx);
	put32(b, k->flags);
	put32(b, k->cipher);
	/*
	 * RSC, global RSC, TSC (8 bytes each) and the two WPI counters
	 * (16 each): ath10k leaves them all zero.
	 */
	zero(b, 8 + 8 + 8 + 16 + 16);
	put32(b, k->key_len);
	put32(b, k->txmic_len);
	put32(b, k->rxmic_len);
	tlv_close(b, at);
	tlv_bytes(b, k->key, k->key_len);
	return end(b);
}

/* An 802.11 frame the firmware takes this way: management or a Null. */
static int
mgmt_or_null(const uint8_t *f)
{
	unsigned type = (f[0] >> 2) & 3, sub = f[0] >> 4;

	if ((f[0] & 3) != 0)		/* protocol version */
		return 0;
	return type == 0 || (type == 2 && (sub == 4 || sub == 12));
}

size_t
wmi_mgmt_tx(struct wmi_buf *b, uint32_t vdev_id, uint32_t desc_id,
	uint32_t chan_freq, uint64_t paddr, const uint8_t *frame,
	size_t frame_len)
{
	size_t at, n;

	if (frame == NULL || frame_len < 24 || frame_len > 0xffff ||
	    desc_id >= WMI_MGMT_TX_MAX_PENDING || !mgmt_or_null(frame))
		return 0;
	n = frame_len < WMI_MGMT_TX_INLINE ? frame_len : WMI_MGMT_TX_INLINE;
	begin(b, WMI_CMD_MGMT_TX_SEND);
	at = tlv_open(b, WMI_TAG_MGMT_TX_CMD);
	put32(b, vdev_id);
	put32(b, desc_id);
	put32(b, chan_freq);		/* 0: the vdev's channel */
	put64(b, paddr);
	put32(b, (uint32_t)frame_len);
	/*
	 * buf_len: the inline copy rounded up to four, as ath10k (which
	 * copies past the end of a short frame; here the padding is zero).
	 */
	put32(b, (uint32_t)round4(n));
	tlv_close(b, at);
	tlv_bytes(b, frame, n);
	return end(b);
}

size_t
wmi_sta_ps_mode(struct wmi_buf *b, uint32_t vdev_id, uint32_t mode)
{
	uint32_t w[2] = { vdev_id, mode };

	return words(b, WMI_CMD_STA_POWERSAVE_MODE,
	    WMI_TAG_STA_POWERSAVE_MODE_CMD, w, 2);
}

size_t
wmi_sta_ps_param(struct wmi_buf *b, uint32_t vdev_id, uint32_t id,
	uint32_t value)
{
	uint32_t w[3] = { vdev_id, id, value };

	return words(b, WMI_CMD_STA_POWERSAVE_PARAM,
	    WMI_TAG_STA_POWERSAVE_PARAM_CMD, w, 3);
}

/* ---- reading ---- */

static uint16_t
get16(const uint8_t *p)
{

	return p[0] | p[1] << 8;
}

static uint32_t
get32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

int
wmi_event(const uint8_t *buf, size_t len, struct wmi_ev *ev)
{

	if (buf == NULL || len < WMI_HDR)
		return EINVAL;
	ev->id = get32(buf) & WMI_ID_MASK;
	ev->tlv = buf + WMI_HDR;
	ev->len = len - WMI_HDR;
	return 0;
}

/*
 * The minimum length of a known structure (Linux's policy table, those
 * of it whose sizes this file knows); 0 for the rest.
 */
static size_t
min_len(unsigned tag)
{

	switch (tag) {
	case WMI_TAG_SERVICE_READY_EVENT:	return 104;
	case WMI_TAG_HAL_REG_CAPABILITIES:	return 36;
	case WMI_TAG_WLAN_HOST_MEM_REQ:		return 16;
	case WMI_TAG_READY_EVENT:		return 36;
	case WMI_TAG_SCAN_EVENT:		return 24;
	case WMI_TAG_VDEV_START_RESPONSE:	return 16;
	case WMI_TAG_PEER_STA_KICKOUT:		return 8;
	case WMI_TAG_MGMT_RX_HDR:		return 40;
	}
	return 0;
}

void
wmi_tlv_begin(struct wmi_tlv_it *it, const uint8_t *p, size_t len)
{

	it->p = p;
	it->left = len;
}

int
wmi_tlv_next(struct wmi_tlv_it *it, unsigned *tag, const uint8_t **v,
	size_t *vlen)
{
	size_t n;
	unsigned t;

	if (it->left == 0)
		return 0;
	if (it->left < WMI_TLV_HDR)
		return -1;
	n = get16(it->p);		/* the length comes first */
	t = get16(it->p + 2);
	if (n > it->left - WMI_TLV_HDR || n < min_len(t))
		return -1;
	*tag = t;
	*v = it->p + WMI_TLV_HDR;
	*vlen = n;
	it->p += WMI_TLV_HDR + n;
	it->left -= WMI_TLV_HDR + n;
	return 1;
}

/* The whole event well formed: 0, or -1. */
static int
walk(const struct wmi_ev *ev)
{
	struct wmi_tlv_it it;
	const uint8_t *v;
	size_t n;
	unsigned t;
	int r;

	wmi_tlv_begin(&it, ev->tlv, ev->len);
	while ((r = wmi_tlv_next(&it, &t, &v, &n)) == 1)
		;
	return r;
}

int
wmi_tlv_find(const struct wmi_ev *ev, unsigned tag, unsigned nth,
	const uint8_t **v, size_t *vlen)
{
	struct wmi_tlv_it it;
	const uint8_t *p, *fv = NULL;
	size_t n, fn = 0;
	unsigned t;
	int r;

	wmi_tlv_begin(&it, ev->tlv, ev->len);
	while ((r = wmi_tlv_next(&it, &t, &p, &n)) == 1) {
		if (t == tag && fv == NULL && nth-- == 0) {
			fv = p;
			fn = n;
		}
	}
	if (r < 0)
		return -1;	/* malformed anywhere: nothing of it is trusted */
	if (fv == NULL)
		return 0;
	*v = fv;
	*vlen = fn;
	return 1;
}

/* The first TLV with this tag, at least min bytes: 0, or EINVAL. */
static int
need(const struct wmi_ev *ev, unsigned tag, size_t min, const uint8_t **v,
	size_t *n)
{

	if (wmi_tlv_find(ev, tag, 0, v, n) != 1 || *n < min)
		return EINVAL;
	return 0;
}

int
wmi_svc_enabled(const struct wmi_svc *s, unsigned id)
{

	/*
	 * SERVICE_READY uses four bits of every 32-bit word: word id / 4,
	 * bit id % 4, valid while id < the map's length in bytes (Linux's
	 * bound, odd as it is).
	 */
	if (id < WMI_SVC_MAX)
		return id < s->len && (s->w[id / 4] >> (id % 4) & 1);
	/* SERVICE_AVAILABLE: all 32 bits, from service 128 on. */
	if (id < WMI_SVC_EXT_MAX) {
		id -= WMI_SVC_MAX;
		return id / 32 < s->ext_words &&
		    (s->ext[id / 32] >> (id % 32) & 1);
	}
	return 0;
}

int
wmi_svc_ready(const struct wmi_ev *ev, struct wmi_svc_ready *r,
	struct wmi_svc *svc)
{
	struct wmi_tlv_it it;
	const uint8_t *e, *reg, *map, *mem, *v;
	size_t ne, nreg, nmap, nmem, n;
	unsigned t, i;
	int k;

	if (walk(ev) < 0 ||
	    need(ev, WMI_TAG_SERVICE_READY_EVENT, 104, &e, &ne) ||
	    need(ev, WMI_TAG_HAL_REG_CAPABILITIES, 36, &reg, &nreg) ||
	    need(ev, WMI_TAG_ARRAY_UINT32, 0, &map, &nmap) ||
	    need(ev, WMI_TAG_ARRAY_STRUCT, 0, &mem, &nmem))
		return EINVAL;
	/* The second ARRAY_UINT32 (DBS hardware modes) is not used. */

	if (get32(e + 4) != WMI_ABI_VER0 || get32(e + 12) != WMI_ABI_NS0 ||
	    get32(e + 16) != WMI_ABI_NS1 || get32(e + 20) != WMI_ABI_NS2 ||
	    get32(e + 24) != WMI_ABI_NS3)
		return ENOTSUP;		/* abi_ver1 is not compared, as Linux */

	memset(r, 0, sizeof(*r));
	r->fw_build = get32(e + 0);
	/*
	 * The firmware version ath10k prints: abi_ver0 >> 24, abi_ver0 &
	 * 0xffffff, abi_ver1 >> 16, abi_ver1 & 0xffff (1.0.0.571 on
	 * clover, so abi_ver1 0x23b there [?]).
	 */
	r->abi_ver0 = get32(e + 4);
	r->abi_ver1 = get32(e + 8);
	r->phy_capability = get32(e + 28);
	r->max_frag_entry = get32(e + 32);
	r->num_rf_chains = get32(e + 36);
	r->ht_cap_info = get32(e + 40);
	r->vht_cap_info = get32(e + 44);
	r->vht_supp_mcs = get32(e + 48);
	r->hw_min_tx_power = get32(e + 52);
	r->hw_max_tx_power = get32(e + 56);
	r->sys_cap_info = get32(e + 60);
	r->max_bcn_ie_size = get32(e + 68);
	r->num_mem_reqs = get32(e + 72);
	r->max_num_scan_chans = get32(e + 76);
	r->hw_bd_id = get32(e + 80);

	r->eeprom_rd = get32(reg + 0);
	r->eeprom_rd_ext = get32(reg + 4);
	r->regcap1 = get32(reg + 8);
	r->regcap2 = get32(reg + 12);
	r->wireless_modes = get32(reg + 16);
	r->low_2ghz_chan = get32(reg + 20);
	r->high_2ghz_chan = get32(reg + 24);
	r->low_5ghz_chan = get32(reg + 28);
	r->high_5ghz_chan = get32(reg + 32);

	/*
	 * The memory requests: TLVs of WLAN_HOST_MEM_REQ and nothing else
	 * inside the array.  Linux trusts the header's count and reads
	 * past the requests it found; here the count must be covered.
	 */
	i = 0;
	wmi_tlv_begin(&it, mem, nmem);
	while ((k = wmi_tlv_next(&it, &t, &v, &n)) == 1) {
		if (t != WMI_TAG_WLAN_HOST_MEM_REQ || i == WMI_MAX_MEM_REQS)
			return EINVAL;
		r->mem_req[i].req_id = get32(v + 0);
		r->mem_req[i].unit_size = get32(v + 4);
		r->mem_req[i].num_unit_info = get32(v + 8);
		r->mem_req[i].num_units = get32(v + 12);
		i++;
	}
	if (k < 0 || r->num_mem_reqs > i)
		return EINVAL;

	/* A map that is not whole words would be read past its end. */
	if (nmap % 4 != 0)
		return EINVAL;
	memset(svc->w, 0, sizeof(svc->w));
	svc->len = nmap;
	for (i = 0; i < WMI_SVC_WORDS && i < nmap / 4; i++)
		svc->w[i] = get32(map + 4 * i);
	return 0;
}

int
wmi_svc_available(const struct wmi_ev *ev, struct wmi_svc *svc)
{
	const uint8_t *v;
	size_t n;
	unsigned i, have;

	/*
	 * TLV 0x22f: a le32 Linux calls the map's length and does not use
	 * [?], then the words of services 128 and on.
	 */
	if (walk(ev) < 0 ||
	    need(ev, WMI_TAG_SERVICE_AVAILABLE_EVENT, 4, &v, &n))
		return EINVAL;
	have = (n - 4) / 4;
	memset(svc->ext, 0, sizeof(svc->ext));
	svc->ext_words = have < WMI_SVC_EXT_WORDS ? have : WMI_SVC_EXT_WORDS;
	for (i = 0; i < svc->ext_words; i++)
		svc->ext[i] = get32(v + 4 + 4 * i);
	return 0;
}

int
wmi_ready(const struct wmi_ev *ev, struct wmi_ready *r)
{
	const uint8_t *v;
	size_t n;

	if (walk(ev) < 0 || need(ev, WMI_TAG_READY_EVENT, 36, &v, &n))
		return EINVAL;
	r->abi_ver0 = get32(v + 0);
	r->abi_ver1 = get32(v + 4);
	memcpy(r->mac, v + 24, 6);
	r->status = get32(v + 32);
	return 0;
}

int
wmi_scan_event(const struct wmi_ev *ev, struct wmi_scan_ev *s)
{
	const uint8_t *v;
	size_t n;

	if (walk(ev) < 0 || need(ev, WMI_TAG_SCAN_EVENT, 24, &v, &n))
		return EINVAL;
	s->type = get32(v + 0);
	s->reason = get32(v + 4);
	s->freq = get32(v + 8);
	s->req_id = get32(v + 12);
	s->scan_id = get32(v + 16);
	s->vdev_id = get32(v + 20);
	return 0;
}

int
wmi_mgmt_rx(const struct wmi_ev *ev, struct wmi_mgmt_rx *m)
{
	const uint8_t *h, *f;
	size_t nh, nf;
	unsigned i;

	if (walk(ev) < 0 || need(ev, WMI_TAG_MGMT_RX_HDR, 40, &h, &nh) ||
	    need(ev, WMI_TAG_ARRAY_BYTE, 0, &f, &nf))
		return EINVAL;
	m->channel = get32(h + 0);
	m->snr = get32(h + 4);
	m->rate = get32(h + 8);
	m->phy_mode = get32(h + 12);
	m->status = get32(h + 20);
	for (i = 0; i < 4; i++)
		m->rssi[i] = get32(h + 24 + 4 * i);
	/*
	 * buf_len must lie inside the byte array.  Linux only checks it
	 * against the end of the event [?: a firmware that pads the array
	 * shorter than buf_len would be refused here].
	 */
	if (get32(h + 16) > nf)
		return EINVAL;
	m->frame = f;
	m->len = get32(h + 16);
	return 0;
}

int
wmi_vdev_start_resp(const struct wmi_ev *ev, struct wmi_vdev_start_resp *r)
{
	const uint8_t *v;
	size_t n;

	if (walk(ev) < 0 ||
	    need(ev, WMI_TAG_VDEV_START_RESPONSE, 16, &v, &n))
		return EINVAL;
	r->vdev_id = get32(v + 0);
	r->req_id = get32(v + 4);
	r->resp_type = get32(v + 8);
	r->status = get32(v + 12);
	return 0;
}

int
wmi_mgmt_tx_compl(const struct wmi_ev *ev, struct wmi_tx_compl *c)
{
	const uint8_t *v;
	size_t n;

	/* No minimum in Linux's table; four words are read, a fifth if there. */
	if (walk(ev) < 0 || need(ev, WMI_TAG_MGMT_TX_COMPL_EVENT, 16, &v, &n))
		return EINVAL;
	c->desc_id = get32(v + 0);
	c->status = get32(v + 4);
	c->pdev_id = get32(v + 8);
	c->ppdu_id = get32(v + 12);
	c->has_ack_rssi = n >= 20;
	c->ack_rssi = c->has_ack_rssi ? get32(v + 16) : 0;
	return 0;
}

int
wmi_mgmt_tx_bundle(const struct wmi_ev *ev, struct wmi_tx_compl *c,
	unsigned max, unsigned *n)
{
	const uint8_t *v, *arr[4] = { NULL, NULL, NULL, NULL };
	size_t nv, len[4] = { 0, 0, 0, 0 };
	uint32_t num;
	unsigned i, k;

	/*
	 * The count, then ARRAY_UINT32s in order: desc_ids, status,
	 * ppdu_ids, ack_rssi.  Linux reads the status from the desc_ids
	 * array (a slip) and checks no array against the count; here the
	 * first two must cover it, the others are used where they do.
	 */
	if (walk(ev) < 0 ||
	    need(ev, WMI_TAG_MGMT_TX_COMPL_BUNDLE, 4, &v, &nv))
		return EINVAL;
	num = get32(v);
	for (k = 0; k < 4; k++)
		if (wmi_tlv_find(ev, WMI_TAG_ARRAY_UINT32, k, &arr[k],
		    &len[k]) != 1)
			arr[k] = NULL;
	if (arr[0] == NULL || arr[1] == NULL ||
	    len[0] / 4 < num || len[1] / 4 < num)
		return EINVAL;
	if (num > max)
		return ENOSPC;
	for (i = 0; i < num; i++) {
		memset(&c[i], 0, sizeof(c[i]));
		c[i].desc_id = get32(arr[0] + 4 * i);
		c[i].status = get32(arr[1] + 4 * i);
		if (arr[2] != NULL && len[2] / 4 >= num)
			c[i].ppdu_id = get32(arr[2] + 4 * i);
		if (arr[3] != NULL && len[3] / 4 >= num) {
			c[i].ack_rssi = get32(arr[3] + 4 * i);
			c[i].has_ack_rssi = 1;
		}
	}
	*n = num;
	return 0;
}

int
wmi_install_key_done(const struct wmi_ev *ev, struct wmi_key_done *k)
{
	const uint8_t *v;
	size_t n;

	/*
	 * Linux has no layout for this event (it waits for HTT's security
	 * indication instead).  Assumed from Qualcomm's host driver [?]:
	 * vdev_id, peer mac (8), key_idx, key_flags, status - 24 bytes.
	 */
	if (walk(ev) < 0 ||
	    need(ev, WMI_TAG_VDEV_INSTALL_KEY_DONE, 4, &v, &n))
		return EINVAL;
	memset(k, 0, sizeof(*k));
	k->vdev_id = get32(v);
	if (n >= 24) {
		memcpy(k->mac, v + 4, 6);
		k->key_idx = get32(v + 12);
		k->key_flags = get32(v + 16);
		k->status = get32(v + 20);
		k->full = 1;
	}
	return 0;
}

int
wmi_peer_kickout(const struct wmi_ev *ev, struct wmi_kickout *k)
{
	const uint8_t *v;
	size_t n;

	/*
	 * Linux reads the mac only.  A reason and an RSSI after it are
	 * assumed from Qualcomm's host driver [?] and given only if there.
	 */
	if (walk(ev) < 0 || need(ev, WMI_TAG_PEER_STA_KICKOUT, 8, &v, &n))
		return EINVAL;
	memset(k, 0, sizeof(*k));
	memcpy(k->mac, v, 6);
	if (n >= 12) {
		k->reason = get32(v + 8);
		k->has_reason = 1;
	}
	if (n >= 16) {
		k->rssi = get32(v + 12);
		k->has_rssi = 1;
	}
	return 0;
}
