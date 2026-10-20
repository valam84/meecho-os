#ifndef _WCN_MLME_H
#define _WCN_MLME_H

/*
 * The client's MLME of the WCN3990 driver: what mac80211 does for ath10k
 * and what this machine has no stack for (port/clover/hw-wifi.md, section
 * 7; port/clover/wifi/sta.md, sections 3, 4 and 6).
 *
 *  - the channel table (2.4 GHz 1-14, 5 GHz 36-165);
 *  - a beacon or probe response, as MGMT_RX hands it, parsed into a
 *    struct wcn_bss, and a small table of them;
 *  - Authentication (open system), Association Request,
 *    Deauthentication and Disassociation built; the answers parsed;
 *  - Ethernet <-> "native wifi" (an 802.11 data header without QoS
 *    Control, then LLC/SNAP; the firmware has no-4addr-pad, so no
 *    padding).
 *
 * No MINIX and no firmware here: frames are built into the caller's
 * buffer and parsed out of one, every length is checked against the
 * buffer it came in.  The host stand is port/test/wcn/mlmetest.c.
 *
 * Sequence numbers: mac80211 numbers management frames itself, the
 * firmware does not (sta.md, 3.3), so the builders below take them from
 * struct wcn_sta and count on.  Data frames go with a zero sequence
 * control: their numbers are the firmware's (it keeps them per TID).
 */

#include <stddef.h>
#include <stdint.h>

#define WCN_ALEN		6

/* ---- Channels ---- */

#define WCN_BAND_2G		0
#define WCN_BAND_5G		1

#define WCN_CHAN_DFS		0x01	/* radar channel: passive scan */

struct wcn_chan {
	uint16_t freq;			/* MHz */
	uint8_t chan;			/* IEEE number */
	uint8_t band;
	uint8_t flags;
};

extern const struct wcn_chan wcn_chans[];
extern const unsigned wcn_nchans;

/* The channel's frequency (the band follows from the number), 0 if not
 * a channel of the table. */
unsigned wcn_chan_freq(unsigned chan);
/* The frequency's channel and band, 0 if not in the table. */
unsigned wcn_freq_chan(unsigned freq, unsigned *band);
const struct wcn_chan *wcn_chan_by_freq(unsigned freq);

/* ---- 802.11 numbers ---- */

/* Frame control, first octet: type and subtype as they lie on the wire. */
#define WCN_FC_ASSOC_REQ	0x00
#define WCN_FC_ASSOC_RESP	0x10
#define WCN_FC_REASSOC_RESP	0x30
#define WCN_FC_PROBE_RESP	0x50
#define WCN_FC_BEACON		0x80
#define WCN_FC_DISASSOC		0xa0
#define WCN_FC_AUTH		0xb0
#define WCN_FC_DEAUTH		0xc0
#define WCN_FC_DATA		0x08
#define WCN_FC_QOS_DATA		0x88
/* Second octet. */
#define WCN_FC1_TODS		0x01
#define WCN_FC1_FROMDS		0x02
#define WCN_FC1_PROTECTED	0x40
#define WCN_FC1_ORDER		0x80

#define WCN_HDR_LEN		24	/* three addresses */

#define WCN_CAP_ESS		0x0001
#define WCN_CAP_PRIVACY		0x0010
#define WCN_CAP_SHORT_PREAMBLE	0x0020
#define WCN_CAP_SPECTRUM	0x0100
#define WCN_CAP_SHORT_SLOT	0x0400

#define WCN_STATUS_SUCCESS	0
#define WCN_REASON_LEAVING	3	/* deauth: "STA is leaving" */
#define WCN_REASON_DISASSOC_LEAVING 8

#define WCN_LISTEN_INTERVAL	5	/* ath10k's max_listen_interval */

/* Cipher suites and AKMs of the RSN OUI 00-0F-AC, as bit numbers in
 * the masks below; a suite of another OUI is bit 31. */
