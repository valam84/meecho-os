#ifndef _WCN_WMI_H
#define _WCN_WMI_H

/*
 * WMI, the control protocol of the WCN3990's firmware, in its TLV form
 * (WMI op version 4; port/clover/wifi/htc-wmi.md, section 4, and
 * port/clover/wifi/sta.md).  Only what a client (managed station) needs:
 * the start (SERVICE_READY, INIT, READY), the channel list and the
 * regulatory domain, a hardware scan, a vdev, a peer for the AP, the
 * association, keys, and management frames sent by reference.
 *
 * What is here is the WMI part of a message, the part after the 8-byte
 * HTC header: a little-endian 32-bit command (or event) id, the id in
 * bits 23:0, then TLVs - le16 length, le16 tag, the value; the length
 * counts the value only.  A command is built into a caller's buffer; an
 * event is parsed out of one, every length checked against the bytes
 * that came.
 *
 * No MINIX and no transport here.  The host stand (port/test/wcn,
 * wmitest.c) compares every command with the bytes ath10k puts on the
 * wire for this chip and feeds the parsers events made to be wrong.
 * Where Linux and this file differ on purpose, the comment says so; a
 * fact taken on faith is marked [?].
 */

#include <stddef.h>
#include <stdint.h>

/* ---- framing ---- */

#define WMI_HDR			4		/* le32 command/event id */
#define WMI_ID_MASK		0x00ffffff	/* bits 31:24 - PLT_PRIV, 0 */
#define WMI_TLV_HDR		4		/* le16 len, le16 tag */

/* TLV tags (Linux's enum wmi_tlv_tag, counted by a compiler). */
#define WMI_TAG_ARRAY_UINT32		0x10	/* le32[] */
#define WMI_TAG_ARRAY_BYTE		0x11	/* u8[], padded to 4 */
#define WMI_TAG_ARRAY_STRUCT		0x12	/* nested TLVs */
#define WMI_TAG_ARRAY_FIXED_STRUCT	0x13	/* structs, no inner TLVs */
#define WMI_TAG_SERVICE_READY_EVENT	0x20
#define WMI_TAG_HAL_REG_CAPABILITIES	0x21
#define WMI_TAG_WLAN_HOST_MEM_REQ	0x22	/* inside SERVICE_READY */
#define WMI_TAG_READY_EVENT		0x23
#define WMI_TAG_SCAN_EVENT		0x24
#define WMI_TAG_CHAN_INFO_EVENT		0x26
#define WMI_TAG_VDEV_START_RESPONSE	0x28
#define WMI_TAG_VDEV_STOPPED_EVENT	0x29
#define WMI_TAG_VDEV_INSTALL_KEY_DONE	0x2a
#define WMI_TAG_PEER_STA_KICKOUT	0x2b
#define WMI_TAG_MGMT_RX_HDR		0x2c
#define WMI_TAG_INIT_CMD		0x4a
#define WMI_TAG_RESOURCE_CONFIG		0x4b
#define WMI_TAG_HOST_MEMORY_CHUNK	0x4c	/* inside INIT */
#define WMI_TAG_START_SCAN_CMD		0x4d
#define WMI_TAG_STOP_SCAN_CMD		0x4e
#define WMI_TAG_SCAN_CHAN_LIST_CMD	0x4f
#define WMI_TAG_CHANNEL			0x50
#define WMI_TAG_PDEV_SET_REGDOMAIN_CMD	0x51
#define WMI_TAG_PDEV_SET_PARAM_CMD	0x52
#define WMI_TAG_VDEV_CREATE_CMD		0x56
#define WMI_TAG_VDEV_DELETE_CMD		0x57
#define WMI_TAG_VDEV_START_REQUEST_CMD	0x58
#define WMI_TAG_VDEV_UP_CMD		0x5c
#define WMI_TAG_VDEV_STOP_CMD		0x5d
#define WMI_TAG_VDEV_DOWN_CMD		0x5e
#define WMI_TAG_VDEV_SET_PARAM_CMD	0x5f
#define WMI_TAG_VDEV_INSTALL_KEY_CMD	0x60
#define WMI_TAG_PEER_CREATE_CMD		0x61
#define WMI_TAG_PEER_DELETE_CMD		0x62
#define WMI_TAG_PEER_SET_PARAM_CMD	0x64
#define WMI_TAG_PEER_ASSOC_CMD		0x65
#define WMI_TAG_VHT_RATE_SET		0x66
#define WMI_TAG_STA_POWERSAVE_MODE_CMD	0x6c
#define WMI_TAG_STA_POWERSAVE_PARAM_CMD	0x6d
#define WMI_TAG_MGMT_TX_CMD		0x1a6
#define WMI_TAG_MGMT_TX_COMPL_EVENT	0x1a7
#define WMI_TAG_MGMT_TX_COMPL_BUNDLE	0x228
#define WMI_TAG_SERVICE_AVAILABLE_EVENT	0x22f

