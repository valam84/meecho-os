/*
 * Host stand for minix/drivers/net/wcn/wcn_wpa.c.
 *
 *  - IEEE 802.11-2016 Annex J.4 (passphrase to PSK) and J.3 (PRF)
 *    vectors; RFC 3394 4.1 for the stand's own key wrap;
 *  - a PTK checked against an independent computation (Python's hashlib
 *    and hmac, see vectors() - no official full 4-way vector was at hand);
 *  - an authenticator written here, apart from the library (its own PRF
 *    on HMAC(), its own RFC 3394 wrap on AES-ECB, its own frame layout),
 *    doing the 4-way handshake, retransmissions, the group rekey and a
 *    PTK rekey against the library, and every way to get it wrong that
 *    the station must refuse - each refusal must leave the station's
 *    state as it was, byte for byte, and give nothing to send or install.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "wcn_wpa.h"

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

static size_t
unhex(const char *s, uint8_t *out, size_t cap)
{
	size_t n = 0;
	unsigned v;

	while (*s) {
		if (*s == ' ') {
			s++;
			continue;
		}
		if (n >= cap || sscanf(s, "%2x", &v) != 1)
			abort();
		out[n++] = (uint8_t)v;
		s += 2;
	}
	return n;
}

static int
same_hex(const uint8_t *p, size_t n, const char *hex)
{
	uint8_t w[256];
	size_t k = unhex(hex, w, sizeof(w));

	return k == n && memcmp(p, w, n) == 0;
}

/* ---- the stand's own crypto: HMAC(), AES-ECB ---- */

static void
t_hmac(const uint8_t *key, size_t klen, const uint8_t *d, size_t n,
	uint8_t out[20])
{
	unsigned l = 20;

	if (HMAC(EVP_sha1(), key, (int)klen, d, n, out, &l) == NULL ||
	    l != 20)
		abort();
}

static void
t_prf(const uint8_t *key, size_t klen, const char *label,
	const uint8_t *data, size_t dlen, uint8_t *out, size_t outlen)
{
	uint8_t buf[512], h[20];
	size_t ll = strlen(label), i, done;

	if (ll + 1 + dlen + 1 > sizeof(buf))
		abort();
	memcpy(buf, label, ll);
	buf[ll] = 0;
	memcpy(buf + ll + 1, data, dlen);
	for (i = 0, done = 0; done < outlen; i++) {
		buf[ll + 1 + dlen] = (uint8_t)i;
		t_hmac(key, klen, buf, ll + dlen + 2, h);
		memcpy(out + done, h, outlen - done < 20 ? outlen - done : 20);
		done += 20;
	}
}

static void
t_aes(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
	int n = 0;

	if (c == NULL ||
	    EVP_EncryptInit_ex(c, EVP_aes_128_ecb(), NULL, key, NULL) != 1 ||
	    EVP_CIPHER_CTX_set_padding(c, 0) != 1 ||
	    EVP_EncryptUpdate(c, out, &n, in, 16) != 1 || n != 16)
		abort();
	EVP_CIPHER_CTX_free(c);
}

/* RFC 3394 2.2.1, the index form: n 64-bit blocks into n + 1. */
static void
t_wrap(const uint8_t kek[16], const uint8_t *p, size_t len, uint8_t *out)
{
	uint8_t a[8], b[16], o[16];
	size_t n = len / 8, i, j, k;
	uint64_t t;

	if (len % 8 || len < 16)
		abort();
	memset(a, 0xa6, 8);
	memcpy(out + 8, p, len);
	for (j = 0; j <= 5; j++)
		for (i = 1; i <= n; i++) {
			memcpy(b, a, 8);
			memcpy(b + 8, out + 8 * i, 8);
			t_aes(kek, b, o);
			t = (uint64_t)n * j + i;
			for (k = 0; k < 8; k++)
				a[k] = o[k] ^ (uint8_t)(t >> (56 - 8 * k));
			memcpy(out + 8 * i, o + 8, 8);
		}
	memcpy(out, a, 8);
}

/* ---- the official vectors ---- */

