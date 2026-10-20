/*
 * The client's MLME of the WCN3990 driver: channels, beacons, the BSS
 * table, the management frames of joining and leaving, and the
 * Ethernet <-> native wifi translation.  See wcn_mlme.h.
 *
 * Facts, not text, from IEEE 802.11-2016 (frame and element formats,
 * clauses 9.3 and 9.4) and from what Linux's mac80211 sends for ath10k
 * (port/clover/wifi/sta.md); ath10k's own derivation of the HT
 * capabilities from the firmware's words is ath10k_get_ht_cap().
 */

#include <errno.h>
#include <string.h>

#include "wcn_mlme.h"

/* ---- Little helpers ---- */

static uint16_t
le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint16_t
be16(const uint8_t *p)
{
	return (uint16_t)(p[0] << 8 | p[1]);
}

static void
put16(uint8_t *p, uint16_t v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8;
}

static const uint8_t bcast[WCN_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t rfc1042[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
static const uint8_t tunnel[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };
static const uint8_t ms_oui[3] = { 0x00, 0x50, 0xf2 };
static const uint8_t rsn_oui[3] = { 0x00, 0x0f, 0xac };

#define ETHERTYPE_AARP	0x80f3
#define ETHERTYPE_IPX	0x8137

/* ---- Channels ---- */

const struct wcn_chan wcn_chans[] = {
	{ 2412, 1, WCN_BAND_2G, 0 },	{ 2417, 2, WCN_BAND_2G, 0 },
	{ 2422, 3, WCN_BAND_2G, 0 },	{ 2427, 4, WCN_BAND_2G, 0 },
	{ 2432, 5, WCN_BAND_2G, 0 },	{ 2437, 6, WCN_BAND_2G, 0 },
	{ 2442, 7, WCN_BAND_2G, 0 },	{ 2447, 8, WCN_BAND_2G, 0 },
	{ 2452, 9, WCN_BAND_2G, 0 },	{ 2457, 10, WCN_BAND_2G, 0 },
	{ 2462, 11, WCN_BAND_2G, 0 },	{ 2467, 12, WCN_BAND_2G, 0 },
	{ 2472, 13, WCN_BAND_2G, 0 },	{ 2484, 14, WCN_BAND_2G, 0 },
	{ 5180, 36, WCN_BAND_5G, 0 },	{ 5200, 40, WCN_BAND_5G, 0 },
	{ 5220, 44, WCN_BAND_5G, 0 },	{ 5240, 48, WCN_BAND_5G, 0 },
	{ 5260, 52, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5280, 56, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5300, 60, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5320, 64, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5500, 100, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5520, 104, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5540, 108, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5560, 112, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5580, 116, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5600, 120, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5620, 124, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5640, 128, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5660, 132, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5680, 136, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5700, 140, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5720, 144, WCN_BAND_5G, WCN_CHAN_DFS },
	{ 5745, 149, WCN_BAND_5G, 0 },	{ 5765, 153, WCN_BAND_5G, 0 },
	{ 5785, 157, WCN_BAND_5G, 0 },	{ 5805, 161, WCN_BAND_5G, 0 },
	{ 5825, 165, WCN_BAND_5G, 0 },
};
const unsigned wcn_nchans = sizeof(wcn_chans) / sizeof(wcn_chans[0]);

unsigned
wcn_chan_freq(unsigned chan)
{
	unsigned i;

	for (i = 0; i < wcn_nchans; i++)
		if (wcn_chans[i].chan == chan)
			return wcn_chans[i].freq;
	return 0;
}

const struct wcn_chan *
wcn_chan_by_freq(unsigned freq)
{
	unsigned i;

	for (i = 0; i < wcn_nchans; i++)
		if (wcn_chans[i].freq == freq)
			return &wcn_chans[i];
	return NULL;
}

unsigned
wcn_freq_chan(unsigned freq, unsigned *band)
{
	const struct wcn_chan *c = wcn_chan_by_freq(freq);

	if (c == NULL)
		return 0;
	if (band != NULL)
		*band = c->band;
	return c->chan;
}

/* ---- Elements ---- */

#define IE_SSID		0
#define IE_RATES	1
#define IE_DS		3
#define IE_HT_CAP	45
#define IE_RSN		48
#define IE_XRATES	50
#define IE_HT_OP	61
#define IE_VHT_CAP	191
#define IE_VENDOR	221

#define HT_CAP_LEN	26
#define HT_OP_LEN	22
#define WMM_INFO_LEN	7
#define WMM_PARAM_LEN	24

/* The first instance of each element we read; the rest is passed on. */
struct ies {
	const uint8_t *ssid, *rates, *xrates, *ds, *rsn, *htcap, *htop;
	const uint8_t *wmm;
	size_t ssid_len, nrates, nxrates, rsn_len, wmm_len;
	int has_ssid, has_vht, wpa1;
};

/* 0, or -1 if an element runs past the end. */
static int
ie_walk(const uint8_t *p, size_t n, struct ies *e)
{
	size_t off = 0, l;
	const uint8_t *v;

	memset(e, 0, sizeof(*e));
	while (off < n) {
		if (n - off < 2)
			return -1;
		l = p[off + 1];
		if (l > n - off - 2)
			return -1;
		v = p + off + 2;
		switch (p[off]) {
		case IE_SSID:
			if (!e->has_ssid) {
				e->has_ssid = 1;
				e->ssid = v;
				e->ssid_len = l;
			}
			break;
		case IE_RATES:
			if (e->rates == NULL && l > 0) {
				e->rates = v;
				e->nrates = l;
			}
			break;
		case IE_XRATES:
			if (e->xrates == NULL && l > 0) {
				e->xrates = v;
				e->nxrates = l;
			}
			break;
		case IE_DS:
			if (e->ds == NULL && l >= 1)
				e->ds = v;
			break;
		case IE_RSN:
			if (e->rsn == NULL) {
				e->rsn = v;
				e->rsn_len = l;
			}
			break;
		case IE_HT_CAP:
			if (e->htcap == NULL && l >= HT_CAP_LEN)
				e->htcap = v;
			break;
		case IE_HT_OP:
			if (e->htop == NULL && l >= HT_OP_LEN)
				e->htop = v;
			break;
		case IE_VHT_CAP:
			if (l >= 12)
				e->has_vht = 1;
			break;
		case IE_VENDOR:
			if (l < 4 || memcmp(v, ms_oui, 3) != 0)
				break;
			if (v[3] == 1 && l >= 6)
				e->wpa1 = 1;
			else if (v[3] == 2 && l >= WMM_INFO_LEN &&
			    v[5] == 1 && e->wmm == NULL &&
			    ((v[4] == 0 && l >= WMM_INFO_LEN) ||
			    (v[4] == 1 && l >= WMM_PARAM_LEN))) {
				e->wmm = v;
				e->wmm_len = l;
			}
			break;
		}
		off += 2 + l;
	}
	return 0;
}

/* Supported then extended rates into one list. */
static unsigned
rates_merge(const struct ies *e, uint8_t *out)
{
	unsigned n = 0;
	size_t i;

	for (i = 0; i < e->nrates && n < WCN_RATES_MAX; i++)
		out[n++] = e->rates[i];
	for (i = 0; i < e->nxrates && n < WCN_RATES_MAX; i++)
		out[n++] = e->xrates[i];
	return n;
}

static uint8_t
suite(const uint8_t *p)
{
	if (memcmp(p, rsn_oui, 3) != 0 || p[3] >= WCN_SUITE_OTHER)
		return WCN_SUITE_OTHER;
	return p[3];
}

/*
 * RSN element body (9.4.2.25).  Fields may end after any of them; what is
 * missing takes the standard's default (CCMP, CCMP, 802.1X).  A count
 * running past the end makes it invalid.
 */
static void
rsn_parse(const uint8_t *p, size_t n, struct wcn_rsn *r)
{
	size_t off, cnt, i;

	memset(r, 0, sizeof(*r));
	r->present = 1;
	if (n < 2 || le16(p) != 1)
		return;
	r->group = WCN_CIPHER_CCMP;
	r->pairwise = WCN_BIT(WCN_CIPHER_CCMP);
	r->akm = WCN_BIT(WCN_AKM_8021X);
	off = 2;
	if (off == n)
		goto done;
	if (n - off < 4)
		return;
	r->group = suite(p + off);
	off += 4;
	if (off == n)
		goto done;
	if (n - off < 2)
		return;
	cnt = le16(p + off);
	off += 2;
	if (cnt == 0 || cnt > (n - off) / 4)
		return;
	r->pairwise = 0;
	for (i = 0; i < cnt; i++, off += 4)
		r->pairwise |= WCN_BIT(suite(p + off));
	if (off == n)
		goto done;
	if (n - off < 2)
		return;
	cnt = le16(p + off);
	off += 2;
	if (cnt == 0 || cnt > (n - off) / 4)
		return;
	r->akm = 0;
	for (i = 0; i < cnt; i++, off += 4)
		r->akm |= WCN_BIT(suite(p + off));
	if (off == n)
		goto done;
	if (n - off < 2)
		return;
	r->caps = le16(p + off);
	off += 2;
	if (off == n)
		goto done;
	if (n - off < 2)
		return;
	cnt = le16(p + off);		/* PMKIDs */
	off += 2;
	if (cnt > (n - off) / 16)
		return;
	off += cnt * 16;
	if (off == n)
		goto done;
	if (n - off < 4)		/* group management cipher */
		return;
	/* Anything after it belongs to a later revision: accepted. */
done:
	r->valid = 1;
}

static enum wcn_sec
classify(const struct wcn_bss *b)
{
	const struct wcn_rsn *r = &b->rsn;
	uint32_t psk, sae, ent;

	if (r->present) {
		if (!r->valid)
			return WCN_SEC_OTHER;
		psk = r->akm & (WCN_BIT(WCN_AKM_PSK) |
		    WCN_BIT(WCN_AKM_PSK_SHA256));
		sae = r->akm & (WCN_BIT(WCN_AKM_SAE) |
		    WCN_BIT(WCN_AKM_SAE_EXT));
		ent = r->akm & (WCN_BIT(WCN_AKM_8021X) |
		    WCN_BIT(WCN_AKM_8021X_SHA256) |
		    WCN_BIT(WCN_AKM_FT_8021X));
		if (!(r->pairwise & WCN_BIT(WCN_CIPHER_CCMP)))
			return sae ? WCN_SEC_WPA3 : WCN_SEC_OTHER;
		if (psk && sae)
			return WCN_SEC_WPA2_WPA3;
		if (psk)
			return WCN_SEC_WPA2_PSK;
		if (sae)
			return WCN_SEC_WPA3;
		if (ent)
			return WCN_SEC_8021X;
		return WCN_SEC_OTHER;
	}
	if (b->wpa1)
		return WCN_SEC_WPA1;
	if (b->capab & WCN_CAP_PRIVACY)
		return WCN_SEC_WEP;
	return WCN_SEC_OPEN;
}

static void
ht_from(const struct ies *e, uint8_t *ht, uint16_t *capinfo, uint8_t *ampdu,
	uint8_t *mcs, uint8_t *op, uint8_t *primary, uint8_t *secoff,
	uint8_t *w40)
{
	if (e->htcap != NULL) {
		*ht = 1;
		*capinfo = le16(e->htcap);
		*ampdu = e->htcap[2];
		memcpy(mcs, e->htcap + 3, 4);
	}
	if (e->htop != NULL) {
		*op = 1;
		*primary = e->htop[0];
		*secoff = e->htop[1] & 3;
		*w40 = (e->htop[1] >> 2) & 1;
	}
}

/* ---- Beacons ---- */

#define BEACON_FIXED	12	/* timestamp, interval, capability */

int
wcn_bss_parse(const uint8_t *f, size_t len, unsigned rx_chan, int rssi,
	uint32_t now, struct wcn_bss *b)
{
	struct ies e;
	const uint8_t *ie;
	size_t n, i;
	unsigned chan, band;

	if (len < WCN_HDR_LEN + BEACON_FIXED)
		return EINVAL;
	if ((f[0] != WCN_FC_BEACON && f[0] != WCN_FC_PROBE_RESP) ||
	    (f[1] & WCN_FC1_PROTECTED))
		return EINVAL;
	ie = f + WCN_HDR_LEN + BEACON_FIXED;
	n = len - WCN_HDR_LEN - BEACON_FIXED;
	if (ie_walk(ie, n, &e) != 0)
		return EINVAL;
	if (!e.has_ssid || e.ssid_len > 32)
		return EINVAL;

	memset(b, 0, sizeof(*b));
	memcpy(b->bssid, f + 16, WCN_ALEN);
	b->probe_resp = f[0] == WCN_FC_PROBE_RESP;
	b->tsf = 0;
	for (i = 0; i < 8; i++)
		b->tsf |= (uint64_t)f[WCN_HDR_LEN + i] << (8 * i);
	b->bintval = le16(f + WCN_HDR_LEN + 8);
	b->capab = le16(f + WCN_HDR_LEN + 10);
	b->ssid_len = (uint8_t)e.ssid_len;
	memcpy(b->ssid, e.ssid, e.ssid_len);
	b->hidden = 1;
	for (i = 0; i < e.ssid_len; i++)
		if (e.ssid[i] != 0)
			b->hidden = 0;
	b->nrates = (uint8_t)rates_merge(&e, b->rates);

	ht_from(&e, &b->ht, &b->ht_capinfo, &b->ht_ampdu, b->ht_mcs,
	    &b->ht_op, &b->ht_primary, &b->ht_secoff, &b->ht_width40);
	b->vht = (uint8_t)e.has_vht;
	b->wmm = e.wmm != NULL;
	b->wpa1 = (uint8_t)e.wpa1;
	if (e.rsn != NULL)
		rsn_parse(e.rsn, e.rsn_len, &b->rsn);

	/* The AP's word on its channel before the one we heard it on: on
	 * 2.4 GHz neighbours leak into each other. */
	if (e.ds != NULL && wcn_chan_freq(e.ds[0]) != 0)
		chan = e.ds[0];
	else if (b->ht_op && wcn_chan_freq(b->ht_primary) != 0)
		chan = b->ht_primary;
	else
		chan = rx_chan;
	b->freq = (uint16_t)wcn_chan_freq(chan);
	if (b->freq != 0 && wcn_freq_chan(b->freq, &band) != 0) {
		b->chan = (uint8_t)chan;
		b->band = (uint8_t)band;
	}

	b->sec = classify(b);
	if (b->rsn.valid) {
		b->pmf_required = (b->rsn.caps & WCN_RSN_CAP_MFPR) != 0;
		b->pmf_capable = (b->rsn.caps & WCN_RSN_CAP_MFPC) != 0;
	}
	b->rssi = (int8_t)(rssi < -128 ? -128 : rssi > 127 ? 127 : rssi);
	b->seen = now;
	b->ies_len = (uint16_t)(n < WCN_BSS_IES ? n : WCN_BSS_IES);
	b->ies_trunc = n > WCN_BSS_IES;
	memcpy(b->ies, ie, b->ies_len);
	return 0;
}

int
wcn_bss_usable(const struct wcn_bss *b)
{
	uint8_t r[WCN_RATES_MAX];

	if (b->freq == 0)
		return 0;
	switch (b->sec) {
	case WCN_SEC_OPEN:
		break;
	case WCN_SEC_WPA2_PSK:
	case WCN_SEC_WPA2_WPA3:
		if (b->pmf_required)
			return 0;
		if (b->rsn.group != WCN_CIPHER_CCMP &&
		    b->rsn.group != WCN_CIPHER_TKIP)
			return 0;
		break;
	default:
		return 0;
	}
	return wcn_rates_join(b, b->ht, r, sizeof(r)) > 0;
}

/* Is (lower, lower + 4) an HT40 pair of 5 GHz? */
static int
pair5(unsigned lower)
{
	if (lower >= 36 && lower <= 140)
		return (lower - 36) % 8 == 0;
	if (lower >= 149 && lower <= 157)
		return (lower - 149) % 8 == 0;
	return 0;
}

void
wcn_bss_chandef(const struct wcn_bss *b, int want40, unsigned *freq,
	unsigned *center, unsigned *width)
{
	unsigned sec, sfreq, lower;

	*freq = *center = b->freq;
	*width = 20;
	if (!want40 || !b->ht || !b->ht_op || !b->ht_width40 || b->freq == 0)
		return;
	if (b->ht_secoff == WCN_HT_SEC_ABOVE)
		sec = b->chan + 4u;
	else if (b->ht_secoff == WCN_HT_SEC_BELOW && b->chan > 4)
		sec = b->chan - 4u;
	else
		return;
	sfreq = wcn_chan_freq(sec);
	if (sfreq == 0 || (b->band == WCN_BAND_2G) != (sfreq < 3000))
		return;
	if (b->band == WCN_BAND_2G && (sec == 14 || b->chan == 14))
		return;
	lower = sec < b->chan ? sec : b->chan;
	if (b->band == WCN_BAND_5G && !pair5(lower))
		return;
	*center = (b->freq + sfreq) / 2;
	*width = 40;
}

/* ---- The table ---- */

void
wcn_bss_init(struct wcn_bss_table *t)
{
	memset(t->used, 0, sizeof(t->used));
}

static int
find(const struct wcn_bss_table *t, const uint8_t *bssid)
{
	int i;

	for (i = 0; i < WCN_BSS_MAX; i++)
		if (t->used[i] && memcmp(t->e[i].bssid, bssid, WCN_ALEN) == 0)
			return i;
	return -1;
}

struct wcn_bss *
wcn_bss_update(struct wcn_bss_table *t, const struct wcn_bss *b)
{
	struct wcn_bss *o;
	uint8_t ssid[32], ssid_len = 0;
	int i, k, keep = 0;

	memset(ssid, 0, sizeof(ssid));

	i = find(t, b->bssid);
	if (i >= 0) {
		o = &t->e[i];
		if (b->hidden && !o->hidden) {
			keep = 1;
			ssid_len = o->ssid_len;
			memcpy(ssid, o->ssid, sizeof(ssid));
		}
	} else {
		for (k = 0; k < WCN_BSS_MAX && t->used[k]; k++)
			;
		if (k == WCN_BSS_MAX) {
			/* The oldest goes, by the newcomer's clock. */
			for (k = 0, i = 1; i < WCN_BSS_MAX; i++)
				if ((uint32_t)(b->seen - t->e[i].seen) >
				    (uint32_t)(b->seen - t->e[k].seen))
					k = i;
		}
		i = k;
	}
	o = &t->e[i];
	memcpy(o, b, sizeof(*o));
	t->used[i] = 1;
	if (keep) {
		o->ssid_len = ssid_len;
		memcpy(o->ssid, ssid, sizeof(ssid));
		o->hidden = 0;
	}
	return o;
}

const struct wcn_bss *
wcn_bss_find(const struct wcn_bss_table *t, const uint8_t *bssid)
{
	int i = find(t, bssid);

	return i < 0 ? NULL : &t->e[i];
}

void
wcn_bss_expire(struct wcn_bss_table *t, uint32_t now, uint32_t maxage)
{
	int i;

	for (i = 0; i < WCN_BSS_MAX; i++)
		if (t->used[i] && (uint32_t)(now - t->e[i].seen) > maxage)
			t->used[i] = 0;
}

const struct wcn_bss *
wcn_bss_best(const struct wcn_bss_table *t, const uint8_t *ssid,
	size_t ssid_len, unsigned secmask)
{
	const struct wcn_bss *best = NULL, *b;
	int i;

	for (i = 0; i < WCN_BSS_MAX; i++) {
		if (!t->used[i])
			continue;
		b = &t->e[i];
		if (b->hidden || b->ssid_len != ssid_len ||
		    memcmp(b->ssid, ssid, ssid_len) != 0)
			continue;
		if (!(secmask & WCN_BIT(b->sec)) || !wcn_bss_usable(b))
			continue;
		if (best == NULL || b->rssi > best->rssi)
			best = b;
	}
	return best;
}

/* ---- Rates ---- */

/* Ours, in 500 kbit/s, in the order ath10k lists them. */
static const uint8_t rates2[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72,
	96, 108 };
static const uint8_t rates5[] = { 12, 18, 24, 36, 48, 72, 96, 108 };

#define SEL_HT		127	/* BSS membership selector: HT PHY */

int
wcn_rates_join(const struct wcn_bss *b, int ht, uint8_t *out, unsigned cap)
{
	const uint8_t *ours;
	unsigned nours, i, k, n = 0;
	uint8_t v;
	int have;

	if (b->band == WCN_BAND_5G) {
		ours = rates5;
		nours = sizeof(rates5);
	} else {
		ours = rates2;
		nours = sizeof(rates2);
	}
	for (i = 0; i < b->nrates; i++) {
		if (!(b->rates[i] & 0x80))
			continue;
		v = b->rates[i] & 0x7f;
		if (v == SEL_HT) {
			if (!ht)
				return -1;
			continue;
		}
		for (k = 0; k < nours && ours[k] != v; k++)
			;
		if (k == nours)
			return -1;	/* a basic rate (or selector) not ours */
	}
	for (k = 0; k < nours; k++) {
		have = 0;
		for (i = 0; i < b->nrates; i++)
			if ((b->rates[i] & 0x7f) == ours[k])
				have = 1;
		if (!have)
			continue;
		if (n >= cap)
			return -1;
		out[n++] = ours[k];
	}
	return n > 0 ? (int)n : -1;
}

void
wcn_rates_peer(const uint8_t *join, unsigned n, uint8_t *out)
{
	unsigned i;
	uint8_t v;

	for (i = 0; i < n; i++) {
		v = join[i] & 0x7f;
		out[i] = v | ((v == 2 || v == 4 || v == 11 || v == 22) ?
		    0x80 : 0);
	}
}

/* ---- HT capabilities ---- */

#define FW_HT_ENABLED	0x0001
#define FW_HT_SGI20	0x0002
#define FW_HT_DSMPS	0x0004
#define FW_HT_RX_STBC	0x0030
#define FW_HT_LDPC	0x0040
#define FW_HT_LSIG	0x0080
#define FW_HT_SGI40	0x0800
#define FW_HT_RX_LDPC	0x1000
#define FW_HT_TX_LDPC	0x2000
#define FW_VHT_MPDU	0x0003

#define HTC_LDPC	0x0001
#define HTC_40		0x0002
#define HTC_SMPS_SHIFT	2
#define HTC_SMPS_OFF	3
#define HTC_SMPS_DYN	1
#define HTC_SGI20	0x0020
#define HTC_SGI40	0x0040
#define HTC_RX_STBC	0x0300
#define HTC_AMSDU	0x0800
#define HTC_DSSS40	0x1000
#define HTC_LSIG	0x8000

/* A-MPDU: 64 KiB (factor 3), density 8 us (6) - ath10k's choice. */
#define HT_AMPDU_PARAMS	(3 | 6 << 2)

uint16_t
wcn_ht_capinfo(uint32_t fw_ht, uint32_t fw_vht)
{
	uint16_t cap;

	if (!(fw_ht & FW_HT_ENABLED))
		return 0;
	cap = HTC_40 | HTC_DSSS40 | HTC_SMPS_OFF << HTC_SMPS_SHIFT;
	if (fw_ht & FW_HT_SGI20)
		cap |= HTC_SGI20;
	if (fw_ht & FW_HT_SGI40)
		cap |= HTC_SGI40;
	/* ath10k ORs "dynamic" over "disabled", which leaves "disabled". */
	if (fw_ht & FW_HT_DSMPS)
		cap |= HTC_SMPS_DYN << HTC_SMPS_SHIFT;
	/* Transmit STBC needs a second chain: never with one. */
	if (fw_ht & FW_HT_RX_STBC)
		cap |= (((fw_ht & FW_HT_RX_STBC) >> 4) << 8) & HTC_RX_STBC;
	if ((fw_ht & FW_HT_LDPC) ||
	    ((fw_ht & FW_HT_RX_LDPC) && (fw_ht & FW_HT_TX_LDPC)))
		cap |= HTC_LDPC;
	if (fw_ht & FW_HT_LSIG)
		cap |= HTC_LSIG;
	if (fw_vht & FW_VHT_MPDU)
		cap |= HTC_AMSDU;
	return cap;
}

/* ---- Building ---- */

static void
mgmt_hdr(const struct wcn_sta *s, uint8_t fc0, uint8_t *buf)
{
	buf[0] = fc0;
	buf[1] = 0;
	put16(buf + 2, 0);	/* duration: the hardware's [?] */
	memcpy(buf + 4, s->bssid, WCN_ALEN);
	memcpy(buf + 10, s->self, WCN_ALEN);
	memcpy(buf + 16, s->bssid, WCN_ALEN);
	put16(buf + 22, (uint16_t)((s->seq & 0xfff) << 4));
}

static void
seq_next(struct wcn_sta *s)
{
	s->seq = (s->seq + 1) & 0xfff;
}

size_t
wcn_build_auth(struct wcn_sta *s, uint8_t *buf, size_t cap)
{
	if (cap < WCN_HDR_LEN + 6)
		return 0;
	mgmt_hdr(s, WCN_FC_AUTH, buf);
	put16(buf + WCN_HDR_LEN, 0);			/* open system */
	put16(buf + WCN_HDR_LEN + 2, 1);		/* sequence 1 */
	put16(buf + WCN_HDR_LEN + 4, WCN_STATUS_SUCCESS);
	seq_next(s);
	return WCN_HDR_LEN + 6;
}

static size_t
leave(struct wcn_sta *s, uint8_t fc0, uint16_t reason, uint8_t *buf,
	size_t cap)
{
	if (cap < WCN_HDR_LEN + 2)
		return 0;
	mgmt_hdr(s, fc0, buf);
	put16(buf + WCN_HDR_LEN, reason);
	seq_next(s);
	return WCN_HDR_LEN + 2;
}

size_t
wcn_build_deauth(struct wcn_sta *s, uint16_t reason, uint8_t *buf,
	size_t cap)
{
	return leave(s, WCN_FC_DEAUTH, reason, buf, cap);
}

size_t
wcn_build_disassoc(struct wcn_sta *s, uint16_t reason, uint8_t *buf,
	size_t cap)
{
	return leave(s, WCN_FC_DISASSOC, reason, buf, cap);
}

/* Append an element; 0 if it does not fit. */
static int
put_ie(uint8_t *buf, size_t cap, size_t *off, uint8_t id, const void *v,
	size_t l)
{
	if (l > 255 || cap - *off < 2 + l)
		return 0;
	buf[*off] = id;
	buf[*off + 1] = (uint8_t)l;
	memcpy(buf + *off + 2, v, l);
	*off += 2 + l;
	return 1;
}

static int
put_raw(uint8_t *buf, size_t cap, size_t *off, const uint8_t *v, size_t l)
{
	if (cap - *off < l)
		return 0;
	memcpy(buf + *off, v, l);
	*off += l;
	return 1;
}

size_t
wcn_build_assoc(struct wcn_sta *s, const struct wcn_bss *b,
	const struct wcn_assoc_opts *o, uint8_t *buf, size_t cap)
{
	static const uint8_t wmm_info[WMM_INFO_LEN] = {
		0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00	/* no U-APSD */
	};
	uint8_t rates[WCN_RATES_MAX], ht[HT_CAP_LEN];
	uint16_t capab, htc;
	size_t off;
	int n, useht, vendor_rsn = 0;

	if (cap < WCN_HDR_LEN + 4 || b->ssid_len == 0 || b->hidden)
		return 0;
	if (o->rsn_ie != NULL) {
		if (o->rsn_len < 2 || (size_t)o->rsn_ie[1] + 2 != o->rsn_len ||
		    (o->rsn_ie[0] != IE_RSN && o->rsn_ie[0] != IE_VENDOR))
			return 0;
		vendor_rsn = o->rsn_ie[0] == IE_VENDOR;
	}
	/* No HT with TKIP or WEP pairwise (802.11-2016 12.6.12, as
	 * mac80211). */
	useht = o->ht_capinfo != 0 && b->ht &&
	    (b->sec == WCN_SEC_OPEN || (b->rsn.valid &&
	    (b->rsn.pairwise & WCN_BIT(WCN_CIPHER_CCMP))));
	n = wcn_rates_join(b, useht, rates, sizeof(rates));
	if (n < 0)
		return 0;

	mgmt_hdr(s, WCN_FC_ASSOC_REQ, buf);
	capab = WCN_CAP_ESS | (b->capab & WCN_CAP_PRIVACY);
	if (b->band == WCN_BAND_2G)
		capab |= WCN_CAP_SHORT_SLOT | WCN_CAP_SHORT_PREAMBLE;
	put16(buf + WCN_HDR_LEN, capab);
	put16(buf + WCN_HDR_LEN + 2, o->listen ? o->listen :
	    WCN_LISTEN_INTERVAL);
	off = WCN_HDR_LEN + 4;

	if (!put_ie(buf, cap, &off, IE_SSID, b->ssid, b->ssid_len))
		return 0;
	if (!put_ie(buf, cap, &off, IE_RATES, rates, n > 8 ? 8 : n))
		return 0;
	if (n > 8 && !put_ie(buf, cap, &off, IE_XRATES, rates + 8, n - 8))
		return 0;
	if (o->rsn_ie != NULL && !vendor_rsn &&
	    !put_raw(buf, cap, &off, o->rsn_ie, o->rsn_len))
		return 0;
	if (useht) {
		htc = o->ht_capinfo;
		if (!o->ht40)
			htc &= ~(HTC_40 | HTC_SGI40);
		memset(ht, 0, sizeof(ht));
		put16(ht, htc);
		ht[2] = HT_AMPDU_PARAMS;
		ht[3] = 0xff;				/* MCS 0-7 */
		if (o->nss >= 2)
			ht[4] = 0xff;			/* MCS 8-15 */
		ht[15] = 0x01;				/* TX MCS set defined */
		if (!put_ie(buf, cap, &off, IE_HT_CAP, ht, sizeof(ht)))
			return 0;
	}
	if (o->rsn_ie != NULL && vendor_rsn &&
	    !put_raw(buf, cap, &off, o->rsn_ie, o->rsn_len))
		return 0;
	if (b->wmm && !put_ie(buf, cap, &off, IE_VENDOR, wmm_info,
	    sizeof(wmm_info)))
		return 0;
	seq_next(s);
	return off;
}

/* ---- Parsing ---- */

int
wcn_mgmt_parse(const uint8_t *f, size_t len, struct wcn_mgmt *m)
{
	if (len < WCN_HDR_LEN)
		return EINVAL;
	if ((f[0] & 0x0f) != 0 || (f[1] & WCN_FC1_PROTECTED))
		return EINVAL;
	m->subtype = f[0];
	m->fc1 = f[1];
	m->da = f + 4;
	m->sa = f + 10;
	m->bssid = f + 16;
	m->seqctl = le16(f + 22);
	m->body = f + WCN_HDR_LEN;
	m->blen = len - WCN_HDR_LEN;
	return 0;
}

static int
for_us(const struct wcn_sta *s, const struct wcn_mgmt *m, int bc)
{
	if (s == NULL)
		return 1;
	if (memcmp(m->da, s->self, WCN_ALEN) != 0 &&
	    !(bc && memcmp(m->da, bcast, WCN_ALEN) == 0))
		return 0;
	return memcmp(m->bssid, s->bssid, WCN_ALEN) == 0 &&
	    memcmp(m->sa, s->bssid, WCN_ALEN) == 0;
}

int
wcn_parse_auth(const struct wcn_sta *s, const uint8_t *f, size_t len,
	struct wcn_auth_resp *r)
{
	struct wcn_mgmt m;

	if (wcn_mgmt_parse(f, len, &m) != 0 || m.subtype != WCN_FC_AUTH ||
	    m.blen < 6 || !for_us(s, &m, 0))
		return EINVAL;
	r->alg = le16(m.body);
	r->seq = le16(m.body + 2);
	r->status = le16(m.body + 4);
	return 0;
}

#define AID_MAX		2007

int
wcn_parse_assoc(const struct wcn_sta *s, const uint8_t *f, size_t len,
	struct wcn_assoc_resp *r)
{
	struct wcn_mgmt m;
	struct ies e;
	unsigned i;

	if (wcn_mgmt_parse(f, len, &m) != 0 ||
	    (m.subtype != WCN_FC_ASSOC_RESP &&
	    m.subtype != WCN_FC_REASSOC_RESP) ||
	    m.blen < 6 || !for_us(s, &m, 0))
		return EINVAL;
	if (ie_walk(m.body + 6, m.blen - 6, &e) != 0)
		return EINVAL;
	memset(r, 0, sizeof(*r));
	r->reassoc = m.subtype == WCN_FC_REASSOC_RESP;
	r->capab = le16(m.body);
	r->status = le16(m.body + 2);
	r->aid = le16(m.body + 4) & 0x3fff;
	if (r->status == WCN_STATUS_SUCCESS &&
	    (r->aid == 0 || r->aid > AID_MAX))
		return EINVAL;
	r->nrates = (uint8_t)rates_merge(&e, r->rates);
	ht_from(&e, &r->ht, &r->ht_capinfo, &r->ht_ampdu, r->ht_mcs,
	    &r->ht_op, &r->ht_primary, &r->ht_secoff, &r->ht_width40);
	r->vht = (uint8_t)e.has_vht;
	if (e.wmm != NULL && e.wmm[4] == 1) {
		r->wmm = 1;
		r->wmm_qosinfo = e.wmm[6];
		for (i = 0; i < 4; i++) {
			r->ac[i].aci_aifsn = e.wmm[8 + 4 * i];
			r->ac[i].ecw = e.wmm[9 + 4 * i];
			r->ac[i].txop = le16(e.wmm + 10 + 4 * i);
		}
	}
	r->ies = m.body + 6;
	r->ies_len = m.blen - 6;
	return 0;
}

int
wcn_parse_leave(const struct wcn_sta *s, const uint8_t *f, size_t len,
	uint16_t *reason, int *disassoc)
{
	struct wcn_mgmt m;

	if (wcn_mgmt_parse(f, len, &m) != 0 ||
	    (m.subtype != WCN_FC_DEAUTH && m.subtype != WCN_FC_DISASSOC) ||
	    m.blen < 2 || !for_us(s, &m, 1))
		return EINVAL;
	*reason = le16(m.body);
	*disassoc = m.subtype == WCN_FC_DISASSOC;
	return 0;
}

/* ---- Native wifi ---- */

size_t
wcn_nwifi_tx(const struct wcn_sta *s, const uint8_t *eth, size_t len,
	int protect, uint8_t *buf, size_t cap)
{
	const uint8_t *pl;
	size_t plen, need;
	uint16_t type;

	if (len < WCN_ETH_HLEN || memcmp(eth + 6, s->self, WCN_ALEN) != 0)
		return 0;
	type = be16(eth + 12);
	pl = eth + WCN_ETH_HLEN;
	plen = len - WCN_ETH_HLEN;
	if (type >= 0x600) {
		need = WCN_HDR_LEN + WCN_LLC_LEN + plen;
	} else if (type <= 1500) {
		/* 802.3: the length field says how much is LLC and data;
		 * the rest is padding to the Ethernet minimum. */
		if (type < 3 || type > plen)
			return 0;
		plen = type;
		need = WCN_HDR_LEN + plen;
	} else
		return 0;
	if (cap < need)
		return 0;

	buf[0] = WCN_FC_DATA;
	buf[1] = WCN_FC1_TODS | (protect ? WCN_FC1_PROTECTED : 0);
	put16(buf + 2, 0);
	memcpy(buf + 4, s->bssid, WCN_ALEN);
	memcpy(buf + 10, s->self, WCN_ALEN);
	memcpy(buf + 16, eth, WCN_ALEN);
	put16(buf + 22, 0);		/* the firmware numbers data */
	if (type >= 0x600) {
		memcpy(buf + WCN_HDR_LEN, (type == ETHERTYPE_AARP ||
		    type == ETHERTYPE_IPX) ? tunnel : rfc1042, 6);
		buf[WCN_HDR_LEN + 6] = type >> 8;
		buf[WCN_HDR_LEN + 7] = type & 0xff;
		memcpy(buf + WCN_HDR_LEN + WCN_LLC_LEN, pl, plen);
	} else
		memcpy(buf + WCN_HDR_LEN, pl, plen);
	return need;
}

int
wcn_nwifi_rx(const uint8_t *f, size_t len, const uint8_t *self,
	struct wcn_rx *r)
{
	size_t hl = WCN_HDR_LEN;
	const uint8_t *body;
	size_t blen;
	uint8_t fc0, fc1;
	uint16_t type;

	if (len < WCN_HDR_LEN)
		return WCN_RX_BAD;
	fc0 = f[0];
	fc1 = f[1];
	if ((fc0 & 0x0f) != WCN_FC_DATA || (fc1 & WCN_FC1_PROTECTED))
		return WCN_RX_BAD;
	switch (fc1 & (WCN_FC1_TODS | WCN_FC1_FROMDS)) {
	case WCN_FC1_FROMDS:
		r->da = f + 4;
		r->bssid = f + 10;
		r->sa = f + 16;
		break;
	case 0:				/* no DS: not from an AP [?] */
		r->da = f + 4;
		r->sa = f + 10;
		r->bssid = f + 16;
		break;
	case WCN_FC1_TODS:
		return WCN_RX_BAD;	/* a station's frame, not for us */
	default:			/* four addresses */
		hl = 30;
		if (len < hl)
			return WCN_RX_BAD;
		r->da = f + 16;
		r->bssid = f + 10;
		r->sa = f + 24;
		break;
	}
	if (fc0 & 0x80) {		/* QoS: native wifi has none, but */
		hl += 2;
		if (len < hl || (f[hl - 2] & 0x80))	/* A-MSDU: not here */
			return WCN_RX_BAD;
		if (fc1 & WCN_FC1_ORDER)
			hl += 4;	/* HT Control */
	}
	if (len < hl)
		return WCN_RX_BAD;
	if (fc0 & 0x40)			/* null function: no data */
		return WCN_RX_DROP;
	if (self != NULL && memcmp(r->sa, self, WCN_ALEN) == 0)
		return WCN_RX_DROP;	/* our broadcast, echoed by the AP */
	body = f + hl;
	blen = len - hl;
	if (blen == 0)
		return WCN_RX_BAD;
	r->llc = 1;
	r->type = 0;
	r->payload = body;
	r->plen = blen;
	if (blen >= WCN_LLC_LEN) {
		type = be16(body + 6);
		if (((memcmp(body, rfc1042, 6) == 0 &&
		    type != ETHERTYPE_AARP && type != ETHERTYPE_IPX) ||
		    memcmp(body, tunnel, 6) == 0) && type >= 0x600) {
			r->llc = 0;
			r->type = type;
			r->payload = body + WCN_LLC_LEN;
			r->plen = blen - WCN_LLC_LEN;
		}
	}
	return WCN_RX_OK;
}

size_t
wcn_rx_eth(const struct wcn_rx *r, uint8_t *eth, size_t cap)
{
	size_t need = WCN_ETH_HLEN + r->plen;
	uint16_t t;

	if (r->llc && r->plen > 1500)
		return 0;
	if (cap < need)
		return 0;
	t = r->llc ? (uint16_t)r->plen : r->type;
	memcpy(eth, r->da, WCN_ALEN);
	memcpy(eth + 6, r->sa, WCN_ALEN);
	eth[12] = t >> 8;
	eth[13] = t & 0xff;
	memcpy(eth + WCN_ETH_HLEN, r->payload, r->plen);
	return need;
}