/* Commands: (group << 12) | 1 for the first of a group, then in order. */
#define WMI_CMD_INIT			0x0001
#define WMI_CMD_START_SCAN		0x3001
#define WMI_CMD_STOP_SCAN		0x3002
#define WMI_CMD_SCAN_CHAN_LIST		0x3003
#define WMI_CMD_PDEV_SET_REGDOMAIN	0x4001
#define WMI_CMD_PDEV_SET_PARAM		0x4003
#define WMI_CMD_VDEV_CREATE		0x5001
#define WMI_CMD_VDEV_DELETE		0x5002
#define WMI_CMD_VDEV_START_REQUEST	0x5003
#define WMI_CMD_VDEV_UP			0x5005
#define WMI_CMD_VDEV_STOP		0x5006
#define WMI_CMD_VDEV_DOWN		0x5007
#define WMI_CMD_VDEV_SET_PARAM		0x5008
#define WMI_CMD_VDEV_INSTALL_KEY	0x5009
#define WMI_CMD_PEER_CREATE		0x6001
#define WMI_CMD_PEER_DELETE		0x6002
#define WMI_CMD_PEER_SET_PARAM		0x6004
#define WMI_CMD_PEER_ASSOC		0x6005
#define WMI_CMD_MGMT_TX_SEND		0x7008	/* by reference; 0x7006 is not TLV's */
#define WMI_CMD_STA_POWERSAVE_MODE	0x9001
#define WMI_CMD_STA_POWERSAVE_PARAM	0x9002

/* Events. */
#define WMI_EV_SERVICE_READY		0x0001
#define WMI_EV_READY			0x0002
#define WMI_EV_SERVICE_AVAILABLE	0x0003	/* may come just before SERVICE_READY */
#define WMI_EV_SCAN			0x3001
#define WMI_EV_CHAN_INFO		0x4002	/* survey, CHAN_STAT_EVENT */
#define WMI_EV_TX_PAUSE			0x4005
#define WMI_EV_VDEV_START_RESP		0x5001
#define WMI_EV_VDEV_STOPPED		0x5002
#define WMI_EV_VDEV_INSTALL_KEY_DONE	0x5003
#define WMI_EV_VDEV_DELETE_RESP		0x5006	/* with SYNC_DELETE_CMDS */
#define WMI_EV_PEER_STA_KICKOUT		0x6001	/* the AP is gone */
#define WMI_EV_PEER_DELETE_RESP		0x6007	/* with SYNC_DELETE_CMDS */
#define WMI_EV_MGMT_RX			0x7001
#define WMI_EV_MGMT_TX_COMPLETION	0x7006
#define WMI_EV_MGMT_TX_BUNDLE_COMPL	0x7007	/* INIT asks for these */
#define WMI_EV_ROAM			0xb001	/* beacon miss and the like */
#define WMI_EV_DEBUG_MESG		0x1d003

/* The ABI both sides name (SERVICE_READY is checked, INIT carries it). */
#define WMI_ABI_VER0			0x01000000	/* major 1, minor 0 */
#define WMI_ABI_VER1			53		/* what the host says */
#define WMI_ABI_NS0			0x5f414351	/* "QCA_" */
#define WMI_ABI_NS1			0x00004c4d	/* "ML" */
#define WMI_ABI_NS2			0
#define WMI_ABI_NS3			0