static void
vectors(void)
{
	uint8_t pmk[32], out[64], ptk[48], k[64], d[128], w[64];
	size_t kl, dl;
	int r;

	/* J.4: passphrase, SSID, PSK. */
	static const struct {
		const char *pass, *ssid, *psk;
	} j4[] = {
		{ "password", "IEEE", "f42c6fc52df0ebef9ebb4b90b38a5f90"
		    "2e83fe1b135a70e23aed762e9710a12e" },
		{ "ThisIsAPassword", "ThisIsASSID",
		    "0dc0d6eb90555ed6419756b9a15ec3e3"
		    "209b63df707dd508d14581f8982721af" },
		{ "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		    "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ",
		    "becb93866bb8c3832cb777c2f559807c"
		    "8c59afcb6eae734885001300a981cc62" },
	};
	/* J.3: key, prefix, data, PRF-512. */
	static const struct {
		const char *key, *prefix, *data, *out;
	} j3[] = {
		{ "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", "prefix",
		    "4869205468657265",
		    "bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da3"
		    "5402ffee75df78c3d31e0f889f012120c0862beb67753e7439ae242e"
		    "db8373698356cf5a" },
		{ "4a656665", "prefix",
		    "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
		    "51f4de5b33f249adf81aeb713a3c20f4fe631446fabdfa58244759ae"
		    "58ef9009a99abf4eac2ca5fa87e692c440eb40023e7babb206d61de7"
		    "b92f41529092b8fc" },
		{ "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "prefix",
		    "dddddddddddddddddddddddddddddddddddddddddddddddddddd"
		    "dddddddddddddddddddddddddddddddddddddddddddddddd",
		    "e1ac546ec4cb636f9976487be5c86be17a0252ca5d8d8df12cfb0473"
		    "525249ce9dd8d177ead710bc9b590547239107aef7b4abd43d87f0a6"
		    "8f1cbd9e2b6f7607" },
	};
	unsigned i;

	for (i = 0; i < sizeof(j4) / sizeof(j4[0]); i++) {
		r = wpa_pmk((const uint8_t *)j4[i].ssid, strlen(j4[i].ssid),
		    j4[i].pass, pmk);
		CHECK(r == 0 && same_hex(pmk, 32, j4[i].psk),
		    "J.4 vector %u (\"%s\"/\"%s\")", i + 1, j4[i].pass,
		    j4[i].ssid);
	}
	printf("J.4 PSK vectors: %u\n", i);
	for (i = 0; i < sizeof(j3) / sizeof(j3[0]); i++) {
		kl = unhex(j3[i].key, k, sizeof(k));
		dl = unhex(j3[i].data, d, sizeof(d));
		r = wpa_prf_sha1(k, kl, j3[i].prefix, d, dl, out, 64);
		CHECK(r == 0 && same_hex(out, 64, j3[i].out), "J.3 vector %u",
		    i + 1);
		/* A shorter PRF is a prefix of the longer. */
		r = wpa_prf_sha1(k, kl, j3[i].prefix, d, dl, ptk, 48);
		CHECK(r == 0 && memcmp(ptk, out, 48) == 0,
		    "J.3 vector %u, 384 bits", i + 1);
	}
	printf("J.3 PRF vectors: %u\n", i);

	/* The hex form is the PSK itself. */
	r = wpa_pmk(NULL, 0, "0dc0d6eb90555ed6419756b9a15ec3e3"
	    "209b63df707dd508d14581f8982721af", pmk);
	CHECK(r == 0 && same_hex(pmk, 32, j4[1].psk), "hex PSK");
	r = wpa_pmk(NULL, 0, "0DC0D6EB90555ED6419756B9A15EC3E3"
	    "209B63DF707DD508D14581F8982721AF", pmk);
	CHECK(r == 0 && same_hex(pmk, 32, j4[1].psk), "hex PSK, capitals");
	CHECK(wpa_pmk(NULL, 0, "0dc0d6eb90555ed6419756b9a15ec3e3"
	    "209b63df707dd508d14581f8982721ag", pmk) == WPA_EINVAL,
	    "64 characters, not hex");
	CHECK(wpa_pmk((const uint8_t *)"x", 1, "1234567", pmk) == WPA_EINVAL,
	    "7-character passphrase");
	CHECK(wpa_pmk((const uint8_t *)"x", 1,
	    "0123456789012345678901234567890123456789012345678901234567890123"
	    "4", pmk) == WPA_EINVAL, "65-character passphrase");
	CHECK(wpa_pmk((const uint8_t *)"x", 1, "pass\tword", pmk) ==
	    WPA_EINVAL, "control character in passphrase");
	CHECK(wpa_pmk((const uint8_t *)"x", 0, "password", pmk) == WPA_EINVAL,
	    "empty SSID");
	CHECK(wpa_pmk((const uint8_t *)"0123456789012345678901234567890123",
	    33, "password", pmk) == WPA_EINVAL, "33-byte SSID");

	/*
	 * PTK: the J.4 PMK above, AA a0..a5, SPA b0..b5, ANonce e0..ff,
	 * SNonce c0..df.  Expected value from Python (hashlib, hmac) - an
	 * independent computation, not a published vector.
	 */
	{
		uint8_t aa[6], spa[6], an[32], sn[32];

		for (i = 0; i < 6; i++) {
			aa[i] = (uint8_t)(0xa0 + i);
			spa[i] = (uint8_t)(0xb0 + i);
		}
		for (i = 0; i < 32; i++) {
			an[i] = (uint8_t)(0xe0 + i);
			sn[i] = (uint8_t)(0xc0 + i);
		}
		unhex(j4[1].psk, pmk, 32);
		r = wpa_ptk(pmk, aa, spa, an, sn, ptk);
		CHECK(r == 0 && same_hex(ptk, 48,
		    "e677aae82af1fb9a25e93e22ad77022e"
		    "d90489e672adcc6ae148d0afafb92b69"
		    "36606222ae74d4d2d1db3ac91f6c2809"), "PTK");
		/* Either side's order gives the same PTK. */
		r = wpa_ptk(pmk, spa, aa, sn, an, out);
		CHECK(r == 0 && memcmp(out, ptk, 48) == 0, "PTK, swapped");
	}

	/* RFC 3394 4.1: the stand's own wrap. */
	unhex("000102030405060708090a0b0c0d0e0f", k, 16);
	unhex("00112233445566778899aabbccddeeff", d, 16);
	t_wrap(k, d, 16, w);
	CHECK(same_hex(w, 24, "1fa68b0a8112b447aef34bd8fb5a7b82"
	    "9d3e862371d2cfe5"), "RFC 3394 4.1 (the stand's wrap)");
}

