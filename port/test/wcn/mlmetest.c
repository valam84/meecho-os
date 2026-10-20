/*
 * Host stand for minix/drivers/net/wcn/wcn_mlme.c: beacons of the kinds
 * an AP sends (WPA2-PSK with HT and WMM, open 5 GHz with VHT, hidden,
 * WPA2/WPA3 transition, WPA3 only, legacy 11b/g) and made to be wrong
 * (elements past the end, a 33-octet SSID, RSN counts past the end);
 * every prefix of every frame, copied into a buffer of exactly that size
 * so that AddressSanitizer sees a read past the end; random element soup;
 * built frames against bytes written out by hand from 802.11-2016; and
 * Ethernet <-> native wifi both ways (IPv4, ARP, EAPOL, AARP, an 802.3
 * frame with its own LLC).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_mlme.h"

static int fails, checks;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		fails++;						\
		printf("FAIL %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

static const uint8_t SELF[6] = { 0x8c, 0xbe, 0xbe, 0x11, 0x22, 0x33 };
static const uint8_t AP[6] = { 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0xf5 };
static const uint8_t AP2[6] = { 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0x06 };
static const uint8_t AP3[6] = { 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, 0x07 };
static const uint8_t BC[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t HOST[6] = { 0x00, 0x11, 0x32, 0x0a, 0x0b, 0x0c };

/* ---- Hex ---- */

static size_t
hex(const char *s, uint8_t *out, size_t cap)
{
	size_t k = 0;
	unsigned v;

	while (*s) {
		while (*s == ' ' || *s == '|')
			s++;
		if (!*s)
			break;
		if (sscanf(s, "%2x", &v) != 1 || k >= cap) {
			printf("bad hex at '%s'\n", s);
			exit(2);
		}
		out[k++] = (uint8_t)v;
		s += 2;
	}
	return k;
}

static int
same(const uint8_t *got, size_t n, const char *want_hex, const char *what)
{
	uint8_t want[2048];
	size_t k = hex(want_hex, want, sizeof(want)), i;

	checks++;
	if (n == k && memcmp(got, want, k) == 0)
		return 1;
	fails++;
	printf("FAIL %s:\n  got  (%zu)", what, n);
	for (i = 0; i < n; i++)
		printf(" %02x", got[i]);
	printf("\n  want (%zu)", k);
	for (i = 0; i < k; i++)
		printf(" %02x", want[i]);
	printf("\n");
	return 0;
}

/* A malloc'd copy of exactly n bytes: reading past it is ASan's. */
static uint8_t *
exact(const uint8_t *p, size_t n)
{
	uint8_t *q = malloc(n ? n : 1);

	if (q == NULL)
		exit(2);
	memcpy(q, p, n);
	return q;
}

/* ---- Elements ---- */

#define Z10 " 00 00 00 00 00 00 00 00 00 00"
#define SSID_LAB "00 0a 4d 45 45 43 48 4f 2d 6c 61 62 "	/* MEECHO-lab */
#define SSID_5 "00 08 4d 45 45 43 48 4f 2d 35 "		/* MEECHO-5 */
#define RATES_G "01 08 82 84 8b 96 0c 12 18 24 32 04 30 48 60 6c "
#define RATES_A "01 08 8c 12 98 24 b0 48 60 6c "
#define DS6 "03 01 06 "
#define TIM "05 04 00 01 00 00 "
#define COUNTRY "07 06 44 45 20 01 0d 14 "
#define RSN_PSK "30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 0c 00 "
#define RSN_MIX "30 18 01 00 00 0f ac 04 01 00 00 0f ac 04 02 00 00 0f ac 02 00 0f ac 08 80 00 "
#define RSN_SAE "30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 08 c0 00 "
#define HTCAP_AP "2d 1a ef 19 1b ff ff" Z10 Z10 " 00 "
#define HTOP6 "3d 16 06 05" Z10 Z10 " "
#define HTOP36 "3d 16 24 05" Z10 Z10 " "
#define HTOP6_20 "3d 16 06 00" Z10 Z10 " "
#define WMM_PARAM "dd 18 00 50 f2 02 01 01 80 00 03 a4 00 00 27 a4 00 00 42 43 5e 00 62 32 2f 00 "
#define VHTCAP "bf 0c 32 00 80 03 fa ff 00 00 fa ff 00 00 "
#define EXTCAP "7f 08 04 00 08 00 00 00 00 40 "

/* A beacon (or probe response): header, fixed fields, elements. */
static size_t
beacon(uint8_t *out, uint8_t fc0, const uint8_t *bssid, uint16_t capab,
	const char *ies)
{
	size_t n = 0;

	out[n++] = fc0;
	out[n++] = 0;
	out[n++] = 0;
	out[n++] = 0;
	memcpy(out + n, fc0 == WCN_FC_BEACON ? BC : SELF, 6);
	n += 6;
	memcpy(out + n, bssid, 6);
	n += 6;
	memcpy(out + n, bssid, 6);
	n += 6;
	out[n++] = 0x30;
	out[n++] = 0x12;
	n += hex("01 02 03 04 05 06 07 08 64 00", out + n, 64);
	out[n++] = capab & 0xff;
	out[n++] = capab >> 8;
	n += hex(ies, out + n, 2048 - n);
	return n;
}

/* Every prefix of a beacon: accepted exactly at the element boundaries
 * after the SSID, refused elsewhere, never read past. */
static void
cut_beacon(const uint8_t *f, size_t n, const char *what)
{
	struct wcn_bss b;
	size_t k, off, ends[256], ne = 0, i;
	int rc, want, got = 0;
	uint8_t *c;

	for (off = 36; off + 2 <= n; off += 2 + f[off + 1])
		ends[ne++] = off + 2 + f[off + 1];
	for (k = 0; k < n; k++) {
		c = exact(f, k);
		rc = wcn_bss_parse(c, k, 0, -50, 0, &b);
		want = 0;
		for (i = 0; i < ne; i++)
			if (ends[i] == k)
				want = 1;
		if (k <= 36 || f[36] != 0)
			want = 0;		/* no SSID yet */
		CHECK((rc == 0) == want, "%s cut at %zu: rc %d, want %s", what,
		    k, rc, want ? "0" : "EINVAL");
		if (rc == 0)
			got++;
		free(c);
	}
	CHECK(got == (int)ne - 1 || got == (int)ne, "%s: %d prefixes parsed",
	    what, got);
}

static int
parse_hex_beacon(uint8_t fc0, const uint8_t *bssid, uint16_t capab,
	const char *ies, unsigned rx_chan, struct wcn_bss *b, int cut,
	const char *what)
{
	uint8_t f[2048], *c;
	size_t n = beacon(f, fc0, bssid, capab, ies);
	int rc;

	c = exact(f, n);
	rc = wcn_bss_parse(c, n, rx_chan, -40, 1000, b);
	free(c);
	if (cut)
		cut_beacon(f, n, what);
	return rc;
}

/* ---- Channels ---- */