/* pdev parameters (TLV numbering - not wmi.h's). */
#define WMI_PDEV_TX_CHAIN_MASK		0x01
#define WMI_PDEV_RX_CHAIN_MASK		0x02
#define WMI_PDEV_TXPOWER_LIMIT2G	0x03	/* 0.5 dBm */
#define WMI_PDEV_TXPOWER_LIMIT5G	0x04
#define WMI_PDEV_DYNAMIC_BW		0x0a	/* ath10k: 1 */
#define WMI_PDEV_PMF_QOS		0x22	/* ath10k: 1 */
#define WMI_PDEV_ARP_AC_OVERRIDE	0x23	/* ath10k: 0 */
#define WMI_PDEV_ANI_ENABLE		0x25	/* ath10k: 1 */
#define WMI_PDEV_IDLE_PS_CONFIG		0x2c	/* ath10k: 1 */
#define WMI_PDEV_BURST_ENABLE		0x30	/* only with the BURST service */

/* vdev parameters (TLV numbering). */
#define WMI_VDEV_RTS_THRESHOLD		0x01
#define WMI_VDEV_BEACON_INTERVAL	0x03	/* TU */
#define WMI_VDEV_SLOT_TIME		0x07	/* WMI_SLOT_* */
#define WMI_VDEV_PREAMBLE		0x08	/* WMI_PREAMBLE_* */
#define WMI_VDEV_DTIM_PERIOD		0x0d
#define WMI_VDEV_PROTECTION_MODE	0x1a	/* CTS protection */
#define WMI_VDEV_NSS			0x22
#define WMI_VDEV_DROP_UNENCRY		0x2f	/* ath10k leaves it alone */
#define WMI_VDEV_TX_ENCAP_TYPE		0x30	/* WMI_TXRX_* */

#define WMI_SLOT_LONG			1
#define WMI_SLOT_SHORT			2
#define WMI_PREAMBLE_LONG		1
#define WMI_PREAMBLE_SHORT		2

/* Frame formats between host and firmware (TX_ENCAP_TYPE, rx_decap_mode). */
#define WMI_TXRX_RAW			0
#define WMI_TXRX_NATIVE_WIFI		1	/* ath10k's choice: 802.11, no QoS ctl */
#define WMI_TXRX_ETHERNET		2
#define WMI_TXRX_MGMT			3

/* vdev types. */
#define WMI_VDEV_TYPE_AP		1
#define WMI_VDEV_TYPE_STA		2
#define WMI_VDEV_TYPE_IBSS		3
#define WMI_VDEV_SUBTYPE_NONE		0

/* VDEV_START_REQUEST flags. */
#define WMI_VDEV_START_HIDDEN_SSID	0x1
#define WMI_VDEV_START_PMF_ENABLED	0x2

/* VDEV_START_RESP resp_type. */
#define WMI_VDEV_RESP_START		0
#define WMI_VDEV_RESP_RESTART		1

/* peer parameters (TLV numbering). */
#define WMI_PEER_SMPS_STATE		0x01
#define WMI_PEER_AMPDU			0x02
#define WMI_PEER_AUTHORIZE		0x03	/* 1 after the pairwise key */
#define WMI_PEER_CHAN_WIDTH		0x04
#define WMI_PEER_NSS			0x05
#define WMI_PEER_DUMMY_VAR		0xff	/* ath10k's power save workaround */

#define WMI_PEER_TYPE_DEFAULT		0	/* ath10k sends it for the BSS peer too */

/* PEER_ASSOC flags (TLV values - wmi.h's differ, e.g. PMF). */
#define WMI_PEER_F_AUTH			0x00000001
#define WMI_PEER_F_QOS			0x00000002	/* AP has WMM */
#define WMI_PEER_F_NEED_PTK_4_WAY	0x00000004	/* RSN or WPA */
#define WMI_PEER_F_NEED_GTK_2_WAY	0x00000010	/* WPA (v1) only */
#define WMI_PEER_F_APSD			0x00000800
#define WMI_PEER_F_HT			0x00001000
#define WMI_PEER_F_40MHZ		0x00002000
#define WMI_PEER_F_STBC			0x00008000
#define WMI_PEER_F_LDPC			0x00010000
#define WMI_PEER_F_SPATIAL_MUX		0x00200000
#define WMI_PEER_F_VHT			0x02000000
#define WMI_PEER_F_80MHZ		0x04000000
#define WMI_PEER_F_PMF			0x08000000	/* needs fw feature mfp: not here */

/* PEER_ASSOC rate_caps. */
#define WMI_RC_DS			0x0001	/* two streams */
#define WMI_RC_CW40			0x0002
#define WMI_RC_SGI			0x0004
#define WMI_RC_HT			0x0008
#define WMI_RC_TX_STBC			0x0020
#define WMI_RC_TS			0x0200	/* three streams */