/* ---- RSN element ---- */

static const char *beacon_ie_hex =
    "30 14 0100 000fac04 0100 000fac04 0100 000fac02 8c00";

static void
rsn(void)
{
	uint8_t ie[64], pmk[32] = { 0 }, mac[6] = { 0 };
	struct wpa_rsn r;
	struct wpa_sm sm;
	size_t n;

	n = wpa_rsnie(ie, sizeof(ie), 0);
	CHECK(n == WPA_RSNIE_LEN && same_hex(ie, n,
	    "30 14 0100 000fac04 0100 000fac04 0100 000fac02 0000"),
	    "own RSNE");
	CHECK(wpa_rsnie(ie, WPA_RSNIE_LEN - 1, 0) == 0, "own RSNE, no room");
	CHECK(wpa_rsn_parse(ie, n, &r) == 0 && r.group == WPA_SUITE_CCMP &&
	    r.pairwise_ccmp && r.akm_psk && !r.mfpr && !r.mfpc, "parse own");

	n = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac02 c000", ie,
	    sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == 0 && r.mfpr && r.mfpc, "MFPR");
	CHECK(wpa_init(&sm, mac, mac, pmk, ie, n, ie, n, NULL, NULL) ==
	    WPA_EINVAL, "no random source");

	/* Two pairwise suites, two AKMs, a PMKID count: read. */
	n = unhex("30 1e 0100 000fac04 0200 000fac02 000fac04 "
	    "0200 000fac01 000fac02 0000 0000", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == 0 && r.pairwise_ccmp && r.akm_psk,
	    "two suites each");
	/* Defaults: version only is CCMP/CCMP/802.1X. */
	n = unhex("30 02 0100", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == 0 && r.pairwise_ccmp &&
	    r.group == WPA_SUITE_CCMP && !r.akm_psk, "defaults");
	/* Malformed. */
	n = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac02 00", ie,
	    sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == WPA_EINVAL, "length disagrees");
	n = unhex("30 0b 0100 000fac04 0200 000fac04 00", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == WPA_EINVAL, "count past the end");
	n = unhex("30 0a 0100 000fac04 0000 0000", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == WPA_EINVAL, "zero count");
	n = unhex("30 06 0200 000fac04", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == WPA_EINVAL, "version 2");
	n = unhex("dd 06 0100 000fac04", ie, sizeof(ie));
	CHECK(wpa_rsn_parse(ie, n, &r) == WPA_EINVAL, "not an RSNE");
}

/* ---- the authenticator ---- */

/* Its view of the EAPOL-Key frame, written apart from the library's. */
#define T_INFO		5
#define T_RC		9
#define T_NONCE		17
#define T_RSC		65
#define T_MIC		81
#define T_KDLEN		97
#define T_KD		99

#define I_M1	0x008a		/* v2, pairwise, ack */
#define I_M2	0x010a		/* v2, pairwise, mic */
#define I_M3	0x13ca		/* v2, pairwise, install, ack, mic, secure, enc */
#define I_M4	0x030a		/* v2, pairwise, mic, secure */
#define I_G1	0x1382		/* v2, ack, mic, secure, enc */
#define I_G2	0x0302		/* v2, mic, secure */

#define B_INSTALL	0x0040
#define B_SECURE	0x0200
#define B_ENC		0x1000

struct auth {
	uint8_t aa[6], spa[6], pmk[32];
	uint8_t anonce[32], snonce[32], ptk[48];
	uint64_t rc;
	uint8_t ie[64];
	size_t ie_len;
	uint8_t gtk[16];
	unsigned gtk_idx;
	uint8_t rsc[8];
};

static size_t
t_frame(uint8_t *f, uint16_t info, uint16_t keylen, uint64_t rc,
	const uint8_t *nonce, const uint8_t *rsc, const uint8_t *kd,
	size_t kdlen, const uint8_t *kck)
{
	size_t n = T_KD + kdlen, i;
	uint8_t *buf, h[20];

	memset(f, 0, T_KD);
	f[0] = 2;			/* 802.1X-2004 */
	f[1] = 3;			/* EAPOL-Key */
	f[2] = (uint8_t)((n - 4) >> 8);
	f[3] = (uint8_t)(n - 4);
	f[4] = 2;			/* RSN */
	f[T_INFO] = (uint8_t)(info >> 8);
	f[T_INFO + 1] = (uint8_t)info;
	f[7] = (uint8_t)(keylen >> 8);
	f[8] = (uint8_t)keylen;
	for (i = 0; i < 8; i++)
		f[T_RC + 7 - i] = (uint8_t)(rc >> (8 * i));
	if (nonce)
		memcpy(f + T_NONCE, nonce, 32);
	if (rsc)
		memcpy(f + T_RSC, rsc, 8);
	f[T_KDLEN] = (uint8_t)(kdlen >> 8);
	f[T_KDLEN + 1] = (uint8_t)kdlen;
	memcpy(f + T_KD, kd, kdlen);
	if (kck) {
		/* The MIC field is zero here: the frame as it is. */
		buf = f;
		t_hmac(kck, 16, buf, n, h);
		memcpy(f + T_MIC, h, 16);
	}
	return n;
}

