/*
 * WPA2-Personal, the station's side (wcn_wpa.h), from IEEE 802.11-2016:
 * 12.7.1.2 (PRF), 12.7.1.3 (PTK), 12.7.2 (EAPOL-Key frames), 12.7.6
 * (4-way handshake), 12.7.7 (group key handshake), 9.4.2.25 (RSNE),
 * 12.7.2 Table 12-6 (KDEs).
 *
 * A handler checks everything first and writes the state last: a frame
 * refused leaves the state as it was, byte for byte (the stand compares
 * the whole structure).
 */

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include "wcn_wpa.h"

/* ---- the crypto, behind three calls ---- */

struct piece {
	const uint8_t *p;
	size_t n;
};

#define SHA1_LEN	20
#define PBKDF2_ROUNDS	4096

static int
cr_pbkdf2(const char *pass, size_t plen, const uint8_t *salt, size_t slen,
	uint8_t *out, size_t outlen)
{

	if (PKCS5_PBKDF2_HMAC(pass, (int)plen, salt, (int)slen,
	    PBKDF2_ROUNDS, EVP_sha1(), (int)outlen, out) != 1)
		return WPA_ECRYPTO;
	return 0;
}

/* HMAC-SHA1 over the pieces, one after another. */
static int
cr_hmac_sha1(const uint8_t *key, size_t klen, const struct piece *v,
	unsigned nv, uint8_t out[SHA1_LEN])
{
	EVP_MAC *mac;
	EVP_MAC_CTX *ctx = NULL;
	OSSL_PARAM par[2];
	char digest[] = "SHA1";
	size_t outl = 0;
	unsigned i;
	int r = WPA_ECRYPTO;

	if ((mac = EVP_MAC_fetch(NULL, "HMAC", NULL)) == NULL)
		return WPA_ECRYPTO;
	if ((ctx = EVP_MAC_CTX_new(mac)) == NULL)
		goto out;
	par[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
	    digest, 0);
	par[1] = OSSL_PARAM_construct_end();
	if (EVP_MAC_init(ctx, key, klen, par) != 1)
		goto out;
	for (i = 0; i < nv; i++)
		if (v[i].n > 0 && EVP_MAC_update(ctx, v[i].p, v[i].n) != 1)
			goto out;
	if (EVP_MAC_final(ctx, out, &outl, SHA1_LEN) != 1 ||
	    outl != SHA1_LEN)
		goto out;
	r = 0;
out:
	EVP_MAC_CTX_free(ctx);
	EVP_MAC_free(mac);
	return r;
}

/*
 * AES key unwrap (RFC 3394, the default IV): inlen - 8 bytes into out,
 * which has room for inlen.  The integrity check failing is WPA_EKDE.
 */
static int
cr_unwrap(const uint8_t kek[WPA_KEK_LEN], const uint8_t *in, size_t inlen,
	uint8_t *out)
{
	EVP_CIPHER_CTX *ctx;
	int n = 0, m = 0, r = WPA_EKDE;

	if (inlen < 24 || inlen % 8 != 0 || inlen > WPA_RX_MAX)
		return WPA_EKDE;
	if ((ctx = EVP_CIPHER_CTX_new()) == NULL)
		return WPA_ECRYPTO;
	EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
	if (EVP_DecryptInit_ex(ctx, EVP_aes_128_wrap(), NULL, kek, NULL) != 1)
		goto out;
	if (EVP_DecryptUpdate(ctx, out, &n, in, (int)inlen) != 1)
		goto out;
	if (EVP_DecryptFinal_ex(ctx, out + n, &m) != 1)
		goto out;
	if ((size_t)(n + m) != inlen - 8)
		goto out;
	r = 0;
out:
	EVP_CIPHER_CTX_free(ctx);
	return r;
}

/* ---- the PMK, the PRF and the PTK ---- */