static void
channels(void)
{
	unsigned band = 99, i;
	unsigned f, c, w;
	struct wcn_bss b;

	CHECK(wcn_chan_freq(1) == 2412, "ch 1");
	CHECK(wcn_chan_freq(13) == 2472, "ch 13");
	CHECK(wcn_chan_freq(14) == 2484, "ch 14");
	CHECK(wcn_chan_freq(36) == 5180, "ch 36");
	CHECK(wcn_chan_freq(144) == 5720, "ch 144");
	CHECK(wcn_chan_freq(149) == 5745, "ch 149");
	CHECK(wcn_chan_freq(165) == 5825, "ch 165");
	CHECK(wcn_chan_freq(0) == 0 && wcn_chan_freq(15) == 0 &&
	    wcn_chan_freq(34) == 0 && wcn_chan_freq(169) == 0, "no channel");
	CHECK(wcn_freq_chan(5745, &band) == 149 && band == WCN_BAND_5G,
	    "5745");
	CHECK(wcn_freq_chan(2437, &band) == 6 && band == WCN_BAND_2G, "2437");
	CHECK(wcn_freq_chan(2400, &band) == 0, "2400");
	CHECK(wcn_nchans == 14 + 25, "%u channels", wcn_nchans);
	for (i = 0; i < wcn_nchans; i++) {
		CHECK(wcn_chan_freq(wcn_chans[i].chan) == wcn_chans[i].freq &&
		    wcn_freq_chan(wcn_chans[i].freq, NULL) ==
		    wcn_chans[i].chan, "table %u", i);
		CHECK((wcn_chans[i].band == WCN_BAND_5G) ==
		    (wcn_chans[i].freq > 5000), "band %u", i);
		CHECK(!(wcn_chans[i].flags & WCN_CHAN_DFS) ||
		    (wcn_chans[i].chan >= 52 && wcn_chans[i].chan <= 144),
		    "dfs %u", i);
	}

	/* HT40 pairs. */
	static const struct {
		unsigned chan, sec, center, width;
	} cd[] = {
		{ 6, WCN_HT_SEC_ABOVE, 2447, 40 },
		{ 6, WCN_HT_SEC_BELOW, 2427, 40 },
		{ 1, WCN_HT_SEC_BELOW, 2412, 20 },
		{ 13, WCN_HT_SEC_ABOVE, 2472, 20 },
		{ 10, WCN_HT_SEC_ABOVE, 2457, 20 },	/* 14 is no partner */
		{ 36, WCN_HT_SEC_ABOVE, 5190, 40 },
		{ 40, WCN_HT_SEC_BELOW, 5190, 40 },
		{ 40, WCN_HT_SEC_ABOVE, 5200, 20 },	/* 40/44 no pair */
		{ 144, WCN_HT_SEC_BELOW, 5710, 40 },
		{ 149, WCN_HT_SEC_ABOVE, 5755, 40 },
		{ 161, WCN_HT_SEC_BELOW, 5795, 40 },
		{ 165, WCN_HT_SEC_ABOVE, 5825, 20 },
		{ 6, WCN_HT_SEC_NONE, 2437, 20 },
	};
	for (i = 0; i < sizeof(cd) / sizeof(cd[0]); i++) {
		memset(&b, 0, sizeof(b));
		b.chan = (uint8_t)cd[i].chan;
		b.freq = (uint16_t)wcn_chan_freq(cd[i].chan);
		(void)wcn_freq_chan(b.freq, &band);
		b.band = (uint8_t)band;
		b.ht = b.ht_op = b.ht_width40 = 1;
		b.ht_secoff = (uint8_t)cd[i].sec;
		wcn_bss_chandef(&b, 1, &f, &c, &w);
		CHECK(f == b.freq && c == cd[i].center && w == cd[i].width,
		    "chandef %u/%u: %u %u %u", cd[i].chan, cd[i].sec, f, c, w);
		wcn_bss_chandef(&b, 0, &f, &c, &w);
		CHECK(c == b.freq && w == 20, "chandef 20 %u", cd[i].chan);
	}
}

/* ---- Beacons ---- */