#define WMI_RATE_CCK			0x80	/* legacy rate byte: CCK, not "basic" */
#define WMI_MAX_RATES			128

/* PHY modes (wmi_phy_mode). */
#define WMI_MODE_11A			0
#define WMI_MODE_11G			1
#define WMI_MODE_11B			2
#define WMI_MODE_11GONLY		3
#define WMI_MODE_11NA_HT20		4
#define WMI_MODE_11NG_HT20		5
#define WMI_MODE_11NA_HT40		6
#define WMI_MODE_11NG_HT40		7
#define WMI_MODE_11AC_VHT20		8
#define WMI_MODE_11AC_VHT40		9
#define WMI_MODE_11AC_VHT80		10

/* struct wmi_channel flags (mode in bits 5:0 shares the word). */
#define WMI_CHAN_HT40_PLUS		0x00000040
#define WMI_CHAN_PASSIVE		0x00000080	/* no probe requests (NO_IR, radar) */
#define WMI_CHAN_ADHOC_ALLOWED		0x00000100
#define WMI_CHAN_AP_DISABLED		0x00000200
#define WMI_CHAN_DFS			0x00000400
#define WMI_CHAN_ALLOW_HT		0x00000800
#define WMI_CHAN_ALLOW_VHT		0x00001000
#define WMI_CHAN_MODE_MASK		0x0000003f

/* START_SCAN control flags. */
#define WMI_SCAN_PASSIVE		0x00000001	/* no SSIDs given */
#define WMI_SCAN_ADD_BCAST_PROBE_REQ	0x00000002
#define WMI_SCAN_ADD_CCK_RATES		0x00000004
#define WMI_SCAN_ADD_OFDM_RATES		0x00000008
#define WMI_SCAN_CHAN_STAT_EVENT	0x00000010
#define WMI_SCAN_FILTER_PROBE_REQ	0x00000020	/* inverted on the wire, see wmi_start_scan */
#define WMI_SCAN_SPOOFED_MAC		0x00001000	/* mac_addr/mac_mask used */

/* Scan events (event_type bits, also notify_scan_events). */
#define WMI_SCAN_EV_STARTED		0x0001
#define WMI_SCAN_EV_COMPLETED		0x0002
#define WMI_SCAN_EV_BSS_CHANNEL		0x0004	/* back home */
#define WMI_SCAN_EV_FOREIGN_CHANNEL	0x0008	/* on channel_freq */
#define WMI_SCAN_EV_DEQUEUED		0x0010
#define WMI_SCAN_EV_PREEMPTED		0x0020
#define WMI_SCAN_EV_START_FAILED	0x0040
#define WMI_SCAN_EV_RESTARTED		0x0080
#define WMI_SCAN_EV_FOREIGN_EXIT	0x0100

/* COMPLETED reasons. */
#define WMI_SCAN_R_COMPLETED		0
#define WMI_SCAN_R_CANCELLED		1
#define WMI_SCAN_R_PREEMPTED		2
#define WMI_SCAN_R_TIMEDOUT		3
#define WMI_SCAN_R_INTERNAL_FAILURE	4

#define WMI_SCAN_PRIORITY_LOW		1
#define WMI_SCAN_ID_PREFIX		0xa000	/* or-ed into scan_id and requestor */
#define WMI_SCAN_ID_MAX			0xfff

/* STOP_SCAN req_type. */
#define WMI_SCAN_STOP_ONE		0x00000000
#define WMI_SCAN_STOP_VDEV_ALL		0x01000000
#define WMI_SCAN_STOP_ALL		0x04000000

#define WMI_SCAN_MAX_CHANNELS		64
#define WMI_SCAN_MAX_SSIDS		16
#define WMI_SCAN_MAX_BSSIDS		4
#define WMI_SCAN_MAX_IE			256
#define WMI_SSID_MAX			32

/* MGMT_RX status bits. */
#define WMI_RX_OK			0x00
#define WMI_RX_ERR_CRC			0x01
#define WMI_RX_ERR_DECRYPT		0x08
#define WMI_RX_ERR_MIC			0x10
#define WMI_RX_ERR_KEY_CACHE_MISS	0x20
#define WMI_RX_EXT_INFO			0x40
#define WMI_NOISE_FLOOR			(-95)	/* dBm; signal = snr + this */