static int
hexval(char c)
{

	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int
wpa_pmk(const uint8_t *ssid, size_t ssid_len, const char *passphrase,
	uint8_t pmk[WPA_PMK_LEN])
{
	size_t n, i;
	int h, l;

	if (passphrase == NULL || pmk == NULL)
		return WPA_EINVAL;
	n = strnlen(passphrase, 65);
	if (n == 2 * WPA_PMK_LEN) {
		for (i = 0; i < WPA_PMK_LEN; i++) {
			h = hexval(passphrase[2 * i]);
			l = hexval(passphrase[2 * i + 1]);
			if (h < 0 || l < 0) {
				OPENSSL_cleanse(pmk, WPA_PMK_LEN);
				return WPA_EINVAL;
			}
			pmk[i] = (uint8_t)(h << 4 | l);
		}
		return 0;
	}
	if (n < 8 || n > 63)
		return WPA_EINVAL;
	for (i = 0; i < n; i++)
		if (passphrase[i] < 32 || passphrase[i] > 126)
			return WPA_EINVAL;
	if (ssid == NULL || ssid_len < 1 || ssid_len > 32)
		return WPA_EINVAL;
	return cr_pbkdf2(passphrase, n, ssid, ssid_len, pmk, WPA_PMK_LEN);
}

/* PRF-n: HMAC-SHA1(K, A || 0 || B || i), i = 0, 1, ..., cut to n. */
int
wpa_prf_sha1(const uint8_t *key, size_t klen, const char *label,
	const uint8_t *data, size_t dlen, uint8_t *out, size_t outlen)
{
	static const uint8_t zero = 0;
	uint8_t h[SHA1_LEN], i;
	struct piece v[4];
	size_t done, k;
	int r;

	if (key == NULL || label == NULL || (data == NULL && dlen > 0) ||
	    out == NULL || outlen > 255 * SHA1_LEN)
		return WPA_EINVAL;
	v[0].p = (const uint8_t *)label;
	v[0].n = strlen(label);
	v[1].p = &zero;
	v[1].n = 1;
	v[2].p = data;
	v[2].n = dlen;
	v[3].p = &i;
	v[3].n = 1;
	for (i = 0, done = 0; done < outlen; i++, done += k) {
		if ((r = cr_hmac_sha1(key, klen, v, 4, h)) != 0) {
			OPENSSL_cleanse(out, outlen);
			return r;
		}
		k = outlen - done < SHA1_LEN ? outlen - done : SHA1_LEN;
		memcpy(out + done, h, k);
	}
	OPENSSL_cleanse(h, sizeof(h));
	return 0;
}

#define MINMAX(a, b, n, lo, hi) do {					\
	if (memcmp((a), (b), (n)) < 0) {				\
		(lo) = (a); (hi) = (b);					\
	} else {							\
		(lo) = (b); (hi) = (a);					\
	}								\
} while (0)

int
wpa_ptk(const uint8_t pmk[WPA_PMK_LEN], const uint8_t aa[6],
	const uint8_t spa[6], const uint8_t anonce[WPA_NONCE_LEN],
	const uint8_t snonce[WPA_NONCE_LEN], uint8_t ptk[WPA_PTK_LEN])
{
	uint8_t d[2 * 6 + 2 * WPA_NONCE_LEN];
	const uint8_t *lo, *hi;

	MINMAX(aa, spa, 6, lo, hi);
	memcpy(d, lo, 6);
	memcpy(d + 6, hi, 6);
	MINMAX(anonce, snonce, WPA_NONCE_LEN, lo, hi);
	memcpy(d + 12, lo, WPA_NONCE_LEN);
	memcpy(d + 12 + WPA_NONCE_LEN, hi, WPA_NONCE_LEN);
	return wpa_prf_sha1(pmk, WPA_PMK_LEN, "Pairwise key expansion", d,
	    sizeof(d), ptk, WPA_PTK_LEN);
}

/* The PTK's parts (12.7.1.3). */
#define KCK(p)	((p))
#define KEK(p)	((p) + WPA_KCK_LEN)
#define TK(p)	((p) + WPA_KCK_LEN + WPA_KEK_LEN)

/* ---- RSN element ---- */

#define EID_RSN		0x30
#define EID_VENDOR	0xdd

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

static uint32_t
suite(const uint8_t *p)
{

	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	    (uint32_t)p[2] << 8 | p[3];
}