#define WCN_CIPHER_WEP40	1
#define WCN_CIPHER_TKIP		2
#define WCN_CIPHER_CCMP		4
#define WCN_CIPHER_WEP104	5
#define WCN_CIPHER_BIP_CMAC	6
#define WCN_CIPHER_NO_GROUP	7
#define WCN_CIPHER_GCMP		8
#define WCN_CIPHER_GCMP256	9
#define WCN_CIPHER_CCMP256	10
#define WCN_AKM_8021X		1
#define WCN_AKM_PSK		2
#define WCN_AKM_FT_8021X	3
#define WCN_AKM_FT_PSK		4
#define WCN_AKM_8021X_SHA256	5
#define WCN_AKM_PSK_SHA256	6
#define WCN_AKM_SAE		8
#define WCN_AKM_FT_SAE		9
#define WCN_AKM_OWE		18
#define WCN_AKM_SAE_EXT		24
#define WCN_SUITE_OTHER		31
#define WCN_BIT(n)		(1u << (n))

#define WCN_RSN_CAP_MFPR	0x0040
#define WCN_RSN_CAP_MFPC	0x0080

/* ---- A BSS, from a beacon or a probe response ---- */

struct wcn_rsn {
	uint8_t present;		/* an RSN element was there */
	uint8_t valid;			/* ... and parsed (version 1, counts in) */
	uint8_t group;			/* group cipher (suite type), or 31 */
	uint32_t pairwise;		/* WCN_BIT(cipher) for each offered */
	uint32_t akm;			/* WCN_BIT(akm) for each offered */
	uint16_t caps;
};

enum wcn_sec {
	WCN_SEC_OPEN,
	WCN_SEC_WEP,		/* privacy and no RSN/WPA element */
	WCN_SEC_WPA1,		/* the WPA vendor element only */
	WCN_SEC_WPA2_PSK,	/* RSN, PSK (or PSK-SHA256), CCMP */
	WCN_SEC_WPA2_WPA3,	/* transition: PSK and SAE, CCMP */
	WCN_SEC_WPA3,		/* SAE only */
	WCN_SEC_8021X,		/* enterprise */
	WCN_SEC_OTHER		/* an RSN we cannot use or could not parse */
};

#define WCN_RATES_MAX		16	/* supported (8) + extended */
#define WCN_BSS_IES		768	/* what a supplicant gets of them */

/* HT Operation, octet 1: secondary channel offset. */
#define WCN_HT_SEC_NONE		0
#define WCN_HT_SEC_ABOVE	1
#define WCN_HT_SEC_BELOW	3

struct wcn_bss {
	uint8_t bssid[WCN_ALEN];
	uint8_t ssid[32];
	uint8_t ssid_len;
	uint8_t hidden;			/* empty or all-zero SSID */
	uint8_t probe_resp;		/* from a probe response */
	uint16_t capab;
	uint16_t bintval;		/* TU */
	uint64_t tsf;
	uint8_t rates[WCN_RATES_MAX];	/* octets as sent: bit 7 = basic */
	uint8_t nrates;
	uint8_t chan;			/* primary channel, 0 if unknown */
	uint8_t band;
	uint16_t freq;
	struct wcn_rsn rsn;
	uint8_t wpa1;			/* the WPA vendor element */
	enum wcn_sec sec;
	uint8_t pmf_required, pmf_capable;
	uint8_t ht;			/* HT Capabilities present */
	uint16_t ht_capinfo;
	uint8_t ht_ampdu;
	uint8_t ht_mcs[4];		/* rx MCS bitmask, streams 1-4 */
	uint8_t ht_op;			/* HT Operation present */
	uint8_t ht_primary, ht_secoff, ht_width40;
	uint8_t vht;			/* VHT Capabilities present */
	uint8_t wmm;			/* WMM information or parameter */
	int8_t rssi;			/* dBm */
	uint32_t seen;			/* the caller's clock */
	uint16_t ies_len;
	uint8_t ies_trunc;		/* ies[] holds the first WCN_BSS_IES */
	uint8_t ies[WCN_BSS_IES];	/* the elements, as received */
};