static void
beacons(void)
{
	struct wcn_bss b;
	unsigned f, c, w;
	int rc;

	/* WPA2-PSK, HT40 above, WMM, 2.4 GHz channel 6, heard on 7. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP, 0x0431,
	    SSID_LAB RATES_G DS6 TIM COUNTRY RSN_PSK HTCAP_AP HTOP6 EXTCAP
	    WMM_PARAM, 7, &b, 1, "wpa2");
	CHECK(rc == 0, "wpa2 rc %d", rc);
	CHECK(memcmp(b.bssid, AP, 6) == 0, "wpa2 bssid");
	CHECK(b.ssid_len == 10 && memcmp(b.ssid, "MEECHO-lab", 10) == 0 &&
	    !b.hidden, "wpa2 ssid");
	CHECK(b.chan == 6 && b.freq == 2437 && b.band == WCN_BAND_2G,
	    "wpa2 chan %u", b.chan);
	CHECK(b.capab == 0x0431 && b.bintval == 100 &&
	    b.tsf == 0x0807060504030201ull, "wpa2 fixed");
	CHECK(b.nrates == 12 && b.rates[0] == 0x82 && b.rates[11] == 0x6c,
	    "wpa2 rates %u", b.nrates);
	CHECK(b.rsn.present && b.rsn.valid && b.rsn.group == WCN_CIPHER_CCMP &&
	    b.rsn.pairwise == WCN_BIT(WCN_CIPHER_CCMP) &&
	    b.rsn.akm == WCN_BIT(WCN_AKM_PSK) && b.rsn.caps == 0x000c,
	    "wpa2 rsn");
	CHECK(b.sec == WCN_SEC_WPA2_PSK && !b.pmf_required && !b.pmf_capable,
	    "wpa2 sec %d", b.sec);
	CHECK(b.ht && b.ht_capinfo == 0x19ef && b.ht_ampdu == 0x1b &&
	    b.ht_mcs[0] == 0xff && b.ht_mcs[1] == 0xff, "wpa2 htcap");
	CHECK(b.ht_op && b.ht_primary == 6 && b.ht_secoff == WCN_HT_SEC_ABOVE &&
	    b.ht_width40, "wpa2 htop");
	CHECK(!b.vht && b.wmm && !b.wpa1, "wpa2 vht/wmm");
	CHECK(b.rssi == -40 && b.seen == 1000 && !b.probe_resp, "wpa2 meta");
	CHECK(b.ies_len == 2 + 10 + 16 + 3 + 6 + 8 + 22 + 28 + 24 + 10 + 26 &&
	    !b.ies_trunc && b.ies[0] == 0 && b.ies[1] == 10, "wpa2 ies %u",
	    b.ies_len);
	CHECK(wcn_bss_usable(&b), "wpa2 usable");
	wcn_bss_chandef(&b, 1, &f, &c, &w);
	CHECK(f == 2437 && c == 2447 && w == 40, "wpa2 chandef");

	/* Open, 5 GHz, no DS element: HT Operation's 36 beats rx 40. */
	rc = parse_hex_beacon(WCN_FC_PROBE_RESP, AP2, 0x0001,
	    SSID_5 RATES_A HTCAP_AP HTOP36 VHTCAP, 40, &b, 1, "open5");
	CHECK(rc == 0 && b.chan == 36 && b.freq == 5180 &&
	    b.band == WCN_BAND_5G, "open5 chan %u", b.chan);
	CHECK(b.sec == WCN_SEC_OPEN && b.vht && !b.wmm && b.probe_resp &&
	    !b.rsn.present, "open5 sec");
	CHECK(wcn_bss_usable(&b), "open5 usable");

	/* Legacy 11b/g on 11, heard on 10, no HT: the DS element wins. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0021,
	    "00 03 6f 6c 64 01 04 82 84 8b 96 03 01 0b", 10, &b, 1, "legacy");
	CHECK(rc == 0 && b.chan == 11 && !b.ht && !b.ht_op &&
	    b.sec == WCN_SEC_OPEN && wcn_bss_usable(&b), "legacy chan %u",
	    b.chan);

	/* No channel at all: rx says 0. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0021,
	    "00 03 6f 6c 64 01 01 82", 0, &b, 0, "nochan");
	CHECK(rc == 0 && b.chan == 0 && b.freq == 0 && !wcn_bss_usable(&b),
	    "nochan");
	/* A DS element naming no channel: fall back to rx. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0021,
	    "00 03 6f 6c 64 01 01 82 03 01 63", 3, &b, 0, "ds99");
	CHECK(rc == 0 && b.chan == 3, "ds99 %u", b.chan);

	/* Hidden: empty, and all zeros. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP, 0x0431,
	    "00 00 " RATES_G DS6 RSN_PSK, 0, &b, 1, "hidden");
	CHECK(rc == 0 && b.hidden && b.ssid_len == 0, "hidden");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP, 0x0431,
	    "00 05 00 00 00 00 00 " RATES_G DS6 RSN_PSK, 0, &b, 0, "hidden0");
	CHECK(rc == 0 && b.hidden && b.ssid_len == 5, "hidden zeros");

	/* WPA2/WPA3 transition and WPA3 only. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP2, 0x0411,
	    SSID_LAB RATES_G DS6 RSN_MIX HTCAP_AP HTOP6_20 WMM_PARAM, 0, &b, 1,
	    "mix");
	CHECK(rc == 0 && b.sec == WCN_SEC_WPA2_WPA3 && b.pmf_capable &&
	    !b.pmf_required && b.rsn.akm == (WCN_BIT(WCN_AKM_PSK) |
	    WCN_BIT(WCN_AKM_SAE)), "mix sec %d", b.sec);
	CHECK(wcn_bss_usable(&b), "mix usable");
	wcn_bss_chandef(&b, 1, &f, &c, &w);
	CHECK(w == 20 && c == 2437, "mix 20 MHz");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411,
	    SSID_LAB RATES_G DS6 RSN_SAE, 0, &b, 1, "wpa3");
	CHECK(rc == 0 && b.sec == WCN_SEC_WPA3 && b.pmf_required &&
	    !wcn_bss_usable(&b), "wpa3 sec %d", b.sec);
	/* WPA2-PSK but PMF required: not ours. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 c0 00",
	    0, &b, 0, "mfpr");
	CHECK(rc == 0 && b.sec == WCN_SEC_WPA2_PSK && b.pmf_required &&
	    !wcn_bss_usable(&b), "mfpr");
	/* WPA vendor element, WEP, enterprise, TKIP-only. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "dd 16 00 50 f2 01 01 00 00 50 f2 02 01 00 00 50 f2 02 01 00 00 50 f2 02",
	    0, &b, 0, "wpa1");
	CHECK(rc == 0 && b.wpa1 && b.sec == WCN_SEC_WPA1 && !wcn_bss_usable(&b),
	    "wpa1");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6,
	    0, &b, 0, "wep");
	CHECK(rc == 0 && b.sec == WCN_SEC_WEP && !wcn_bss_usable(&b), "wep");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 01 00 00",
	    0, &b, 0, "eap");
	CHECK(rc == 0 && b.sec == WCN_SEC_8021X && !wcn_bss_usable(&b), "eap");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 14 01 00 00 0f ac 02 01 00 00 0f ac 02 01 00 00 0f ac 02 00 00",
	    0, &b, 0, "tkip");
	CHECK(rc == 0 && b.sec == WCN_SEC_OTHER && !wcn_bss_usable(&b), "tkip");
	/* CCMP pairwise with a TKIP group: usable (mixed-mode AP). */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 18 01 00 00 0f ac 02 02 00 00 0f ac 04 00 0f ac 02 01 00 00 0f ac 02 00 00",
	    0, &b, 0, "tkipgroup");
	CHECK(rc == 0 && b.sec == WCN_SEC_WPA2_PSK &&
	    b.rsn.group == WCN_CIPHER_TKIP && wcn_bss_usable(&b), "tkipgroup");

	/* RSN shapes. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 06 01 00 00 0f ac 04", 0, &b, 0, "rsnshort");
	CHECK(rc == 0 && b.rsn.valid && b.rsn.pairwise ==
	    WCN_BIT(WCN_CIPHER_CCMP) && b.rsn.akm == WCN_BIT(WCN_AKM_8021X) &&
	    b.sec == WCN_SEC_8021X, "rsn defaults");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 0e 01 00 00 0f ac 04 05 00 00 0f ac 04 01 00", 0, &b, 1,
	    "rsnpw");
	CHECK(rc == 0 && b.rsn.present && !b.rsn.valid &&
	    b.sec == WCN_SEC_OTHER && !wcn_bss_usable(&b), "rsn pw count");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 12 01 00 00 0f ac 04 01 00 00 0f ac 04 03 00 00 0f ac 02", 0,
	    &b, 1, "rsnakm");
	CHECK(rc == 0 && !b.rsn.valid && b.sec == WCN_SEC_OTHER,
	    "rsn akm count");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 12 01 00 00 0f ac 04 01 00 00 0f ac 04 00 00 00 0f ac 02", 0,
	    &b, 0, "rsnakm0");
	CHECK(rc == 0 && !b.rsn.valid, "rsn akm count 0");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 02 02 00", 0, &b, 0, "rsnv2");
	CHECK(rc == 0 && !b.rsn.valid && b.sec == WCN_SEC_OTHER, "rsn v2");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 01 01", 0, &b, 0, "rsn1");
	CHECK(rc == 0 && !b.rsn.valid, "rsn 1 octet");
	/* With PMKIDs and a group management cipher, then more. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 2c 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 06 80 00"
	    " 01 00 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 00 0f ac 06"
	    " aa bb", 0, &b, 1, "rsnfull");
	CHECK(rc == 0 && b.rsn.valid && b.sec == WCN_SEC_WPA2_PSK &&
	    b.rsn.akm == WCN_BIT(WCN_AKM_PSK_SHA256), "rsn full");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 1a 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 00 00"
	    " 02 00 00 01 02 03", 0, &b, 0, "rsnpmkid");
	CHECK(rc == 0 && !b.rsn.valid, "rsn pmkid count");
	/* A suite of another OUI. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6
	    "30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 50 f2 02 00 00",
	    0, &b, 0, "rsnoui");
	CHECK(rc == 0 && b.rsn.akm == WCN_BIT(WCN_SUITE_OTHER) &&
	    b.sec == WCN_SEC_OTHER, "rsn other oui");

	/* Refused frames. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411,
	    SSID_LAB "01 08 82 84", 0, &b, 0, "past");
	CHECK(rc == EINVAL, "element past the end");
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB "01", 0, &b,
	    0, "half");
	CHECK(rc == EINVAL, "half an element header");
	{
		char ies[512];
		int k, i;

		k = sprintf(ies, "00 21");
		for (i = 0; i < 33; i++)
			k += sprintf(ies + k, " 61");
		sprintf(ies + k, " %s", RATES_G);
		rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, ies, 0, &b,
		    0, "ssid33");
		CHECK(rc == EINVAL, "33-octet SSID");
		k = sprintf(ies, "00 20");
		for (i = 0; i < 31; i++)
			k += sprintf(ies + k, " 61");
		sprintf(ies + k, " 62 %s%s", RATES_G, DS6);
		rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, ies, 0, &b,
		    1, "ssid32");
		CHECK(rc == 0 && b.ssid_len == 32 && b.ssid[31] == 'b',
		    "32-octet SSID");
	}
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, RATES_G DS6, 0, &b, 0,
	    "nossid");
	CHECK(rc == EINVAL, "no SSID");
	rc = parse_hex_beacon(0xb0, AP3, 0x0411, SSID_LAB RATES_G, 0, &b, 0,
	    "auth");
	CHECK(rc == EINVAL, "not a beacon");
	{
		uint8_t fr[256];
		size_t n = beacon(fr, WCN_FC_BEACON, AP, 1, SSID_LAB RATES_G);

		fr[1] = WCN_FC1_PROTECTED;
		CHECK(wcn_bss_parse(fr, n, 6, 0, 0, &b) == EINVAL,
		    "protected beacon");
	}
	/* Second SSID element and short HT elements are ignored. */
	rc = parse_hex_beacon(WCN_FC_BEACON, AP3, 0x0411, SSID_LAB
	    "00 02 78 79 " RATES_G DS6 "2d 02 00 00 3d 01 06", 0, &b, 1, "dup");
	CHECK(rc == 0 && b.ssid_len == 10 && !b.ht && !b.ht_op, "dup/short");
}

/* Element soup beyond the copy limit, and random soups. */
static uint32_t rng = 0x12345678;