size_t
wpa_rsnie(uint8_t *buf, size_t cap, uint16_t caps)
{
	static const uint8_t ie[WPA_RSNIE_LEN - 2] = {
		0x30, 0x14,
		0x01, 0x00,			/* version 1 */
		0x00, 0x0f, 0xac, 0x04,		/* group: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, /* pairwise: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, /* AKM: PSK */
	};

	if (buf == NULL || cap < WPA_RSNIE_LEN)
		return 0;
	memcpy(buf, ie, sizeof(ie));
	buf[WPA_RSNIE_LEN - 2] = (uint8_t)caps;
	buf[WPA_RSNIE_LEN - 1] = (uint8_t)(caps >> 8);
	return WPA_RSNIE_LEN;
}

/* A suite list: count, then 4 bytes each; is want among them. */
static int
suites(const uint8_t **p, size_t *rem, uint32_t want, int *found)
{
	size_t n, i;

	if (*rem < 2)
		return WPA_EINVAL;
	n = le16(*p);
	if (n == 0 || (*rem - 2) / 4 < n)
		return WPA_EINVAL;
	*found = 0;
	for (i = 0; i < n; i++)
		if (suite(*p + 2 + 4 * i) == want)
			*found = 1;
	*p += 2 + 4 * n;
	*rem -= 2 + 4 * n;
	return 0;
}

int
wpa_rsn_parse(const uint8_t *ie, size_t len, struct wpa_rsn *r)
{
	const uint8_t *p;
	size_t rem;

	if (ie == NULL || r == NULL || len < 4 || len > WPA_IE_MAX ||
	    ie[0] != EID_RSN || ie[1] != len - 2)
		return WPA_EINVAL;
	memset(r, 0, sizeof(*r));
	r->version = le16(ie + 2);
	if (r->version != 1)
		return WPA_EINVAL;
	/* Defaults when a field is absent: CCMP, CCMP, 802.1X (AKM 1). */
	r->group = WPA_SUITE_CCMP;
	r->pairwise_ccmp = 1;
	r->akm_psk = 0;
	p = ie + 4;
	rem = len - 4;
	if (rem == 0)
		return 0;
	if (rem < 4)
		return WPA_EINVAL;
	r->group = suite(p);
	p += 4;
	rem -= 4;
	if (rem == 0)
		return 0;
	if (suites(&p, &rem, WPA_SUITE_CCMP, &r->pairwise_ccmp) != 0)
		return WPA_EINVAL;
	if (rem == 0)
		return 0;
	if (suites(&p, &rem, WPA_SUITE_PSK, &r->akm_psk) != 0)
		return WPA_EINVAL;
	if (rem == 0)
		return 0;
	if (rem < 2)
		return WPA_EINVAL;
	r->caps = le16(p);
	r->mfpr = (r->caps & WPA_CAP_MFPR) != 0;
	r->mfpc = (r->caps & WPA_CAP_MFPC) != 0;
	/* PMKID list and group management cipher: not ours to read. */
	return 0;
}

/* ---- the state ---- */

int
wpa_init(struct wpa_sm *sm, const uint8_t own[6], const uint8_t ap[6],
	const uint8_t pmk[WPA_PMK_LEN], const uint8_t *own_ie,
	size_t own_ie_len, const uint8_t *ap_ie, size_t ap_ie_len,
	wpa_random_fn rnd, void *rnd_arg)
{
	struct wpa_rsn r;

	if (sm == NULL || own == NULL || ap == NULL || pmk == NULL ||
	    rnd == NULL)
		return WPA_EINVAL;
	if (wpa_rsn_parse(own_ie, own_ie_len, &r) != 0 ||
	    r.group != WPA_SUITE_CCMP || !r.pairwise_ccmp || !r.akm_psk)
		return WPA_EINVAL;
	if (wpa_rsn_parse(ap_ie, ap_ie_len, &r) != 0)
		return WPA_EINVAL;
	if (r.group != WPA_SUITE_CCMP || !r.pairwise_ccmp || !r.akm_psk)
		return WPA_ENOTSUP;
	if (r.mfpr)
		return WPA_ENOTSUP;
	memset(sm, 0, sizeof(*sm));
	memcpy(sm->own, own, 6);
	memcpy(sm->ap, ap, 6);
	memcpy(sm->pmk, pmk, WPA_PMK_LEN);
	memcpy(sm->own_ie, own_ie, own_ie_len);
	sm->own_ie_len = own_ie_len;
	memcpy(sm->ap_ie, ap_ie, ap_ie_len);
	sm->ap_ie_len = ap_ie_len;
	sm->rnd = rnd;
	sm->rnd_arg = rnd_arg;
	sm->state = WPA_S_IDLE;
	return 0;
}