/*
 * Parse a beacon or probe response (the whole frame, header included, no
 * FCS).  rx_chan is the channel MGMT_RX reports (0: unknown); the DS
 * Parameter Set, then HT Operation, take precedence over it.  0, or
 * EINVAL: not a beacon/probe response, too short, an element running
 * past the end, an SSID over 32 octets.  A bad RSN element does not
 * reject the frame: the BSS is kept as WCN_SEC_OTHER.
 */
int wcn_bss_parse(const uint8_t *f, size_t len, unsigned rx_chan,
	int rssi, uint32_t now, struct wcn_bss *b);

/* Can this driver join it: open, WPA2-PSK or transition (by PSK), PMF
 * not required, a supported channel, the basic rates ours. */
int wcn_bss_usable(const struct wcn_bss *b);

/* The channel to start the vdev on: primary frequency, centre and width
 * (20 or 40).  40 only if want40, the AP's HT Operation says so and the
 * pair is a valid one. */
void wcn_bss_chandef(const struct wcn_bss *b, int want40, unsigned *freq,
	unsigned *center, unsigned *width);

#define WCN_BSS_MAX		32

struct wcn_bss_table {
	struct wcn_bss e[WCN_BSS_MAX];
	uint8_t used[WCN_BSS_MAX];
};

void wcn_bss_init(struct wcn_bss_table *t);
/* Insert or update by BSSID; a hidden SSID does not wipe one learnt from
 * a probe response.  A full table loses its oldest entry. */
struct wcn_bss *wcn_bss_update(struct wcn_bss_table *t,
	const struct wcn_bss *b);
const struct wcn_bss *wcn_bss_find(const struct wcn_bss_table *t,
	const uint8_t *bssid);
/* Drop entries not seen for more than maxage (wrap-safe). */
void wcn_bss_expire(struct wcn_bss_table *t, uint32_t now, uint32_t maxage);
/* The usable BSS of this SSID with the strongest signal, among those
 * whose security is in secmask (WCN_BIT(WCN_SEC_...)); NULL if none. */
const struct wcn_bss *wcn_bss_best(const struct wcn_bss_table *t,
	const uint8_t *ssid, size_t ssid_len, unsigned secmask);

/* ---- Rates ---- */

/*
 * The rates to send in Association Request (and to PEER_ASSOC): ours that
 * the AP has, in our order, in units of 500 kbit/s without the basic
 * bit.  -1 if a basic rate or membership selector of the AP is not ours
 * (ht: we send HT, so the HT selector is met), or nothing is common.
 */
int wcn_rates_join(const struct wcn_bss *b, int ht, uint8_t *out,
	unsigned cap);
/* The same rates for PEER_ASSOC: bit 7 marks CCK, not basic (sta.md 3.5). */
void wcn_rates_peer(const uint8_t *join, unsigned n, uint8_t *out);

/* ---- Frames we build ---- */

struct wcn_sta {
	uint8_t self[WCN_ALEN];
	uint8_t bssid[WCN_ALEN];
	uint16_t seq;			/* next management sequence number */
};

/*
 * The HT Capabilities Info field from the firmware's SERVICE_READY
 * ht_cap_info and vht_cap_info, as ath10k_get_ht_cap() derives it for one
 * transmit chain.  0 if the firmware says no HT.
 */
uint16_t wcn_ht_capinfo(uint32_t fw_ht, uint32_t fw_vht);

struct wcn_assoc_opts {
	const uint8_t *rsn_ie;		/* whole element (48 or WPA 221), or NULL */
	size_t rsn_len;
	uint16_t ht_capinfo;		/* 0: send no HT */
	int ht40;			/* keep 20/40 and SGI40 in it */
	unsigned nss;			/* 1 or 2 receive streams */
	uint16_t listen;		/* 0: WCN_LISTEN_INTERVAL */
};

/* Each returns the frame's length, 0 if it did not fit or cannot be. */
size_t wcn_build_auth(struct wcn_sta *s, uint8_t *buf, size_t cap);
size_t wcn_build_assoc(struct wcn_sta *s, const struct wcn_bss *b,
	const struct wcn_assoc_opts *o, uint8_t *buf, size_t cap);