/* Keys. */
#define WMI_KEY_PAIRWISE		0x0
#define WMI_KEY_GROUP			0x1
#define WMI_CIPHER_NONE			0	/* TLV numbering */
#define WMI_CIPHER_WEP			1
#define WMI_CIPHER_TKIP			2
#define WMI_CIPHER_AES_CCM		4	/* CCMP-128 */
#define WMI_CIPHER_AES_CMAC		7
#define WMI_CIPHER_AES_GCM		9	/* wmi.h says 8: TLV differs */
#define WMI_MAX_KEY_INDEX		3
#define WMI_MAX_KEY_LEN			32

/* STA power save. */
#define WMI_STA_PS_MODE_DISABLED	0
#define WMI_STA_PS_MODE_ENABLED		1
#define WMI_STA_PS_RX_WAKE_POLICY	0	/* param: value WAKE 0 */
#define WMI_STA_PS_TX_WAKE_THRESHOLD	1	/* param: value ALWAYS 1 */
#define WMI_STA_PS_PSPOLL_COUNT		2	/* param: value NO_MAX 0 */
#define WMI_STA_PS_RX_WAKE		0
#define WMI_STA_PS_TX_WAKE_ALWAYS	1
#define WMI_STA_PS_PSPOLL_NO_MAX	0

/* Management frames by reference. */
#define WMI_MGMT_TX_INLINE		64	/* first bytes copied into the command */
#define WMI_MGMT_TX_MAX_PENDING		50	/* desc_id below this, as ath10k */
#define WMI_MGMT_TX_CCMP_MIC		8	/* room a protected action/deauth needs */

/* Services (TLV numbering; SERVICE_READY: 4 bits a word). */
#define WMI_SVC_STA_PWRSAVE		4
#define WMI_SVC_11AC			8
#define WMI_SVC_STA_KEEP_ALIVE		29
#define WMI_SVC_BURST			37
#define WMI_SVC_ADAPTIVE_OCS		39
#define WMI_SVC_RX_FULL_REORDER		65	/* HTT in-order rx; INIT's offload peers */
#define WMI_SVC_MGMT_TX_WMI		76	/* management frames go by WMI */
#define WMI_SVC_SYNC_DELETE_CMDS	92
#define WMI_SVC_THERM_THROT		140	/* the rest: SERVICE_AVAILABLE */
#define WMI_SVC_SPOOF_MAC		155
#define WMI_SVC_TX_DATA_MGMT_ACK_RSSI	174
#define WMI_SVC_EXTEND_ADDRESS		179	/* 64-bit host memory chunks */
#define WMI_SVC_MAX			128	/* end of the SERVICE_READY map */
#define WMI_SVC_EXT_MAX			256

#define WMI_MAX_MEM_REQS		16
#define WMI_MEM_UNITS_VDEVS		0x1	/* num_unit_info: count is vdevs + 1 */
#define WMI_MEM_UNITS_PEERS		0x2	/* peers + 1 */
#define WMI_MEM_UNITS_ACTIVE_PEERS	0x4

/* INIT flags: what SERVICE_READY/AVAILABLE said that changes INIT. */
#define WMI_INIT_RX_REORDER		0x1	/* WMI_SVC_RX_FULL_REORDER */
#define WMI_INIT_TX_ACK_RSSI		0x2	/* WMI_SVC_TX_DATA_MGMT_ACK_RSSI */
#define WMI_INIT_EXT_ADDR		0x4	/* WMI_SVC_EXTEND_ADDRESS */

/* ---- building ---- */

struct wmi_buf {
	uint8_t *p;
	size_t cap;
	size_t len;
	int err;			/* did not fit, or a value out of range */
};

/*
 * Each builder writes WMI_HDR + TLVs into b and returns the length, or 0
 * if it did not fit or an argument is outside what the firmware takes
 * (the limits ath10k checks, and a few it does not - each said where).
 */

struct wmi_mem_chunk {
	uint32_t req_id;
	uint64_t paddr;			/* device (IOVA) address */
	uint32_t len;
};

size_t wmi_init(struct wmi_buf *b, unsigned flags,
	const struct wmi_mem_chunk *c, unsigned n);
size_t wmi_pdev_set_param(struct wmi_buf *b, uint32_t id, uint32_t value);
size_t wmi_pdev_set_regdomain(struct wmi_buf *b, uint32_t rd, uint32_t rd2g,
	uint32_t rd5g, uint32_t ctl2g, uint32_t ctl5g);