static uint32_t
rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static void
soups(void)
{
	uint8_t f[2048], *c;
	struct wcn_bss b;
	struct wcn_assoc_resp ar;
	struct wcn_rx rx;
	struct wcn_sta s;
	size_t n, k, l;
	int i, ok = 0, rc;

	/* 4 vendor elements of 255: 1020 octets past the copy limit. */
	n = beacon(f, WCN_FC_BEACON, AP, 0x0431, SSID_LAB RATES_G DS6);
	for (i = 0; i < 4; i++) {
		f[n++] = 221;
		f[n++] = 255;
		memset(f + n, i, 255);
		n += 255;
	}
	c = exact(f, n);
	CHECK(wcn_bss_parse(c, n, 0, 0, 0, &b) == 0 && b.ies_trunc &&
	    b.ies_len == WCN_BSS_IES && b.ssid_len == 10, "big beacon");
	free(c);

	memcpy(s.self, SELF, 6);
	memcpy(s.bssid, AP, 6);
	for (i = 0; i < 200000; i++) {
		n = beacon(f, (rnd() & 1) ? WCN_FC_BEACON : WCN_FC_PROBE_RESP, AP,
		    (uint16_t)rnd(), "");
		k = rnd() % 400;
		while (k-- > 0 && n < sizeof(f)) {
			/* Mostly known elements, sometimes junk. */
			static const uint8_t ids[] = { 0, 1, 3, 45, 48, 50, 61,
			    191, 221, 7, 127 };
			if (rnd() % 8 == 0)
				f[n++] = (uint8_t)rnd();
			else
				f[n++] = ids[rnd() % sizeof(ids)];
			if (n >= sizeof(f))
				break;
			l = rnd() % 8 == 0 ? rnd() % 256 : rnd() % 40;
			f[n++] = (uint8_t)l;
			if (rnd() % 3 == 0 && n + 4 <= sizeof(f)) {
				f[n++] = 0x00;
				f[n++] = rnd() & 1 ? 0x50 : 0x0f;
				f[n++] = rnd() & 1 ? 0xf2 : 0xac;
				f[n++] = (uint8_t)(rnd() % 4);
			}
		}
		if (n > sizeof(f))
			n = sizeof(f);
		n = 36 + rnd() % (n - 35);
		c = exact(f, n);
		rc = wcn_bss_parse(c, n, rnd() % 200, -60, 0, &b);
		if (rc == 0) {
			ok++;
			if (b.ssid_len > 32 || b.nrates > WCN_RATES_MAX ||
			    b.ies_len > n - 36 || b.ies_len > WCN_BSS_IES ||
			    (b.chan != 0 && wcn_chan_freq(b.chan) != b.freq) ||
			    (b.sec == WCN_SEC_WPA2_PSK && !b.rsn.valid)) {
				CHECK(0, "soup %d: inconsistent", i);
				free(c);
				break;
			}
			(void)wcn_bss_usable(&b);
		}
		/* The same bytes as an assoc response and as data. */
		c[0] = WCN_FC_ASSOC_RESP;
		memcpy(c + 4, SELF, 6);
		(void)wcn_parse_assoc(&s, c, n, &ar);
		c[0] = (uint8_t)(0x08 | (rnd() & 0xf0));
		c[1] = (uint8_t)rnd();
		(void)wcn_nwifi_rx(c, n, SELF, &rx);
		free(c);
	}
	CHECK(ok > 500, "soups parsed: %d", ok);
	printf("soups: %d of 200000 random beacons accepted\n", ok);
}

/* ---- The table ---- */

static void
table(void)
{
	static struct wcn_bss_table t;
	struct wcn_bss b, h;
	const struct wcn_bss *p;
	uint8_t f[512];
	size_t n;
	unsigned i;
	const uint8_t lab[] = "MEECHO-lab";

	wcn_bss_init(&t);
	n = beacon(f, WCN_FC_BEACON, AP, 0x0431,
	    SSID_LAB RATES_G DS6 RSN_PSK HTCAP_AP HTOP6 WMM_PARAM);
	CHECK(wcn_bss_parse(f, n, 6, -70, 100, &b) == 0, "t1");
	wcn_bss_update(&t, &b);
	n = beacon(f, WCN_FC_BEACON, AP2, 0x0411,
	    SSID_LAB RATES_G DS6 RSN_MIX);
	CHECK(wcn_bss_parse(f, n, 6, -50, 110, &b) == 0, "t2");
	wcn_bss_update(&t, &b);
	n = beacon(f, WCN_FC_BEACON, AP3, 0x0411, SSID_LAB RATES_G DS6 RSN_SAE);
	CHECK(wcn_bss_parse(f, n, 6, -30, 120, &b) == 0, "t3");
	wcn_bss_update(&t, &b);

	p = wcn_bss_best(&t, lab, 10, ~0u);
	CHECK(p != NULL && memcmp(p->bssid, AP2, 6) == 0,
	    "best: the transition AP (WPA3-only is not usable)");
	p = wcn_bss_best(&t, lab, 10, WCN_BIT(WCN_SEC_WPA2_PSK));
	CHECK(p != NULL && memcmp(p->bssid, AP, 6) == 0, "best: PSK only");
	p = wcn_bss_best(&t, lab, 10, WCN_BIT(WCN_SEC_OPEN));
	CHECK(p == NULL, "best: open none");
	p = wcn_bss_best(&t, lab, 9, ~0u);
	CHECK(p == NULL, "best: SSID prefix is not the SSID");

	/* An update moves the signal. */
	n = beacon(f, WCN_FC_BEACON, AP, 0x0431,
	    SSID_LAB RATES_G DS6 RSN_PSK);
	CHECK(wcn_bss_parse(f, n, 6, -20, 130, &b) == 0, "t1'");
	wcn_bss_update(&t, &b);
	p = wcn_bss_best(&t, lab, 10, ~0u);
	CHECK(p != NULL && memcmp(p->bssid, AP, 6) == 0 && p->rssi == -20,
	    "best after update");
	for (i = 0, n = 0; i < WCN_BSS_MAX; i++)
		n += t.used[i];
	CHECK(n == 3, "no duplicate on update: %zu", n);

	/* Hidden: the probe response names it, the next beacon does not
	 * take the name away. */
	{
		static const uint8_t HID[6] = { 2, 0, 0, 0, 0, 9 };
		const uint8_t hn[] = "secret";

		n = beacon(f, WCN_FC_BEACON, HID, 0x0001, "00 00 " RATES_G DS6);
		CHECK(wcn_bss_parse(f, n, 6, -10, 140, &h) == 0, "h1");
		wcn_bss_update(&t, &h);
		CHECK(wcn_bss_best(&t, hn, 6, ~0u) == NULL, "hidden unnamed");
		n = beacon(f, WCN_FC_PROBE_RESP, HID, 0x0001,
		    "00 06 73 65 63 72 65 74 " RATES_G DS6);
		CHECK(wcn_bss_parse(f, n, 6, -10, 150, &h) == 0, "h2");
		wcn_bss_update(&t, &h);
		n = beacon(f, WCN_FC_BEACON, HID, 0x0001, "00 00 " RATES_G DS6);
		CHECK(wcn_bss_parse(f, n, 6, -12, 160, &h) == 0, "h3");
		wcn_bss_update(&t, &h);
		p = wcn_bss_find(&t, HID);
		CHECK(p != NULL && p->ssid_len == 6 && !p->hidden &&
		    p->rssi == -12 && p->seen == 160, "hidden keeps its name");
		CHECK(wcn_bss_best(&t, hn, 6, ~0u) == p, "hidden found");
	}

	/* Ageing, across the clock's wrap. */
	wcn_bss_expire(&t, 165, 40);	/* 110 and 120 go, 130/160 stay */
	CHECK(wcn_bss_find(&t, AP2) == NULL && wcn_bss_find(&t, AP3) == NULL &&
	    wcn_bss_find(&t, AP) != NULL, "expire");
	wcn_bss_init(&t);
	b.seen = 0xfffffff0u;
	wcn_bss_update(&t, &b);
	wcn_bss_expire(&t, 0x10, 0x40);
	CHECK(wcn_bss_find(&t, b.bssid) != NULL, "expire across wrap: kept");
	wcn_bss_expire(&t, 0x40, 0x40);
	CHECK(wcn_bss_find(&t, b.bssid) == NULL, "expire across wrap: gone");

	/* Full: the oldest goes. */
	wcn_bss_init(&t);
	for (i = 0; i < WCN_BSS_MAX + 1; i++) {
		b.bssid[5] = (uint8_t)i;
		b.seen = 1000 + (i == 5 ? 0 : 10 + i);
		wcn_bss_update(&t, &b);
	}
	b.bssid[5] = 5;
	CHECK(wcn_bss_find(&t, b.bssid) == NULL, "full: the oldest went");
	b.bssid[5] = WCN_BSS_MAX;
	CHECK(wcn_bss_find(&t, b.bssid) != NULL, "full: the newest is in");
	b.bssid[5] = 0;
	CHECK(wcn_bss_find(&t, b.bssid) != NULL, "full: the others stay");
}