size_t wcn_build_deauth(struct wcn_sta *s, uint16_t reason, uint8_t *buf,
	size_t cap);
size_t wcn_build_disassoc(struct wcn_sta *s, uint16_t reason, uint8_t *buf,
	size_t cap);

/* ---- Frames we parse ---- */

struct wcn_mgmt {
	uint8_t subtype;		/* first octet of frame control */
	uint8_t fc1;
	const uint8_t *da, *sa, *bssid;
	uint16_t seqctl;
	const uint8_t *body;
	size_t blen;
};

/* A management frame: 0, or EINVAL (short, not management, version,
 * protected - this driver does no PMF). */
int wcn_mgmt_parse(const uint8_t *f, size_t len, struct wcn_mgmt *m);

struct wcn_auth_resp {
	uint16_t alg, seq, status;
};

/* Each of these takes the frame whole and checks it is to self from the
 * BSS of s (self NULL or bssid NULL in s: not checked).  0 or EINVAL. */
int wcn_parse_auth(const struct wcn_sta *s, const uint8_t *f, size_t len,
	struct wcn_auth_resp *r);

struct wcn_wmm_ac {
	uint8_t aci_aifsn, ecw;
	uint16_t txop;			/* units of 32 us */
};

struct wcn_assoc_resp {
	uint16_t capab, status, aid;	/* aid with bits 14-15 stripped */
	uint8_t reassoc;
	uint8_t rates[WCN_RATES_MAX];
	uint8_t nrates;
	uint8_t ht, ht_ampdu;
	uint16_t ht_capinfo;
	uint8_t ht_mcs[4];
	uint8_t ht_op, ht_primary, ht_secoff, ht_width40;
	uint8_t vht;
	uint8_t wmm;			/* WMM Parameter element given */
	uint8_t wmm_qosinfo;
	struct wcn_wmm_ac ac[4];	/* BE, BK, VI, VO as on the wire */
	const uint8_t *ies;		/* into the frame: for the supplicant */
	size_t ies_len;
};

/* EINVAL also for a success with an AID outside 1..2007. */
int wcn_parse_assoc(const struct wcn_sta *s, const uint8_t *f, size_t len,
	struct wcn_assoc_resp *r);
/* Deauthentication or Disassociation: *disassoc tells which. */
int wcn_parse_leave(const struct wcn_sta *s, const uint8_t *f, size_t len,
	uint16_t *reason, int *disassoc);

/* ---- Native wifi <-> Ethernet ---- */

#define WCN_ETH_HLEN		14
#define WCN_LLC_LEN		8
#define WCN_ETHERTYPE_EAPOL	0x888e

/*
 * An Ethernet frame (dst, src, type, payload) out to the AP: to-DS, addr1
 * the BSSID, addr2 self (the source must be self), addr3 the
 * destination; RFC 1042 SNAP, or bridge tunnel for AARP and IPX; an
 * 802.3 length frame goes with its own LLC.  protect sets the Protected
 * bit (the firmware encrypts; EAPOL before the key goes clear).  The
 * length, or 0.
 */
size_t wcn_nwifi_tx(const struct wcn_sta *s, const uint8_t *eth, size_t len,
	int protect, uint8_t *buf, size_t cap);

struct wcn_rx {
	const uint8_t *da, *sa, *bssid;
	uint16_t type;			/* ethertype; 0 for raw LLC */
	int llc;			/* payload is LLC: an 802.3 frame */
	const uint8_t *payload;
	size_t plen;
};

#define WCN_RX_OK		0
#define WCN_RX_DROP		1	/* null data, our own echo */
#define WCN_RX_BAD		2	/* malformed, protected, to-DS only */

/* A received native wifi frame; self is our address (echo check). */
int wcn_nwifi_rx(const uint8_t *f, size_t len, const uint8_t *self,
	struct wcn_rx *r);
/* Write it as Ethernet; the length, or 0 if it does not fit or an LLC
 * payload is longer than an 802.3 length field allows. */
size_t wcn_rx_eth(const struct wcn_rx *r, uint8_t *eth, size_t cap);

#endif /* _WCN_MLME_H */