static int
t_mic_ok(const uint8_t *kck, const uint8_t *f, size_t n)
{
	uint8_t c[1024], h[20];

	if (n > sizeof(c))
		return 0;
	memcpy(c, f, n);
	memset(c + T_MIC, 0, 16);
	t_hmac(kck, 16, c, n, h);
	return memcmp(h, f + T_MIC, 16) == 0;
}

static uint64_t
t_rc(const uint8_t *f)
{
	uint64_t v = 0;
	int i;

	for (i = 0; i < 8; i++)
		v = v << 8 | f[T_RC + i];
	return v;
}

static uint16_t
t_info(const uint8_t *f)
{

	return (uint16_t)(f[T_INFO] << 8 | f[T_INFO + 1]);
}

static void
t_derive(struct auth *a)
{
	uint8_t d[76];
	const uint8_t *m1 = a->aa, *m2 = a->spa, *n1 = a->anonce,
	    *n2 = a->snonce, *t;

	if (memcmp(m1, m2, 6) > 0) {
		t = m1; m1 = m2; m2 = t;
	}
	if (memcmp(n1, n2, 32) > 0) {
		t = n1; n1 = n2; n2 = t;
	}
	memcpy(d, m1, 6);
	memcpy(d + 6, m2, 6);
	memcpy(d + 12, n1, 32);
	memcpy(d + 44, n2, 32);
	t_prf(a->pmk, 32, "Pairwise key expansion", d, 76, a->ptk, 48);
}

static size_t
a_m1(struct auth *a, uint8_t *f)
{

	return t_frame(f, I_M1, 16, ++a->rc, a->anonce, NULL,
	    (const uint8_t *)"", 0, NULL);
}

/* Message 2 as the AP checks it; the PTK follows from its SNonce. */
static int
a_m2(struct auth *a, const uint8_t *f, size_t n, const uint8_t *sta_ie,
	size_t sta_ie_len)
{

	if (n != T_KD + sta_ie_len || f[0] != 2 || f[1] != 3 || f[4] != 2 ||
	    t_info(f) != I_M2 || t_rc(f) != a->rc ||
	    f[T_KDLEN] != 0 || f[T_KDLEN + 1] != sta_ie_len ||
	    memcmp(f + T_KD, sta_ie, sta_ie_len) != 0)
		return 0;
	memcpy(a->snonce, f + T_NONCE, 32);
	t_derive(a);
	return t_mic_ok(a->ptk, f, n);
}

/* Empty answers: message 4 and group message 2. */
static int
a_ack(const struct auth *a, const uint8_t *f, size_t n, uint16_t info,
	uint64_t rc)
{
	static const uint8_t zero[32];

	return n == T_KD && t_info(f) == info && t_rc(f) == rc &&
	    memcmp(f + T_NONCE, zero, 32) == 0 && t_mic_ok(a->ptk, f, n);
}

struct kd {
	const uint8_t *ie;		/* first RSNE, NULL - none */
	size_t ie_len;
	const uint8_t *ie2;
	size_t ie2_len;
	size_t extra;			/* vendor element of this size first */
	int ngtk;			/* GTK KDEs */
	size_t gtk_len;
	int badpad;
};

static size_t
a_keydata(const struct auth *a, const struct kd *o, uint8_t *w)
{
	uint8_t p[256];
	size_t n = 0, i;
	int g;

	if (o->extra) {
		p[n++] = 0xdd;
		p[n++] = (uint8_t)(o->extra - 2);
		p[n++] = 0x00; p[n++] = 0x50; p[n++] = 0xf2;
		for (i = 5; i < o->extra; i++)
			p[n++] = 0x77;
	}
	if (o->ie) {
		memcpy(p + n, o->ie, o->ie_len);
		n += o->ie_len;
	}
	if (o->ie2) {
		memcpy(p + n, o->ie2, o->ie2_len);
		n += o->ie2_len;
	}
	for (g = 0; g < o->ngtk; g++) {
		p[n++] = 0xdd;
		p[n++] = (uint8_t)(6 + o->gtk_len);
		p[n++] = 0x00; p[n++] = 0x0f; p[n++] = 0xac; p[n++] = 0x01;
		p[n++] = (uint8_t)a->gtk_idx;
		p[n++] = 0;
		for (i = 0; i < o->gtk_len; i++)
			p[n++] = i < 16 ? a->gtk[i] : 0x55;
	}
	if (n % 8 != 0 || n < 16) {
		p[n++] = 0xdd;
		while (n % 8 != 0 || n < 16)
			p[n++] = 0x00;
		/* dd 00 .. 01: looks like padding up to its last byte. */
		if (o->badpad)
			p[n - 1] = 0x01;
	}
	t_wrap(a->ptk + 16, p, n, w);
	return n + 8;
}

static struct kd
kd_m3_default(const struct auth *a)
{
	struct kd o;

	memset(&o, 0, sizeof(o));
	o.ie = a->ie;
	o.ie_len = a->ie_len;
	o.ngtk = 1;
	o.gtk_len = 16;
	return o;
}

static size_t
a_m3(struct auth *a, uint8_t *f, const struct kd *o, uint16_t info)
{
	uint8_t w[300];
	size_t n = a_keydata(a, o, w);

	return t_frame(f, info, 16, ++a->rc, a->anonce, a->rsc, w, n, a->ptk);
}