/* ---- Rates and HT ---- */

static void
rates(void)
{
	struct wcn_bss b;
	uint8_t r[WCN_RATES_MAX], p[WCN_RATES_MAX];
	int n;

	memset(&b, 0, sizeof(b));
	b.band = WCN_BAND_2G;
	b.nrates = (uint8_t)hex("82 84 8b 96", b.rates, WCN_RATES_MAX);
	n = wcn_rates_join(&b, 0, r, sizeof(r));
	CHECK(n == 4 && same(r, 4, "02 04 0b 16", "11b"), "11b %d", n);
	wcn_rates_peer(r, 4, p);
	CHECK(same(p, 4, "82 84 8b 96", "peer cck"), "peer");

	b.nrates = (uint8_t)hex("0c 12 98 24 b0 48 60 6c 82 84 8b 16", b.rates,
	    WCN_RATES_MAX);
	n = wcn_rates_join(&b, 0, r, sizeof(r));
	CHECK(n == 12 && same(r, 12, "02 04 0b 16 0c 12 18 24 30 48 60 6c",
	    "our order"), "g %d", n);
	wcn_rates_peer(r, 12, p);
	CHECK(same(p, 12, "82 84 8b 96 0c 12 18 24 30 48 60 6c", "peer g"),
	    "peer g");
	CHECK(wcn_rates_join(&b, 0, r, 4) == -1, "too small");

	/* HT selector: needs HT; VHT selector: not ours. */
	b.nrates = (uint8_t)hex("82 84 8b 96 ff", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 0, r, sizeof(r)) == -1, "HT required, no HT");
	CHECK(wcn_rates_join(&b, 1, r, sizeof(r)) == 4, "HT required, HT");
	b.nrates = (uint8_t)hex("82 84 8b 96 fe", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 1, r, sizeof(r)) == -1, "VHT required");
	b.nrates = (uint8_t)hex("82 84 8b 96 fb", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 1, r, sizeof(r)) == -1, "SAE H2E required");
	/* A basic rate we lack; a non-basic one we lack is fine. */
	b.nrates = (uint8_t)hex("82 84 a2", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 1, r, sizeof(r)) == -1, "basic 17 Mbit/s");
	b.nrates = (uint8_t)hex("82 84 22", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 1, r, sizeof(r)) == 2, "non-basic 17 Mbit/s");
	/* 5 GHz: CCK is not ours there. */
	b.band = WCN_BAND_5G;
	b.nrates = (uint8_t)hex("8c 12 98 24 b0 48 60 6c", b.rates,
	    WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 0, r, sizeof(r)) == 8 && r[0] == 12, "11a");
	b.nrates = (uint8_t)hex("82 8c", b.rates, WCN_RATES_MAX);
	CHECK(wcn_rates_join(&b, 0, r, sizeof(r)) == -1, "5 GHz basic CCK");
	b.nrates = 0;
	CHECK(wcn_rates_join(&b, 0, r, sizeof(r)) == -1, "nothing common");

	/* ath10k_get_ht_cap(), one chain. */
	CHECK(wcn_ht_capinfo(0, 0) == 0, "ht off");
	CHECK(wcn_ht_capinfo(0x0001, 0) == 0x100e, "ht bare: %04x",
	    wcn_ht_capinfo(0x0001, 0));
	CHECK(wcn_ht_capinfo(0x0001 | 0x0002 | 0x0800 | 0x0010 | 0x0004, 0) ==
	    0x116e, "ht sgi stbc: %04x",
	    wcn_ht_capinfo(0x0001 | 0x0002 | 0x0800 | 0x0010 | 0x0004, 0));
	CHECK(wcn_ht_capinfo(0x0001 | 0x0008 | 0x0030, 0) == 0x130e,
	    "ht tx stbc dropped, rx 3: %04x",
	    wcn_ht_capinfo(0x0001 | 0x0008 | 0x0030, 0));
	CHECK(wcn_ht_capinfo(0x0001 | 0x0040, 0) == 0x100f, "ldpc");
	CHECK(wcn_ht_capinfo(0x0001 | 0x1000, 0) == 0x100e, "rx ldpc only");
	CHECK(wcn_ht_capinfo(0x0001 | 0x3000, 0) == 0x100f, "rx+tx ldpc");
	CHECK(wcn_ht_capinfo(0x0001 | 0x0080, 2) == 0x980e, "lsig amsdu");
}

/* ---- Built frames ---- */

#define H_AP "a0 b1 c2 d3 e4 f5"
#define H_SELF "8c be be 11 22 33"