/* A channel, as ath10k's put_wmi_channel lays it. */
struct wmi_chan {
	uint32_t freq;			/* MHz, primary */
	uint32_t center1, center2;	/* band centres; center2 for 80+80 only */
	uint32_t mode;			/* WMI_MODE_* */
	uint32_t flags;			/* WMI_CHAN_* */
	uint8_t min_power, max_power;	/* 0.5 dBm; max_tx_power = max_power */
	uint8_t reg_power;		/* 0.5 dBm */
	uint8_t antenna_max;		/* 1 dBm */
};

size_t wmi_scan_chan_list(struct wmi_buf *b, const struct wmi_chan *c,
	unsigned n);

struct wmi_ssid {
	uint8_t len;
	uint8_t ssid[WMI_SSID_MAX];
};

struct wmi_scan {
	uint32_t vdev_id;
	uint32_t scan_id, req_id;	/* without the prefix, at most 0xfff */
	uint32_t priority, notify;
	uint32_t dwell_active, dwell_passive;	/* ms */
	uint32_t min_rest, max_rest, repeat_probe, probe_spacing;
	uint32_t idle_time, max_scan_time, probe_delay;
	uint32_t flags;			/* WMI_SCAN_*, FILTER_PROBE_REQ as meant */
	uint32_t burst_duration;
	const uint32_t *freqs;		/* MHz */
	unsigned nchan;
	const struct wmi_ssid *ssids;	/* none: say WMI_SCAN_PASSIVE */
	unsigned nssid;
	const uint8_t (*bssids)[6];
	unsigned nbssid;
	const uint8_t *ie;		/* probe request IEs */
	size_t ie_len;
	uint8_t mac_addr[6], mac_mask[6];	/* with WMI_SCAN_SPOOFED_MAC */
};

/* ath10k's defaults for a hardware scan (one broadcast BSSID). */
void wmi_scan_defaults(struct wmi_scan *s);
size_t wmi_start_scan(struct wmi_buf *b, const struct wmi_scan *s);
size_t wmi_stop_scan(struct wmi_buf *b, uint32_t req_type, uint32_t scan_id,
	uint32_t req_id, uint32_t vdev_id);

size_t wmi_vdev_create(struct wmi_buf *b, uint32_t vdev_id, uint32_t type,
	uint32_t subtype, const uint8_t mac[6]);
size_t wmi_vdev_delete(struct wmi_buf *b, uint32_t vdev_id);
size_t wmi_vdev_set_param(struct wmi_buf *b, uint32_t vdev_id, uint32_t id,
	uint32_t value);

struct wmi_vdev_start {
	uint32_t vdev_id;
	uint32_t bcn_intval, dtim_period;
	uint32_t flags;			/* WMI_VDEV_START_* */
	const uint8_t *ssid;		/* AP/IBSS only: a station sends none */
	size_t ssid_len;
	struct wmi_chan chan;
};

size_t wmi_vdev_start(struct wmi_buf *b, const struct wmi_vdev_start *v);
size_t wmi_vdev_stop(struct wmi_buf *b, uint32_t vdev_id);
size_t wmi_vdev_up(struct wmi_buf *b, uint32_t vdev_id, uint32_t aid,
	const uint8_t bssid[6]);
size_t wmi_vdev_down(struct wmi_buf *b, uint32_t vdev_id);

size_t wmi_peer_create(struct wmi_buf *b, uint32_t vdev_id,
	const uint8_t mac[6], uint32_t type);
size_t wmi_peer_delete(struct wmi_buf *b, uint32_t vdev_id,
	const uint8_t mac[6]);
size_t wmi_peer_set_param(struct wmi_buf *b, uint32_t vdev_id,
	const uint8_t mac[6], uint32_t id, uint32_t value);