static size_t
a_g1(struct auth *a, uint8_t *f, const struct kd *o)
{
	uint8_t w[300];
	size_t n = a_keydata(a, o, w);

	return t_frame(f, I_G1, 0, ++a->rc, NULL, a->rsc, w, n, a->ptk);
}

/* ---- the station ---- */

static uint8_t rnd_ctr;
static int rnd_fail;

static int
rnd(void *arg, uint8_t *buf, size_t n)
{
	size_t i;

	(void)arg;
	if (rnd_fail)
		return -1;
	for (i = 0; i < n; i++)
		buf[i] = (uint8_t)(rnd_ctr * 37 + i * 11 + 5);
	rnd_ctr++;
	return 0;
}

static const uint8_t STA[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x01 };
static const uint8_t AP[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0xff };
static uint8_t sta_ie[WPA_RSNIE_LEN];
static struct wpa_out out;

static int
nothing(const struct wpa_out *o)
{

	return o->len == 0 && !o->set_ptk && !o->set_gtk && !o->done;
}

/* A frame the station must refuse: err (or any, if 0), state intact. */
static void
refuse(const struct wpa_sm *sm, const uint8_t *f, size_t n, int err,
	const char *what)
{
	struct wpa_sm c = *sm;
	int r;

	r = wpa_rx(&c, f, n, &out);
	CHECK(err ? r == err : r != 0, "%s: %d (%s), want %d", what, r,
	    wpa_strerror(r), err);
	CHECK(memcmp(&c, sm, sizeof(c)) == 0, "%s: state changed", what);
	CHECK(nothing(&out), "%s: output on refusal", what);
}

static void
auth_init(struct auth *a, const char *pass)
{
	unsigned i;

	memset(a, 0, sizeof(*a));
	memcpy(a->aa, AP, 6);
	memcpy(a->spa, STA, 6);
	if (wpa_pmk((const uint8_t *)"MEECHO", 6, pass, a->pmk) != 0)
		abort();
	for (i = 0; i < 32; i++)
		a->anonce[i] = (uint8_t)(0x90 + 3 * i);
	a->ie_len = unhex(beacon_ie_hex, a->ie, sizeof(a->ie));
	for (i = 0; i < 16; i++)
		a->gtk[i] = (uint8_t)(0x40 + i);
	a->gtk_idx = 1;
	for (i = 0; i < 6; i++)
		a->rsc[i] = (uint8_t)(i + 1);
}

static void
sta_init(struct wpa_sm *sm, const char *pass, const struct auth *a)
{
	uint8_t pmk[32];
	int r;

	wpa_rsnie(sta_ie, sizeof(sta_ie), 0);
	if (wpa_pmk((const uint8_t *)"MEECHO", 6, pass, pmk) != 0)
		abort();
	r = wpa_init(sm, STA, AP, pmk, sta_ie, sizeof(sta_ie), a->ie,
	    a->ie_len, rnd, NULL);
	CHECK(r == 0, "wpa_init: %s", wpa_strerror(r));
}

/* Run to just after message 2. */
static void
to_m2(struct wpa_sm *sm, struct auth *a)
{
	uint8_t f[512];
	size_t n;
	int r;

	n = a_m1(a, f);
	r = wpa_rx(sm, f, n, &out);
	CHECK(r == 0 && out.len > 0 && !out.set_ptk && !out.set_gtk &&
	    sm->state == WPA_S_M2, "M1: %s", wpa_strerror(r));
	CHECK(a_m2(a, out.frame, out.len, sta_ie, sizeof(sta_ie)), "M2");
}