static void
built(void)
{
	struct wcn_sta s;
	struct wcn_bss b;
	struct wcn_assoc_opts o;
	uint8_t f[512], out[512];
	size_t n;
	static const uint8_t rsn_supp[] = {
		0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
		0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
		0x00, 0x00
	};

	memcpy(s.self, SELF, 6);
	memcpy(s.bssid, AP, 6);
	s.seq = 1;

	n = wcn_build_auth(&s, out, sizeof(out));
	same(out, n, "b0 00 00 00 " H_AP " " H_SELF " " H_AP " 10 00"
	    " 00 00 01 00 00 00", "auth");
	CHECK(s.seq == 2, "seq after auth");
	CHECK(wcn_build_auth(&s, out, 29) == 0 && s.seq == 2,
	    "auth: small buffer, seq kept");

	n = beacon(f, WCN_FC_BEACON, AP, 0x0431,
	    SSID_LAB RATES_G DS6 RSN_PSK HTCAP_AP HTOP6 WMM_PARAM);
	CHECK(wcn_bss_parse(f, n, 6, -50, 0, &b) == 0, "assoc bss");
	memset(&o, 0, sizeof(o));
	o.rsn_ie = rsn_supp;
	o.rsn_len = sizeof(rsn_supp);
	o.ht_capinfo = wcn_ht_capinfo(0x0001 | 0x0002 | 0x0800 | 0x0010, 0);
	o.ht40 = 1;
	o.nss = 1;
	n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
	same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 20 00"
	    " 31 04 05 00"
	    " 00 0a 4d 45 45 43 48 4f 2d 6c 61 62"
	    " 01 08 02 04 0b 16 0c 12 18 24 32 04 30 48 60 6c"
	    " 30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 00 00"
	    " 2d 1a 6e 11 1b ff 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00"
	    " 00 00 00 00 00 00 00"
	    " dd 07 00 50 f2 02 00 01 00", "assoc wpa2 ht40 wmm");
	CHECK(s.seq == 3, "seq after assoc");

	/* 20 MHz only, two streams, own listen interval. */
	o.ht40 = 0;
	o.nss = 2;
	o.listen = 10;
	n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
	same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 30 00"
	    " 31 04 0a 00"
	    " 00 0a 4d 45 45 43 48 4f 2d 6c 61 62"
	    " 01 08 02 04 0b 16 0c 12 18 24 32 04 30 48 60 6c"
	    " 30 14 01 00 00 0f ac 04 01 00 00 0f ac 04 01 00 00 0f ac 02 00 00"
	    " 2d 1a 2c 11 1b ff ff 00 00 00 00 00 00 00 00 00 00 01 00 00 00"
	    " 00 00 00 00 00 00 00"
	    " dd 07 00 50 f2 02 00 01 00", "assoc ht20 nss2");

	/* Every too-small buffer: 0, sequence untouched, no overrun. */
	{
		size_t full = n, k;
		uint8_t *c;
		uint16_t seq = s.seq;

		for (k = 0; k < full; k++) {
			c = malloc(k ? k : 1);
			CHECK(wcn_build_assoc(&s, &b, &o, c, k) == 0 &&
			    s.seq == seq, "assoc into %zu", k);
			free(c);
		}
	}
	s.seq = 4;

	/* Open 5 GHz AP: no privacy, no short slot/preamble, no RSN, no
	 * WMM; HT stays. */
	n = beacon(f, WCN_FC_PROBE_RESP, AP, 0x0001,
	    SSID_5 RATES_A HTCAP_AP HTOP36 VHTCAP);
	CHECK(wcn_bss_parse(f, n, 36, -50, 0, &b) == 0, "open bss");
	memset(&o, 0, sizeof(o));
	o.ht_capinfo = 0x100e;
	o.ht40 = 1;
	o.nss = 1;
	n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
	same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 40 00"
	    " 01 00 05 00"
	    " 00 08 4d 45 45 43 48 4f 2d 35"
	    " 01 08 0c 12 18 24 30 48 60 6c"
	    " 2d 1a 0e 10 1b ff 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00"
	    " 00 00 00 00 00 00 00", "assoc open 5 GHz");

	/* Legacy AP: no HT element even if we can. */
	n = beacon(f, WCN_FC_BEACON, AP, 0x0021,
	    "00 03 6f 6c 64 01 04 82 84 8b 96 03 01 0b");
	CHECK(wcn_bss_parse(f, n, 11, -50, 0, &b) == 0, "legacy bss");
	n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
	same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 50 00"
	    " 21 04 05 00 00 03 6f 6c 64 01 04 02 04 0b 16", "assoc legacy");

	/* A WPA vendor element goes after HT; a malformed one is refused;
	 * a hidden BSS cannot be joined by its beacon. */
	{
		static const uint8_t bad[] = { 0x30, 0x05, 1, 0 };
		uint16_t seq;

		o.rsn_ie = bad;
		o.rsn_len = sizeof(bad);
		seq = s.seq;
		CHECK(wcn_build_assoc(&s, &b, &o, out, sizeof(out)) == 0 &&
		    s.seq == seq, "bad RSN length");
		o.rsn_ie = NULL;
		n = beacon(f, WCN_FC_BEACON, AP, 0x0021, "00 00 01 01 82");
		CHECK(wcn_bss_parse(f, n, 11, -50, 0, &b) == 0, "hidden bss");
		CHECK(wcn_build_assoc(&s, &b, &o, out, sizeof(out)) == 0,
		    "hidden: no SSID to ask for");
		/* AP requiring VHT: cannot. */
		n = beacon(f, WCN_FC_BEACON, AP, 0x0001,
		    "00 01 78 01 03 8c 98 fe " HTCAP_AP);
		CHECK(wcn_bss_parse(f, n, 36, -50, 0, &b) == 0, "vht bss");
		CHECK(wcn_build_assoc(&s, &b, &o, out, sizeof(out)) == 0,
		    "VHT selector: refused");
	}
	{
		static const uint8_t wpa[] = { 0xdd, 0x06, 0x00, 0x50, 0xf2,
			0x01, 0x01, 0x00 };

		n = beacon(f, WCN_FC_BEACON, AP, 0x0431, SSID_LAB RATES_G DS6
		    HTCAP_AP "dd 06 00 50 f2 01 01 00 " WMM_PARAM);
		CHECK(wcn_bss_parse(f, n, 6, -50, 0, &b) == 0, "wpa1 bss");
		o.rsn_ie = wpa;
		o.rsn_len = sizeof(wpa);
		s.seq = 0;
		n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
		/* WPA1 (TKIP) means no HT. */
		same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 00 00"
		    " 31 04 05 00 00 0a 4d 45 45 43 48 4f 2d 6c 61 62"
		    " 01 08 02 04 0b 16 0c 12 18 24 32 04 30 48 60 6c"
		    " dd 06 00 50 f2 01 01 00 dd 07 00 50 f2 02 00 01 00",
		    "assoc wpa1");
		/* RSN with TKIP pairwise only: no HT either. */
		n = beacon(f, WCN_FC_BEACON, AP, 0x0431, SSID_LAB RATES_G DS6
		    "30 14 01 00 00 0f ac 02 01 00 00 0f ac 02 01 00 00 0f ac"
		    " 02 00 00 " HTCAP_AP);
		CHECK(wcn_bss_parse(f, n, 6, -50, 0, &b) == 0 && b.ht,
		    "tkip bss");
		o.rsn_ie = NULL;
		n = wcn_build_assoc(&s, &b, &o, out, sizeof(out));
		same(out, n, "00 00 00 00 " H_AP " " H_SELF " " H_AP " 10 00"
		    " 31 04 05 00 00 0a 4d 45 45 43 48 4f 2d 6c 61 62"
		    " 01 08 02 04 0b 16 0c 12 18 24 32 04 30 48 60 6c",
		    "assoc tkip: no HT");
	}

	s.seq = 0xfff;
	n = wcn_build_deauth(&s, WCN_REASON_LEAVING, out, sizeof(out));
	same(out, n, "c0 00 00 00 " H_AP " " H_SELF " " H_AP " f0 ff 03 00",
	    "deauth");
	CHECK(s.seq == 0, "seq wraps");
	n = wcn_build_disassoc(&s, WCN_REASON_DISASSOC_LEAVING, out,
	    sizeof(out));
	same(out, n, "a0 00 00 00 " H_AP " " H_SELF " " H_AP " 00 00 08 00",
	    "disassoc");
	CHECK(wcn_build_deauth(&s, 3, out, 25) == 0 && s.seq == 1,
	    "deauth small");
}

/* ---- Parsed answers ---- */

/* A frame from the AP to us (or to dst). */
static size_t
from_ap(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *dst,
	const char *body)
{
	size_t n = 0;

	f[n++] = fc0;
	f[n++] = fc1;
	f[n++] = 0x3a;
	f[n++] = 0x01;
	memcpy(f + n, dst, 6);
	n += 6;
	memcpy(f + n, AP, 6);
	n += 6;
	memcpy(f + n, AP, 6);
	n += 6;
	f[n++] = 0x20;
	f[n++] = 0x00;
	return n + hex(body, f + n, 1024);
}