int
wpa_authorized(const struct wpa_sm *sm)
{

	return sm != NULL && sm->authorized;
}

void
wpa_clear(struct wpa_sm *sm)
{

	if (sm != NULL)
		OPENSSL_cleanse(sm, sizeof(*sm));
}

void
wpa_out_clear(struct wpa_out *out)
{

	if (out != NULL)
		OPENSSL_cleanse(out, sizeof(*out));
}

/* ---- EAPOL-Key frames (12.7.2) ---- */

#define EAPOL_KEY	3		/* 802.1X packet type */
#define DESC_RSN	2		/* descriptor type */

/* Offsets from the start of the 802.1X header. */
#define O_VER		0
#define O_TYPE		1
#define O_BODYLEN	2
#define O_DESC		4
#define O_INFO		5
#define O_KEYLEN	7
#define O_RC		9
#define O_NONCE		17
#define O_IV		49
#define O_RSC		65
#define O_MIC		81
#define O_DLEN		97
#define MIC_LEN		16

/* Key Information. */
#define KI_VER		0x0007
#define KI_VER_AES	2		/* HMAC-SHA1-128, AES key wrap */
#define KI_PAIRWISE	0x0008
#define KI_INSTALL	0x0040
#define KI_ACK		0x0080
#define KI_MIC		0x0100
#define KI_SECURE	0x0200
#define KI_ERROR	0x0400
#define KI_REQUEST	0x0800
#define KI_ENC		0x1000
#define KI_SMK		0x2000

struct key {
	const uint8_t *f;		/* the frame, 4 + body bytes */
	size_t len;
	unsigned ver;
	uint16_t info;
	uint16_t keylen;
	uint64_t rc;
	const uint8_t *nonce, *rsc, *mic, *data;
	size_t dlen;
};

static int
parse(const uint8_t *f, size_t len, struct key *k)
{
	size_t body;
	unsigned i;

	if (f == NULL || len < 4)
		return WPA_ESHORT;
	if (f[O_TYPE] != EAPOL_KEY || f[O_VER] < 1 || f[O_VER] > 3)
		return WPA_EFRAME;
	body = be16(f + O_BODYLEN);
	if (len - 4 < body || body < WPA_HDR_LEN - 4)
		return WPA_ESHORT;
	if (4 + body > WPA_RX_MAX)
		return WPA_EFRAME;
	if (f[O_DESC] != DESC_RSN)
		return WPA_EFRAME;
	k->f = f;
	k->len = 4 + body;
	k->ver = f[O_VER];
	k->info = be16(f + O_INFO);
	k->keylen = be16(f + O_KEYLEN);
	for (k->rc = 0, i = 0; i < 8; i++)
		k->rc = k->rc << 8 | f[O_RC + i];
	k->nonce = f + O_NONCE;
	k->rsc = f + O_RSC;
	k->mic = f + O_MIC;
	k->dlen = be16(f + O_DLEN);
	k->data = f + WPA_HDR_LEN;
	if (k->dlen != k->len - WPA_HDR_LEN)
		return WPA_ESHORT;
	return 0;
}

/* The MIC over the whole frame with the MIC field zero. */
static int
mic(const uint8_t *ptk, const uint8_t *f, size_t len, uint8_t m[MIC_LEN])
{
	static const uint8_t zero[MIC_LEN];
	uint8_t h[SHA1_LEN];
	struct piece v[3];
	int r;

	v[0].p = f;
	v[0].n = O_MIC;
	v[1].p = zero;
	v[1].n = MIC_LEN;
	v[2].p = f + O_MIC + MIC_LEN;
	v[2].n = len - O_MIC - MIC_LEN;
	if ((r = cr_hmac_sha1(KCK(ptk), WPA_KCK_LEN, v, 3, h)) != 0)
		return r;
	memcpy(m, h, MIC_LEN);
	OPENSSL_cleanse(h, sizeof(h));
	return 0;
}

static int
mic_ok(const uint8_t *ptk, const struct key *k)
{
	uint8_t m[MIC_LEN];
	int r;

	if ((r = mic(ptk, k->f, k->len, m)) != 0)
		return r;
	if (CRYPTO_memcmp(m, k->mic, MIC_LEN) != 0)
		return WPA_EMIC;
	return 0;
}