struct wmi_peer_assoc {
	uint8_t mac[6];			/* the BSSID */
	uint32_t vdev_id;
	int reassoc;			/* new_assoc = !reassoc */
	uint32_t aid;			/* from the Association Response */
	uint32_t flags;			/* WMI_PEER_F_* */
	uint32_t caps;			/* capability information */
	uint32_t listen_intval;		/* ath10k: 1 for a station */
	uint32_t ht_caps, max_mpdu, mpdu_density;	/* density <= 16 */
	uint32_t rate_caps;		/* WMI_RC_* */
	uint32_t nss;
	uint32_t vht_caps;
	uint32_t phy_mode;		/* WMI_MODE_* */
	uint32_t ht_info[2];		/* ath10k leaves both 0 */
	const uint8_t *legacy;		/* 500 kbit/s units | WMI_RATE_CCK */
	unsigned nlegacy;
	const uint8_t *ht;		/* MCS indices */
	unsigned nht;
	uint32_t vht_rx_max, vht_rx_mcs, vht_tx_max, vht_tx_mcs;
};

size_t wmi_peer_assoc(struct wmi_buf *b, const struct wmi_peer_assoc *a);

struct wmi_key {
	uint32_t vdev_id;
	uint8_t mac[6];			/* the BSSID, pairwise and group alike */
	uint32_t idx;			/* <= WMI_MAX_KEY_INDEX */
	uint32_t flags;			/* WMI_KEY_PAIRWISE / WMI_KEY_GROUP */
	uint32_t cipher;		/* WMI_CIPHER_* */
	const uint8_t *key;		/* none for WMI_CIPHER_NONE */
	uint32_t key_len;		/* key, then tx mic, then rx mic */
	uint32_t txmic_len, rxmic_len;
};

size_t wmi_vdev_install_key(struct wmi_buf *b, const struct wmi_key *k);

/*
 * A management frame (or a (QoS) Null) handed to the firmware by its
 * address: frame_len bytes at paddr stay mapped until MGMT_TX_COMPLETION
 * names desc_id.  The first WMI_MGMT_TX_INLINE bytes ride in the command.
 * A protected action/deauth/disassoc must already include room for the
 * CCMP MIC (WMI_MGMT_TX_CCMP_MIC) in frame and frame_len.
 */
size_t wmi_mgmt_tx(struct wmi_buf *b, uint32_t vdev_id, uint32_t desc_id,
	uint32_t chan_freq, uint64_t paddr, const uint8_t *frame,
	size_t frame_len);

size_t wmi_sta_ps_mode(struct wmi_buf *b, uint32_t vdev_id, uint32_t mode);
size_t wmi_sta_ps_param(struct wmi_buf *b, uint32_t vdev_id, uint32_t id,
	uint32_t value);

/* ---- reading ---- */

/*
 * An event: its id and its TLVs.  0, or EINVAL when shorter than the
 * header.  Every parser below first walks all the TLVs - a length past
 * the end, a header cut short, or a known structure shorter than its
 * size (the minimum lengths Linux's policy table checks) anywhere makes
 * the whole event EINVAL, as in Linux.
 */
struct wmi_ev {
	uint32_t id;			/* masked with WMI_ID_MASK */
	const uint8_t *tlv;
	size_t len;
};

int wmi_event(const uint8_t *buf, size_t len, struct wmi_ev *ev);

/* Walk TLVs: 1 and the next one, 0 at the end, -1 if malformed. */
struct wmi_tlv_it {
	const uint8_t *p;
	size_t left;
};

void wmi_tlv_begin(struct wmi_tlv_it *it, const uint8_t *p, size_t len);
int wmi_tlv_next(struct wmi_tlv_it *it, unsigned *tag, const uint8_t **v,
	size_t *vlen);

/* The nth (from 0) TLV with this tag: 1, 0 if absent, -1 if malformed. */
int wmi_tlv_find(const struct wmi_ev *ev, unsigned tag, unsigned nth,
	const uint8_t **v, size_t *vlen);

/* The service bitmaps of SERVICE_READY and SERVICE_AVAILABLE. */
#define WMI_SVC_WORDS		64	/* kept of SERVICE_READY's map */
#define WMI_SVC_EXT_WORDS	4	/* (256 - 128) / 32 */

struct wmi_svc {
	uint32_t w[WMI_SVC_WORDS];
	size_t len;			/* bytes given, as Linux's bound */
	uint32_t ext[WMI_SVC_EXT_WORDS];
	unsigned ext_words;		/* 0: no SERVICE_AVAILABLE */
};

int wmi_svc_enabled(const struct wmi_svc *s, unsigned id);

struct wmi_mem_req {
	uint32_t req_id, unit_size, num_unit_info, num_units;
};