static void
answers(void)
{
	struct wcn_sta s;
	struct wcn_auth_resp a;
	struct wcn_assoc_resp r;
	uint8_t f[1024], *c;
	size_t n, k, off, ends[64], ne;
	uint16_t reason;
	int dis, rc, want, i;

	memcpy(s.self, SELF, 6);
	memcpy(s.bssid, AP, 6);
	s.seq = 0;

	n = from_ap(f, WCN_FC_AUTH, 0, SELF, "00 00 02 00 00 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == 0 && a.alg == 0 && a.seq == 2 &&
	    a.status == 0, "auth ok");
	n = from_ap(f, WCN_FC_AUTH, 0, SELF, "00 00 02 00 0d 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == 0 && a.status == 13,
	    "auth refused");
	CHECK(wcn_parse_auth(&s, f, n - 1, &a) == EINVAL, "auth short");
	n = from_ap(f, WCN_FC_AUTH, 0, HOST, "00 00 02 00 00 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == EINVAL, "auth to another");
	CHECK(wcn_parse_auth(NULL, f, n, &a) == 0, "auth unchecked");
	n = from_ap(f, WCN_FC_AUTH, 0, SELF, "00 00 02 00 00 00");
	f[16 + 5] ^= 1;
	CHECK(wcn_parse_auth(&s, f, n, &a) == EINVAL, "auth other BSS");
	n = from_ap(f, WCN_FC_AUTH, WCN_FC1_PROTECTED, SELF,
	    "00 00 02 00 00 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == EINVAL, "auth protected");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF, "00 00 02 00 00 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == EINVAL, "auth wrong subtype");
	n = from_ap(f, 0xb4, 0, SELF, "00 00 02 00 00 00");
	CHECK(wcn_parse_auth(&s, f, n, &a) == EINVAL, "control frame");

	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF,
	    "11 04 00 00 01 c0 " RATES_G HTCAP_AP HTOP6 EXTCAP WMM_PARAM);
	rc = wcn_parse_assoc(&s, f, n, &r);	/* r.ies points into f */
	CHECK(rc == 0 && r.status == 0 && r.aid == 1 && r.capab == 0x0411 &&
	    !r.reassoc, "assoc ok: aid %u", r.aid);
	CHECK(r.nrates == 12 && r.ht && r.ht_capinfo == 0x19ef &&
	    r.ht_op && r.ht_primary == 6 && r.ht_secoff == 1 && r.ht_width40,
	    "assoc ht");
	CHECK(r.wmm && r.wmm_qosinfo == 0x80 && r.ac[0].aci_aifsn == 0x03 &&
	    r.ac[0].ecw == 0xa4 && r.ac[1].aci_aifsn == 0x27 &&
	    r.ac[2].aci_aifsn == 0x42 && r.ac[2].txop == 94 &&
	    r.ac[3].aci_aifsn == 0x62 && r.ac[3].ecw == 0x32 &&
	    r.ac[3].txop == 47, "assoc wmm");
	CHECK(r.ies_len == n - 30 && r.ies[0] == 1, "assoc ies");

	/* Truncation: accepted at element boundaries only. */
	ne = 0;
	for (off = 30; off + 2 <= n; off += 2 + f[off + 1])
		ends[ne++] = off + 2 + f[off + 1];
	for (k = 0; k < n; k++) {
		c = exact(f, k);
		rc = wcn_parse_assoc(&s, c, k, &r);
		want = k == 30;
		for (i = 0; i < (int)ne; i++)
			if (ends[i] == k)
				want = 1;
		CHECK((rc == 0) == want, "assoc cut at %zu: %d", k, rc);
		free(c);
	}

	n = from_ap(f, WCN_FC_REASSOC_RESP, 0, SELF, "11 04 00 00 d7 c7");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == 0 && r.aid == 2007 &&
	    r.reassoc, "reassoc aid 2007");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF, "11 04 00 00 d8 c7");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == EINVAL, "aid 2008");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF, "11 04 00 00 00 c0");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == EINVAL, "aid 0 on success");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF, "11 04 11 00 00 00");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == 0 && r.status == 17 &&
	    r.aid == 0, "assoc refused, no aid");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF, "11 04 00 00 05 00 01 08 82");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == EINVAL, "assoc rates past end");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF,
	    "11 04 00 00 05 00 dd 07 00 50 f2 02 00 01 00");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == 0 && r.aid == 5 && !r.wmm,
	    "WMM info is not parameters");
	n = from_ap(f, WCN_FC_ASSOC_RESP, 0, SELF,
	    "11 04 00 00 05 00 dd 10 00 50 f2 02 01 01 00 00 00 00 00 00 00 00 00 00");
	CHECK(wcn_parse_assoc(&s, f, n, &r) == 0 && !r.wmm,
	    "short WMM parameters ignored");

	n = from_ap(f, WCN_FC_DEAUTH, 0, BC, "07 00");
	CHECK(wcn_parse_leave(&s, f, n, &reason, &dis) == 0 && reason == 7 &&
	    !dis, "deauth broadcast");
	n = from_ap(f, WCN_FC_DISASSOC, 0, SELF, "08 00");
	CHECK(wcn_parse_leave(&s, f, n, &reason, &dis) == 0 && reason == 8 &&
	    dis, "disassoc");
	CHECK(wcn_parse_leave(&s, f, n - 1, &reason, &dis) == EINVAL,
	    "disassoc short");
	n = from_ap(f, WCN_FC_DEAUTH, WCN_FC1_PROTECTED, SELF,
	    "07 00 00 00 00 00 00 00 00 00");
	CHECK(wcn_parse_leave(&s, f, n, &reason, &dis) == EINVAL,
	    "protected deauth (PMF) refused");
	n = from_ap(f, WCN_FC_DEAUTH, 0, HOST, "07 00");
	CHECK(wcn_parse_leave(&s, f, n, &reason, &dis) == EINVAL,
	    "deauth to another");
	n = from_ap(f, WCN_FC_DEAUTH, 0, SELF, "07 00");
	f[10] ^= 2;		/* spoofed source */
	CHECK(wcn_parse_leave(&s, f, n, &reason, &dis) == EINVAL,
	    "deauth from not the AP");
	for (k = 0; k < WCN_HDR_LEN; k++) {
		c = exact(f, k);
		CHECK(wcn_parse_leave(&s, c, k, &reason, &dis) == EINVAL &&
		    wcn_parse_auth(&s, c, k, &a) == EINVAL, "short %zu", k);
		free(c);
	}
}

/* ---- Native wifi ---- */

/* Turn our to-DS frame into the from-DS one the AP would hand us from
 * that destination: a1 = self, a2 = bssid, a3 = the far end. */
static void
reflect(uint8_t *f)
{
	uint8_t far[6];

	memcpy(far, f + 16, 6);
	f[1] = (uint8_t)((f[1] & ~WCN_FC1_TODS & ~WCN_FC1_PROTECTED) |
	    WCN_FC1_FROMDS);
	memcpy(f + 4, SELF, 6);
	memcpy(f + 10, AP, 6);
	memcpy(f + 16, far, 6);
}

static void
roundtrip(const struct wcn_sta *s, const char *ethhex, int protect,
	const char *nwhex, const char *what)
{
	uint8_t eth[1600], nw[1600], back[1600], want[1600];
	size_t n, m, k;
	struct wcn_rx r;
	unsigned t;
	int rc;

	n = hex(ethhex, eth, sizeof(eth));
	m = wcn_nwifi_tx(s, eth, n, protect, nw, sizeof(nw));
	if (nwhex != NULL)
		same(nw, m, nwhex, what);
	CHECK(m > 0, "%s: tx", what);
	if (m == 0)
		return;
	CHECK(wcn_nwifi_tx(s, eth, n, protect, nw, m - 1) == 0,
	    "%s: tx into one less", what);
	m = wcn_nwifi_tx(s, eth, n, protect, nw, sizeof(nw));
	reflect(nw);
	rc = wcn_nwifi_rx(nw, m, SELF, &r);
	CHECK(rc == WCN_RX_OK, "%s: rx %d", what, rc);
	if (rc != WCN_RX_OK)
		return;
	k = wcn_rx_eth(&r, back, sizeof(back));
	/* What comes back: dst self, src the far end, same type/payload
	 * (an 802.3 frame loses its padding). */
	memcpy(want, SELF, 6);
	memcpy(want + 6, eth, 6);
	memcpy(want + 12, eth + 12, n - 12);
	t = (unsigned)(eth[12] << 8 | eth[13]);
	if (t < 0x600 && t < n - 14)
		n = 14 + t;
	CHECK(k == n && memcmp(back, want, n) == 0, "%s: round trip (%zu/%zu)",
	    what, k, n);
	CHECK(wcn_rx_eth(&r, back, k - 1) == 0, "%s: eth into one less",
	    what);
}

#define ARP "00 01 08 00 06 04 00 01 8c be be 11 22 33 c0 a8 21 64" \
	" 00 00 00 00 00 00 c0 a8 21 01"