/* Our answer, MIC'd with the given PTK. */
static int
build(struct wpa_out *o, unsigned ver, uint16_t info, uint64_t rc,
	const uint8_t *nonce, const uint8_t *data, size_t dlen,
	const uint8_t *ptk)
{
	uint8_t *f = o->frame;
	size_t n = WPA_HDR_LEN + dlen;
	unsigned i;
	int r;

	if (n > sizeof(o->frame))
		return WPA_EINVAL;
	memset(f, 0, WPA_HDR_LEN);
	f[O_VER] = (uint8_t)ver;
	f[O_TYPE] = EAPOL_KEY;
	f[O_BODYLEN] = (uint8_t)((n - 4) >> 8);
	f[O_BODYLEN + 1] = (uint8_t)(n - 4);
	f[O_DESC] = DESC_RSN;
	f[O_INFO] = (uint8_t)(info >> 8);
	f[O_INFO + 1] = (uint8_t)info;
	for (i = 0; i < 8; i++)
		f[O_RC + i] = (uint8_t)(rc >> (56 - 8 * i));
	if (nonce != NULL)
		memcpy(f + O_NONCE, nonce, WPA_NONCE_LEN);
	f[O_DLEN] = (uint8_t)(dlen >> 8);
	f[O_DLEN + 1] = (uint8_t)dlen;
	if (dlen > 0)
		memcpy(f + WPA_HDR_LEN, data, dlen);
	if ((r = mic(ptk, f, n, f + O_MIC)) != 0)
		return r;
	o->len = n;
	return 0;
}

/* ---- key data (12.7.2, KDEs in Table 12-6) ---- */

#define KDE_GTK		1
#define GTK_CCMP_LEN	16

struct gtk {
	unsigned idx;
	int tx;
	size_t len;
	uint8_t key[WPA_GTK_MAX];
};

/*
 * The decrypted key data: elements, then perhaps padding (0xdd and
 * zeros to the end).  rsne: message 3, where the first RSNE must be the
 * beacon's byte for byte and a second may only name CCMP; the group
 * message carries none we read.  A GTK KDE is required, once.
 */
static int
keydata(const struct wpa_sm *sm, const uint8_t *d, size_t n, int rsne,
	struct gtk *g)
{
	struct wpa_rsn r;
	size_t off, l, i;
	int nie = 0, ngtk = 0;

	for (off = 0; off < n; off += 2 + l) {
		if (d[off] == EID_VENDOR && (n - off == 1 || d[off + 1] == 0)) {
			for (i = off + 1; i < n; i++)
				if (d[i] != 0)
					return WPA_EKDE;
			break;
		}
		if (n - off < 2)
			return WPA_EKDE;
		l = d[off + 1];
		if (l > n - off - 2)
			return WPA_EKDE;
		if (d[off] == EID_RSN && rsne) {
			if (nie == 0) {
				if (2 + l != sm->ap_ie_len ||
				    memcmp(d + off, sm->ap_ie, 2 + l) != 0)
					return WPA_EIE;
			} else if (nie == 1) {
				if (wpa_rsn_parse(d + off, 2 + l, &r) != 0 ||
				    !r.pairwise_ccmp)
					return WPA_EIE;
			} else
				return WPA_EIE;
			nie++;
		} else if (d[off] == EID_VENDOR && l >= 4 &&
		    d[off + 2] == 0x00 && d[off + 3] == 0x0f &&
		    d[off + 4] == 0xac && d[off + 5] == KDE_GTK) {
			/* OUI, type, key id byte, reserved, the GTK. */
			if (ngtk++ > 0 || l != 6 + GTK_CCMP_LEN)
				return WPA_EKDE;
			g->idx = d[off + 6] & 3;
			g->tx = (d[off + 6] >> 2) & 1;
			if (g->idx == 0)
				return WPA_EKDE;
			g->len = GTK_CCMP_LEN;
			memcpy(g->key, d + off + 8, GTK_CCMP_LEN);
		}
		/* Anything else (IGTK, vendor elements): not ours. */
	}
	if (rsne && nie == 0)
		return WPA_EIE;
	if (ngtk == 0)
		return WPA_EKDE;
	return 0;
}