static void
success(void)
{
	struct wpa_sm sm, before;
	struct auth a;
	struct kd o;
	uint8_t f[512], tk[16], sn1[32];
	size_t n;
	int r;

	auth_init(&a, "correct horse battery");
	sta_init(&sm, "correct horse battery", &a);
	CHECK(!wpa_authorized(&sm) && sm.state == WPA_S_IDLE, "idle");

	/* M1, then the AP repeats it: the same SNonce. */
	to_m2(&sm, &a);
	memcpy(sn1, a.snonce, 32);
	n = a_m1(&a, f);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && a_m2(&a, out.frame, out.len, sta_ie, sizeof(sta_ie)) &&
	    memcmp(a.snonce, sn1, 32) == 0, "repeated M1: same SNonce");

	o = kd_m3_default(&a);
	n = a_m3(&a, f, &o, I_M3);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0, "M3: %s", wpa_strerror(r));
	CHECK(a_ack(&a, out.frame, out.len, I_M4, a.rc), "M4");
	CHECK(out.set_ptk && memcmp(out.tk, a.ptk + 32, 16) == 0, "TK");
	CHECK(out.set_gtk && out.gtk_idx == 1 && out.gtk_len == 16 &&
	    memcmp(out.gtk, a.gtk, 16) == 0 &&
	    memcmp(out.rsc, a.rsc, 8) == 0, "GTK and RSC from M3");
	CHECK(out.done && wpa_authorized(&sm) && sm.state == WPA_S_DONE,
	    "authorized");
	memcpy(tk, out.tk, 16);

	/* The same M3 again: a replay. */
	refuse(&sm, f, n, WPA_EREPLAY, "replayed M3");
	/* M4 was lost, the AP retransmits M3 (counter up): M4, no keys. */
	n = a_m3(&a, f, &o, I_M3);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && a_ack(&a, out.frame, out.len, I_M4, a.rc) &&
	    !out.set_ptk && !out.set_gtk, "retransmitted M3: M4, no keys");

	/* Group rekey. */
	a.gtk[0] ^= 0xff;
	a.gtk_idx = 2;
	a.rsc[0] = 0x77;
	memset(&o, 0, sizeof(o));
	o.ngtk = 1;
	o.gtk_len = 16;
	n = a_g1(&a, f, &o);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && a_ack(&a, out.frame, out.len, I_G2, a.rc),
	    "group 1/2 -> 2/2: %s", wpa_strerror(r));
	CHECK(out.set_gtk && !out.set_ptk && out.gtk_idx == 2 &&
	    memcmp(out.gtk, a.gtk, 16) == 0 && out.rsc[0] == 0x77,
	    "new GTK");
	refuse(&sm, f, n, WPA_EREPLAY, "replayed group 1/2");
	n = a_g1(&a, f, &o);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && a_ack(&a, out.frame, out.len, I_G2, a.rc) &&
	    !out.set_gtk, "same GTK again: answered, not reinstalled");

	/* Group rekey with padding of every length. */
	for (o.extra = 0; o.extra < 16; o.extra++) {
		if (o.extra > 0 && o.extra < 6)
			continue;
		a.gtk[1]++;
		n = a_g1(&a, f, &o);
		r = wpa_rx(&sm, f, n, &out);
		CHECK(r == 0 && out.set_gtk &&
		    memcmp(out.gtk, a.gtk, 16) == 0,
		    "group 1/2, vendor element %zu: %s", o.extra,
		    wpa_strerror(r));
	}

	/* PTK rekey: a new 4-way handshake while authorized. */
	for (n = 0; n < 32; n++)
		a.anonce[n] ^= 0x5a;
	before = sm;
	to_m2(&sm, &a);
	CHECK(wpa_authorized(&sm) && memcmp(sm.ptk, before.ptk, 48) == 0,
	    "rekey: old PTK in use until M3");
	o = kd_m3_default(&a);
	n = a_m3(&a, f, &o, I_M3);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && out.set_ptk && memcmp(out.tk, a.ptk + 32, 16) == 0 &&
	    memcmp(out.tk, tk, 16) != 0 && !out.set_gtk,
	    "rekey M3: new TK, same GTK not reinstalled");
	CHECK(a_ack(&a, out.frame, out.len, I_M4, a.rc), "rekey M4");

	/* Ethernet padding after the body is not part of the frame. */
	a.gtk[2]++;
	memset(&o, 0, sizeof(o));
	o.ngtk = 1;
	o.gtk_len = 16;
	n = a_g1(&a, f, &o);
	memset(f + n, 0xee, 10);
	r = wpa_rx(&sm, f, n + 10, &out);
	CHECK(r == 0 && out.set_gtk, "trailing bytes ignored: %s",
	    wpa_strerror(r));
	wpa_clear(&sm);
}