static void
nwifi(void)
{
	struct wcn_sta s;
	struct wcn_rx r;
	uint8_t f[256], e[256], *c;
	size_t n, k;
	int rc;

	memcpy(s.self, SELF, 6);
	memcpy(s.bssid, AP, 6);
	s.seq = 0;

	roundtrip(&s, "ff ff ff ff ff ff " H_SELF " 08 06 " ARP, 0,
	    "08 01 00 00 " H_AP " " H_SELF " ff ff ff ff ff ff 00 00"
	    " aa aa 03 00 00 00 08 06 " ARP, "arp");
	roundtrip(&s, "00 11 32 0a 0b 0c " H_SELF " 08 00 45 00 00 1c 00 01"
	    " 00 00 40 01 00 00 c0 a8 21 64 c0 a8 21 01 08 00 f7 ff 00 00 00 00",
	    1, "08 41 00 00 " H_AP " " H_SELF " 00 11 32 0a 0b 0c 00 00"
	    " aa aa 03 00 00 00 08 00 45 00 00 1c 00 01 00 00 40 01 00 00"
	    " c0 a8 21 64 c0 a8 21 01 08 00 f7 ff 00 00 00 00", "ipv4 protected");
	roundtrip(&s, H_AP " " H_SELF " 88 8e 02 03 00 5f 02 01 0a 00 00 00 00"
	    " 00 00 00 01 11 22 33 44", 0,
	    "08 01 00 00 " H_AP " " H_SELF " " H_AP " 00 00"
	    " aa aa 03 00 00 00 88 8e 02 03 00 5f 02 01 0a 00 00 00 00 00 00"
	    " 00 01 11 22 33 44", "eapol");
	roundtrip(&s, "09 00 07 ff ff ff " H_SELF " 80 f3 00 01 80 9b 06 04",
	    0, "08 01 00 00 " H_AP " " H_SELF " 09 00 07 ff ff ff 00 00"
	    " aa aa 03 00 00 f8 80 f3 00 01 80 9b 06 04", "aarp tunnel");
	roundtrip(&s, "ff ff ff ff ff ff " H_SELF " 81 37 ff ff 00 1e", 0,
	    "08 01 00 00 " H_AP " " H_SELF " ff ff ff ff ff ff 00 00"
	    " aa aa 03 00 00 f8 81 37 ff ff 00 1e", "ipx tunnel");
	/* 802.3 with LLC (STP), padded to the Ethernet minimum. */
	roundtrip(&s, "01 80 c2 00 00 00 " H_SELF " 00 07 42 42 03 00 00 00 00"
	    " ee ee ee ee ee ee ee", 0,
	    "08 01 00 00 " H_AP " " H_SELF " 01 80 c2 00 00 00 00 00"
	    " 42 42 03 00 00 00 00", "802.3 llc");

	/* Refused on the way out. */
	n = hex(H_AP " 00 11 32 0a 0b 0c 08 00 45", e, sizeof(e));
	CHECK(wcn_nwifi_tx(&s, e, n, 0, f, sizeof(f)) == 0, "tx: not our SA");
	n = hex(H_AP " " H_SELF " 05 dd 00", e, sizeof(e));
	CHECK(wcn_nwifi_tx(&s, e, n, 0, f, sizeof(f)) == 0, "tx: 1501");
	n = hex(H_AP " " H_SELF " 00 09 42 42 03", e, sizeof(e));
	CHECK(wcn_nwifi_tx(&s, e, n, 0, f, sizeof(f)) == 0, "tx: short 802.3");
	CHECK(wcn_nwifi_tx(&s, e, 13, 0, f, sizeof(f)) == 0, "tx: no header");
	n = hex(H_AP " " H_SELF " 08 00", e, sizeof(e));
	CHECK(wcn_nwifi_tx(&s, e, n, 0, f, sizeof(f)) == 32,
	    "tx: empty payload");

	/* Received shapes. */
#define RXF(fc0, fc1, a1, a2, a3) fc0 " " fc1 " 2c 00 " a1 " " a2 " " a3 " 00 00 "
#define SNAP_IP "aa aa 03 00 00 00 08 00 45 00"
	n = hex(RXF("08", "02", H_SELF, H_AP, "00 11 32 0a 0b 0c") SNAP_IP, f,
	    sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && memcmp(r.da, SELF, 6) == 0 &&
	    memcmp(r.sa, HOST, 6) == 0 && memcmp(r.bssid, AP, 6) == 0 &&
	    r.type == 0x0800 && !r.llc && r.plen == 2, "rx from-DS");
	/* Every prefix. */
	for (k = 0; k < n; k++) {
		c = exact(f, k);
		rc = wcn_nwifi_rx(c, k, SELF, &r);
		if (k <= 24)
			CHECK(rc == WCN_RX_BAD, "rx cut %zu: %d", k, rc);
		else if (k < 32)
			CHECK(rc == WCN_RX_OK && r.llc && r.plen == k - 24,
			    "rx cut %zu: llc", k);
		else
			CHECK(rc == WCN_RX_OK && !r.llc && r.plen == k - 32,
			    "rx cut %zu: snap", k);
		free(c);
	}
	/* QoS data (native wifi strips it, but): */
	n = hex("88 02 2c 00 " H_SELF " " H_AP " 00 11 32 0a 0b 0c 00 00 05 00 "
	    SNAP_IP, f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.type == 0x0800 && r.plen == 2 &&
	    r.payload[0] == 0x45, "rx qos");
	n = hex("88 82 2c 00 " H_SELF " " H_AP " 00 11 32 0a 0b 0c 00 00 05 00"
	    " 00 00 00 00 " SNAP_IP, f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.type == 0x0800 && r.plen == 2,
	    "rx qos + htc");
	n = hex("88 02 2c 00 " H_SELF " " H_AP " 00 11 32 0a 0b 0c 00 00 85 00 "
	    SNAP_IP, f, sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_BAD, "rx a-msdu");
	n = hex("48 01 2c 00 " H_AP " " H_SELF " " H_AP " 00 00", f, sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_BAD, "rx to-DS");
	n = hex("48 02 2c 00 " H_SELF " " H_AP " " H_AP " 00 00", f, sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_DROP, "rx null");
	n = hex("c8 02 2c 00 " H_SELF " " H_AP " " H_AP " 00 00 00 00", f,
	    sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_DROP, "rx qos null");
	n = hex(RXF("08", "42", H_SELF, H_AP, "00 11 32 0a 0b 0c") SNAP_IP, f,
	    sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_BAD, "rx protected");
	n = hex(RXF("80", "00", H_SELF, H_AP, H_AP) SNAP_IP, f, sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_BAD, "rx management");
	n = hex(RXF("08", "02", "ff ff ff ff ff ff", H_AP, H_SELF) SNAP_IP, f,
	    sizeof(f));
	CHECK(wcn_nwifi_rx(f, n, SELF, &r) == WCN_RX_DROP, "rx own echo");
	CHECK(wcn_nwifi_rx(f, n, NULL, &r) == WCN_RX_OK, "rx echo unchecked");
	/* No DS bits (IBSS / TDLS shape). */
	n = hex(RXF("08", "00", H_SELF, "00 11 32 0a 0b 0c", H_AP) SNAP_IP, f,
	    sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && memcmp(r.sa, HOST, 6) == 0 &&
	    memcmp(r.bssid, AP, 6) == 0, "rx no DS");
	/* Four addresses. */
	n = hex("08 03 2c 00 " H_SELF " " H_AP " 02 00 00 00 00 77 00 00"
	    " 00 11 32 0a 0b 0c " SNAP_IP, f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.da[5] == 0x77 && memcmp(r.sa, HOST, 6) == 0 &&
	    r.type == 0x0800, "rx 4 addresses");
	CHECK(wcn_nwifi_rx(f, 29, SELF, &r) == WCN_RX_BAD, "rx 4 addr short");
	/* Bridge tunnel in, AARP under RFC 1042 kept as LLC, foreign LLC. */
	n = hex(RXF("08", "02", H_SELF, H_AP, "00 11 32 0a 0b 0c")
	    "aa aa 03 00 00 f8 81 37 ff ff", f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && !r.llc && r.type == 0x8137, "rx tunnel");
	n = hex(RXF("08", "02", H_SELF, H_AP, "00 11 32 0a 0b 0c")
	    "aa aa 03 00 00 00 80 f3 00 01", f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.llc && r.plen == 10, "rx rfc1042 aarp: llc");
	k = wcn_rx_eth(&r, e, sizeof(e));
	same(e, k, H_SELF " 00 11 32 0a 0b 0c 00 0a aa aa 03 00 00 00 80 f3 00 01",
	    "rx rfc1042 aarp as 802.3");
	n = hex(RXF("08", "02", H_SELF, H_AP, "00 11 32 0a 0b 0c")
	    "aa aa 03 00 00 00 05 dc 00 01", f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.llc, "rx snap with a length: llc");
	n = hex(RXF("08", "02", H_SELF, H_AP, "00 11 32 0a 0b 0c")
	    "aa aa 03 00 10 18 08 00 00 01", f, sizeof(f));
	rc = wcn_nwifi_rx(f, n, SELF, &r);
	CHECK(rc == WCN_RX_OK && r.llc, "rx other OUI: llc");
	/* An LLC payload over 1500 cannot be an 802.3 frame. */
	{
		static uint8_t big[24 + 1501];

		n = hex(RXF("08", "02", H_SELF, H_AP, H_AP), big, 24);
		memset(big + 24, 0x42, 1501);
		CHECK(wcn_nwifi_rx(big, sizeof(big), SELF, &r) == WCN_RX_OK &&
		    r.llc && wcn_rx_eth(&r, e, sizeof(e)) == 0,
		    "rx llc 1501");
	}
}

int
main(void)
{
	channels();
	beacons();
	soups();
	table();
	rates();
	built();
	answers();
	nwifi();
	printf("mlmetest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