struct wmi_svc_ready {
	uint32_t fw_build;
	uint32_t abi_ver0, abi_ver1;	/* firmware version: see wcn_wmi.c */
	uint32_t phy_capability, max_frag_entry;
	uint32_t num_rf_chains;
	uint32_t ht_cap_info, vht_cap_info, vht_supp_mcs;
	uint32_t hw_min_tx_power, hw_max_tx_power;
	uint32_t sys_cap_info;		/* bit 1: RF kill */
	uint32_t max_bcn_ie_size, max_num_scan_chans, hw_bd_id;
	/* HAL_REG_CAPABILITIES */
	uint32_t eeprom_rd, eeprom_rd_ext, regcap1, regcap2, wireless_modes;
	uint32_t low_2ghz_chan, high_2ghz_chan, low_5ghz_chan, high_5ghz_chan;
	unsigned num_mem_reqs;
	struct wmi_mem_req mem_req[WMI_MAX_MEM_REQS];
};

/*
 * SERVICE_READY: 0, EINVAL (malformed, a part missing, more memory
 * requests than WMI_MAX_MEM_REQS or than the event carries), ENOTSUP
 * (the ABI is not the host's).  The bitmap goes into svc; its
 * SERVICE_AVAILABLE half is left alone.
 */
int wmi_svc_ready(const struct wmi_ev *ev, struct wmi_svc_ready *r,
	struct wmi_svc *svc);

/* SERVICE_AVAILABLE: the services from 128 on into svc->ext. */
int wmi_svc_available(const struct wmi_ev *ev, struct wmi_svc *svc);

struct wmi_ready {
	uint32_t abi_ver0, abi_ver1;
	uint8_t mac[6];			/* expect it unusable on clover */
	uint32_t status;
};

int wmi_ready(const struct wmi_ev *ev, struct wmi_ready *r);

struct wmi_scan_ev {
	uint32_t type;			/* one WMI_SCAN_EV_* */
	uint32_t reason;		/* WMI_SCAN_R_* with COMPLETED */
	uint32_t freq;			/* with FOREIGN_CHANNEL */
	uint32_t req_id, scan_id;	/* with the 0xa000 prefix */
	uint32_t vdev_id;
};

int wmi_scan_event(const struct wmi_ev *ev, struct wmi_scan_ev *s);

struct wmi_mgmt_rx {
	uint32_t channel;		/* IEEE channel number, not MHz */
	uint32_t snr;			/* dB above WMI_NOISE_FLOOR */
	uint32_t rate;			/* kbit/s */
	uint32_t phy_mode;
	uint32_t status;		/* WMI_RX_* */
	uint32_t rssi[4];		/* per chain, 0 or 0x80: none */
	const uint8_t *frame;		/* 802.11 frame, header on, no FCS */
	size_t len;
};

int wmi_mgmt_rx(const struct wmi_ev *ev, struct wmi_mgmt_rx *m);

struct wmi_vdev_start_resp {
	uint32_t vdev_id, req_id, resp_type, status;	/* status 0: started */
};

int wmi_vdev_start_resp(const struct wmi_ev *ev,
	struct wmi_vdev_start_resp *r);

struct wmi_tx_compl {
	uint32_t desc_id;
	uint32_t status;		/* 0: acknowledged; else not */
	uint32_t pdev_id, ppdu_id;
	uint32_t ack_rssi;		/* only with TX_DATA_MGMT_ACK_RSSI */
	int has_ack_rssi;
};

int wmi_mgmt_tx_compl(const struct wmi_ev *ev, struct wmi_tx_compl *c);

/*
 * The bundled form (INIT asks for it): up to max reports into c, their
 * number into *n.  EINVAL if malformed or an array shorter than the
 * count, ENOSPC if more than max.
 */
int wmi_mgmt_tx_bundle(const struct wmi_ev *ev, struct wmi_tx_compl *c,
	unsigned max, unsigned *n);

struct wmi_key_done {
	uint32_t vdev_id;
	uint8_t mac[6];
	uint32_t key_idx, key_flags, status;
	int full;			/* the event carried all of the above */
};

int wmi_install_key_done(const struct wmi_ev *ev, struct wmi_key_done *k);

struct wmi_kickout {
	uint8_t mac[6];
	uint32_t reason, rssi;
	int has_reason, has_rssi;
};

int wmi_peer_kickout(const struct wmi_ev *ev, struct wmi_kickout *k);

#endif /* _WCN_WMI_H */