/* Every way message 3 can be wrong. */
static void
bad_m3(void)
{
	struct wpa_sm sm;
	struct auth a, a0;
	struct kd o;
	uint8_t f[512], ie[64];
	size_t n, k;

	/* Wrong passphrase: M2's MIC fails at the AP, M3's at the station. */
	auth_init(&a, "the right one");
	sta_init(&sm, "the wrong one", &a);
	n = a_m1(&a, f);
	CHECK(wpa_rx(&sm, f, n, &out) == 0, "M1, wrong passphrase");
	CHECK(!a_m2(&a, out.frame, out.len, sta_ie, sizeof(sta_ie)),
	    "AP accepted M2 under another passphrase");
	o = kd_m3_default(&a);
	n = a_m3(&a, f, &o, I_M3);
	refuse(&sm, f, n, WPA_EMIC, "M3, wrong passphrase");
	CHECK(!wpa_authorized(&sm), "authorized with a wrong passphrase");

	auth_init(&a, "the right one");
	sta_init(&sm, "the right one", &a);
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_ESTATE, "M3 before M1");
	to_m2(&sm, &a);
	a0 = a;

	/* ANonce changed between M1 and M3 (and the MIC right). */
	a.anonce[5] ^= 1;
	o = kd_m3_default(&a);
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_ENONCE, "M3, other ANonce");
	a = a0;

	/* RSNE not the beacon's: other capabilities. */
	memcpy(ie, a.ie, a.ie_len);
	ie[a.ie_len - 2] = 0;
	o.ie = ie;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EIE, "M3, other RSNE");
	a = a0;
	o = kd_m3_default(&a);
	o.ie = NULL;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EIE, "M3, no RSNE");
	a = a0;
	o = kd_m3_default(&a);
	k = unhex("30 14 0100 000fac04 0100 000fac02 0100 000fac02 0000", ie,
	    sizeof(ie));
	o.ie2 = ie;
	o.ie2_len = k;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EIE, "M3, second RSNE TKIP");
	a = a0;
	k = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac02 0000", ie,
	    sizeof(ie));
	o.ie2_len = k;
	n = a_m3(&a, f, &o, I_M3);
	{
		struct wpa_sm c = sm;

		CHECK(wpa_rx(&c, f, n, &out) == 0, "M3, second RSNE CCMP");
	}
	a = a0;

	/* Key data. */
	o = kd_m3_default(&a);
	o.ngtk = 0;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE, "M3, no GTK");
	a = a0;
	o.ngtk = 2;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE, "M3, two GTKs");
	a = a0;
	o.ngtk = 1;
	o.gtk_len = 32;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE, "M3, 32-byte GTK");
	a = a0;
	o.gtk_len = 16;
	o.badpad = 1;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE,
	    "M3, bad padding (dd 01)");
	a = a0;
	o.extra = 6;		/* 52 bytes: dd 00 00 01 */
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE,
	    "M3, bad padding (dd 00 00 01)");
	a = a0;
	o.extra = 0;
	o.badpad = 0;
	a.gtk_idx = 0;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3), WPA_EKDE, "M3, GTK index 0");
	a = a0;
	/* Wrapped data damaged, the MIC made right again. */
	n = a_m3(&a, f, &o, I_M3);
	f[T_KD + 3] ^= 1;
	memset(f + T_MIC, 0, 16);
	{
		uint8_t h[20];

		t_hmac(a.ptk, 16, f, n, h);
		memcpy(f + T_MIC, h, 16);
	}
	refuse(&sm, f, n, WPA_EKDE, "M3, key data fails to unwrap");
	a = a0;

	/* Key information. */
	refuse(&sm, f, a_m3(&a, f, &o, I_M3 & ~B_INSTALL), WPA_EBITS,
	    "M3 without Install");
	a = a0;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3 & ~B_SECURE), WPA_EBITS,
	    "M3 without Secure");
	a = a0;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3 & ~B_ENC), WPA_EBITS,
	    "M3 without Encrypted Key Data");
	a = a0;
	refuse(&sm, f, a_m3(&a, f, &o, (I_M3 & ~7) | 1), WPA_EBITS,
	    "M3, descriptor version 1");
	a = a0;
	refuse(&sm, f, a_m3(&a, f, &o, I_M3 | 0x0800), WPA_EBITS,
	    "M3 with Request");
	a = a0;

	/* Replay counter not above M1's... is allowed; equal to an M3's not. */
	n = a_m3(&a, f, &o, I_M3);
	{
		struct wpa_sm c = sm;

		CHECK(wpa_rx(&c, f, n, &out) == 0, "good M3");
		refuse(&c, f, n, WPA_EREPLAY, "M3 replayed");
		/* An older M1 after it: replay too. */
		a.rc -= 3;
		k = a_m1(&a, f);
		refuse(&c, f, k, WPA_EREPLAY, "old M1 after M3");
		a.rc += 1;
		/* A group message with M3's counter. */
		memset(&o, 0, sizeof(o));
		o.ngtk = 1;
		o.gtk_len = 16;
		k = a_g1(&a, f, &o);
		refuse(&c, f, k, WPA_EREPLAY, "group 1/2 with M3's counter");
	}
	a = a0;

	/* Every byte of M3 flipped; every truncation. */
	o = kd_m3_default(&a);
	n = a_m3(&a, f, &o, I_M3);
	for (k = 0; k < n; k++) {
		uint8_t g[512];
		char what[64];

		memcpy(g, f, n);
		g[k] ^= 0x10;
		snprintf(what, sizeof(what), "M3, byte %zu flipped", k);
		refuse(&sm, g, n, 0, what);
	}
	for (k = 0; k < n; k++) {
		uint8_t g[512];
		char what[64];

		memcpy(g, f, k);
		snprintf(what, sizeof(what), "M3 cut to %zu", k);
		refuse(&sm, g, k, 0, what);
		if (k >= 4) {
			/* The 802.1X length made to agree with the cut. */
			g[2] = (uint8_t)((k - 4) >> 8);
			g[3] = (uint8_t)(k - 4);
			snprintf(what, sizeof(what), "M3 cut to %zu, length "
			    "fixed", k);
			refuse(&sm, g, k, 0, what);
		}
	}
	/* M3 with padding of every length (vendor element in front). */
	for (o.extra = 0; o.extra < 16; o.extra++) {
		struct wpa_sm c = sm;
		int r;

		if (o.extra > 0 && o.extra < 6)
			continue;
		a = a0;
		n = a_m3(&a, f, &o, I_M3);
		r = wpa_rx(&c, f, n, &out);
		CHECK(r == 0 && out.set_gtk && memcmp(out.gtk, a.gtk, 16) == 0,
		    "M3, vendor element %zu: %s", o.extra, wpa_strerror(r));
	}
	wpa_clear(&sm);
}