static int
unwrap_keydata(const struct wpa_sm *sm, const uint8_t *ptk,
	const struct key *k, int rsne, struct gtk *g)
{
	uint8_t plain[WPA_RX_MAX];
	int r;

	if ((r = cr_unwrap(KEK(ptk), k->data, k->dlen, plain)) != 0)
		return r;
	r = keydata(sm, plain, k->dlen - 8, rsne, g);
	OPENSSL_cleanse(plain, k->dlen);
	return r;
}

/* The GTK goes out unless it is the one installed (12.7.7.2 note). */
static void
give_gtk(const struct wpa_sm *sm, const struct key *k, const struct gtk *g,
	struct wpa_out *o)
{

	if (sm->have_gtk && sm->gtk_idx == g->idx && sm->gtk_len == g->len &&
	    CRYPTO_memcmp(sm->gtk, g->key, g->len) == 0)
		return;
	o->set_gtk = 1;
	o->gtk_idx = g->idx;
	o->gtk_tx = g->tx;
	o->gtk_len = g->len;
	memcpy(o->gtk, g->key, g->len);
	memcpy(o->rsc, k->rsc, WPA_RSC_LEN);
}

static void
keep_gtk(struct wpa_sm *sm, const struct gtk *g)
{

	sm->have_gtk = 1;
	sm->gtk_idx = g->idx;
	sm->gtk_len = g->len;
	memcpy(sm->gtk, g->key, g->len);
}

static int
replayed(const struct wpa_sm *sm, const struct key *k)
{

	return sm->have_rc && k->rc <= sm->rc;
}

/* Message 1: ANonce in, message 2 out.  Not authenticated - no counter. */
static int
rx_m1(struct wpa_sm *sm, const struct key *k, struct wpa_out *o)
{
	uint8_t snonce[WPA_NONCE_LEN], tptk[WPA_PTK_LEN];
	int r;

	if ((k->info & (KI_INSTALL | KI_ENC)) != 0 ||
	    k->keylen != WPA_TK_LEN)
		return WPA_EBITS;
	if (replayed(sm, k))
		return WPA_EREPLAY;
	/* The AP repeating message 1 gets the same SNonce (12.7.6.2). */
	if (sm->state == WPA_S_M2 &&
	    memcmp(k->nonce, sm->anonce, WPA_NONCE_LEN) == 0)
		memcpy(snonce, sm->snonce, WPA_NONCE_LEN);
	else if (sm->rnd(sm->rnd_arg, snonce, WPA_NONCE_LEN) != 0)
		return WPA_ERANDOM;
	if ((r = wpa_ptk(sm->pmk, sm->ap, sm->own, k->nonce, snonce,
	    tptk)) != 0)
		goto out;
	if ((r = build(o, k->ver, KI_VER_AES | KI_PAIRWISE | KI_MIC, k->rc,
	    snonce, sm->own_ie, sm->own_ie_len, tptk)) != 0)
		goto out;
	memcpy(sm->anonce, k->nonce, WPA_NONCE_LEN);
	memcpy(sm->snonce, snonce, WPA_NONCE_LEN);
	memcpy(sm->tptk, tptk, WPA_PTK_LEN);
	sm->state = WPA_S_M2;
out:
	OPENSSL_cleanse(tptk, sizeof(tptk));
	return r;
}

/*
 * Message 3: the PTK confirmed, the GTK delivered, message 4 out.  In
 * WPA_S_DONE it is a retransmission (message 4 was lost): answered, the
 * keys not given again.
 */
static int
rx_m3(struct wpa_sm *sm, const struct key *k, struct wpa_out *o)
{
	const uint8_t *ptk;
	struct gtk g;
	int r, again;

	if ((k->info & (KI_INSTALL | KI_SECURE | KI_ENC)) !=
	    (KI_INSTALL | KI_SECURE | KI_ENC) || k->keylen != WPA_TK_LEN)
		return WPA_EBITS;
	if (sm->state == WPA_S_M2) {
		ptk = sm->tptk;
		again = 0;
	} else if (sm->state == WPA_S_DONE) {
		ptk = sm->ptk;
		again = 1;
	} else
		return WPA_ESTATE;
	if (replayed(sm, k))
		return WPA_EREPLAY;
	if (memcmp(k->nonce, sm->anonce, WPA_NONCE_LEN) != 0)
		return WPA_ENONCE;
	if ((r = mic_ok(ptk, k)) != 0)
		return r;
	if ((r = unwrap_keydata(sm, ptk, k, 1, &g)) != 0)
		goto out;
	if ((r = build(o, k->ver, KI_VER_AES | KI_PAIRWISE | KI_MIC |
	    KI_SECURE, k->rc, NULL, NULL, 0, ptk)) != 0)
		goto out;
	if (!again) {
		o->set_ptk = 1;
		memcpy(o->tk, TK(ptk), WPA_TK_LEN);
	}
	give_gtk(sm, k, &g, o);
	o->done = 1;
	sm->rc = k->rc;
	sm->have_rc = 1;
	if (!again) {
		memcpy(sm->ptk, sm->tptk, WPA_PTK_LEN);
		OPENSSL_cleanse(sm->tptk, WPA_PTK_LEN);
		sm->have_ptk = 1;
		sm->state = WPA_S_DONE;
		sm->authorized = 1;
	}
	keep_gtk(sm, &g);
out:
	OPENSSL_cleanse(&g, sizeof(g));
	return r;
}

/* Group message 1: a new GTK under the PTK in use, group message 2 out. */
static int
rx_g1(struct wpa_sm *sm, const struct key *k, struct wpa_out *o)
{
	struct gtk g;
	int r;

	if ((k->info & (KI_MIC | KI_SECURE | KI_ENC | KI_INSTALL)) !=
	    (KI_MIC | KI_SECURE | KI_ENC))
		return WPA_EBITS;
	if (!sm->have_ptk)
		return WPA_ESTATE;
	if (replayed(sm, k))
		return WPA_EREPLAY;
	if ((r = mic_ok(sm->ptk, k)) != 0)
		return r;
	if ((r = unwrap_keydata(sm, sm->ptk, k, 0, &g)) != 0)
		goto out;
	if ((r = build(o, k->ver, KI_VER_AES | KI_MIC | KI_SECURE, k->rc,
	    NULL, NULL, 0, sm->ptk)) != 0)
		goto out;
	give_gtk(sm, k, &g, o);
	o->done = 1;
	sm->rc = k->rc;
	sm->have_rc = 1;
	keep_gtk(sm, &g);
out:
	OPENSSL_cleanse(&g, sizeof(g));
	return r;
}

int
wpa_rx(struct wpa_sm *sm, const uint8_t *frame, size_t len,
	struct wpa_out *out)
{
	struct key k;
	int r;

	if (sm == NULL || out == NULL)
		return WPA_EINVAL;
	memset(out, 0, sizeof(*out));
	if ((r = parse(frame, len, &k)) != 0)
		return r;
	/* From the authenticator: Ack always; never Error, Request, SMK. */
	if ((k.info & KI_VER) != KI_VER_AES ||
	    (k.info & (KI_ERROR | KI_REQUEST | KI_SMK)) != 0 ||
	    (k.info & KI_ACK) == 0)
		return WPA_EBITS;
	if ((k.info & KI_PAIRWISE) == 0)
		r = rx_g1(sm, &k, out);
	else if ((k.info & KI_MIC) == 0)
		r = rx_m1(sm, &k, out);
	else
		r = rx_m3(sm, &k, out);
	if (r != 0)
		wpa_out_clear(out);
	return r;
}

const char *
wpa_strerror(int err)
{

	switch (err) {
	case 0:			return "ok";
	case WPA_EINVAL:	return "invalid argument";
	case WPA_ENOTSUP:	return "AP requires an unsupported mode";
	case WPA_ESHORT:	return "frame truncated";
	case WPA_EFRAME:	return "not an RSN EAPOL-Key frame";
	case WPA_EBITS:		return "unexpected key information";
	case WPA_ESTATE:	return "message not expected now";
	case WPA_EREPLAY:	return "replay counter not increasing";
	case WPA_EMIC:		return "MIC mismatch";
	case WPA_ENONCE:	return "ANonce changed";
	case WPA_EIE:		return "RSN element mismatch";
	case WPA_EKDE:		return "bad key data";
	case WPA_ECRYPTO:	return "crypto failure";
	case WPA_ERANDOM:	return "no random bytes";
	default:		return "unknown error";
	}
}