static void
bad_other(void)
{
	struct wpa_sm sm;
	struct auth a;
	struct kd o;
	uint8_t f[512], ie[64], pmk[32] = { 1 };
	size_t n, k;
	int r;

	/* The AP's element decides whether to start at all. */
	auth_init(&a, "abcdefgh");
	wpa_rsnie(sta_ie, sizeof(sta_ie), 0);
	n = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac02 c000", ie,
	    sizeof(ie));
	CHECK(wpa_init(&sm, STA, AP, pmk, sta_ie, sizeof(sta_ie), ie, n, rnd,
	    NULL) == WPA_ENOTSUP, "AP requires PMF");
	n = unhex("30 14 0100 000fac02 0100 000fac04 0100 000fac02 0000", ie,
	    sizeof(ie));
	CHECK(wpa_init(&sm, STA, AP, pmk, sta_ie, sizeof(sta_ie), ie, n, rnd,
	    NULL) == WPA_ENOTSUP, "AP's group cipher TKIP");
	n = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac08 0000", ie,
	    sizeof(ie));
	CHECK(wpa_init(&sm, STA, AP, pmk, sta_ie, sizeof(sta_ie), ie, n, rnd,
	    NULL) == WPA_ENOTSUP, "AP's AKM SAE only");
	n = unhex("30 14 0100 000fac04 0100 000fac04 0100 000fac02 0000", ie,
	    sizeof(ie));
	CHECK(wpa_init(&sm, STA, AP, pmk, ie, n - 1, ie, n, rnd, NULL) ==
	    WPA_EINVAL, "own RSNE malformed");

	auth_init(&a, "abcdefgh");
	sta_init(&sm, "abcdefgh", &a);

	/* Group message before any PTK. */
	memset(&o, 0, sizeof(o));
	o.ngtk = 1;
	o.gtk_len = 16;
	refuse(&sm, f, a_g1(&a, f, &o), WPA_ESTATE, "group 1/2 before PTK");

	/* Not a key frame, not RSN, M1 with odd bits. */
	n = a_m1(&a, f);
	f[1] = 0;
	refuse(&sm, f, n, WPA_EFRAME, "EAP-Packet");
	f[1] = 3;
	f[4] = 254;
	refuse(&sm, f, n, WPA_EFRAME, "WPA1 descriptor");
	f[4] = 2;
	f[T_INFO + 1] |= B_INSTALL;
	refuse(&sm, f, n, WPA_EBITS, "M1 with Install");
	f[T_INFO + 1] &= (uint8_t)~B_INSTALL;
	f[T_INFO + 1] &= (uint8_t)~0x80;
	refuse(&sm, f, n, WPA_EBITS, "M1 without Ack");
	f[T_INFO + 1] |= 0x80;
	f[8] = 32;
	refuse(&sm, f, n, WPA_EBITS, "M1, key length 32");
	f[8] = 16;

	/* No random bytes: no M2. */
	rnd_fail = 1;
	refuse(&sm, f, n, WPA_ERANDOM, "M1 without random bytes");
	rnd_fail = 0;

	/* Every truncation of M1. */
	for (k = 0; k < n; k++) {
		char what[64];

		snprintf(what, sizeof(what), "M1 cut to %zu", k);
		refuse(&sm, f, k, 0, what);
	}
	refuse(&sm, NULL, 0, 0, "no frame");

	/* A new ANonce in a second M1 gets a new SNonce. */
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0, "M1");
	memcpy(ie, out.frame + T_NONCE, 32);
	a.anonce[0] ^= 1;
	n = a_m1(&a, f);
	r = wpa_rx(&sm, f, n, &out);
	CHECK(r == 0 && memcmp(ie, out.frame + T_NONCE, 32) != 0,
	    "other ANonce, other SNonce");
	CHECK(a_m2(&a, out.frame, out.len, sta_ie, sizeof(sta_ie)), "M2");
	o = kd_m3_default(&a);
	n = a_m3(&a, f, &o, I_M3);
	CHECK(wpa_rx(&sm, f, n, &out) == 0 && out.set_ptk, "M3 to that");

	/* Every truncation and every flipped byte of group message 1. */
	memset(&o, 0, sizeof(o));
	o.ngtk = 1;
	o.gtk_len = 16;
	a.gtk[3]++;
	n = a_g1(&a, f, &o);
	for (k = 0; k < n; k++) {
		uint8_t g[512];
		char what[64];

		memcpy(g, f, n);
		g[k] ^= 0x01;
		snprintf(what, sizeof(what), "group 1/2, byte %zu flipped", k);
		refuse(&sm, g, n, 0, what);
		snprintf(what, sizeof(what), "group 1/2 cut to %zu", k);
		refuse(&sm, f, k, 0, what);
	}
	refuse(&sm, f, a_g1(&a, f, &(struct kd){ .ngtk = 0 }), WPA_EKDE,
	    "group 1/2 without GTK");
	/* 24 bytes of GTK KDE need no padding: a vendor element makes 33. */
	o.badpad = 1;
	o.extra = 9;
	refuse(&sm, f, a_g1(&a, f, &o), WPA_EKDE, "group 1/2, bad padding");
	o.badpad = 0;
	o.extra = 0;
	/* Install set in a group message. */
	n = a_g1(&a, f, &o);
	f[T_INFO + 1] |= B_INSTALL;
	refuse(&sm, f, n, WPA_EBITS, "group 1/2 with Install");
	r = wpa_rx(&sm, f, a_g1(&a, f, &o), &out);
	CHECK(r == 0 && a_ack(&a, out.frame, out.len, I_G2, a.rc),
	    "group 1/2 after all that: %s", wpa_strerror(r));
	wpa_clear(&sm);
}

int
main(void)
{

	vectors();
	rsn();
	success();
	bad_m3();
	bad_other();
	printf("wpatest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
