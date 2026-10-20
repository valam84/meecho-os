#ifndef _WCN_WPA_H
#define _WCN_WPA_H

/*
 * The station's side of WPA2-Personal (IEEE 802.11-2016, 12.7): the PSK
 * from a passphrase, the RSN element of the Association Request, the
 * 4-way handshake and the group key handshake.  Only what this machine
 * needs: AKM PSK (00-0F-AC:2), CCMP for both ciphers (00-0F-AC:4), key
 * descriptor version 2 (HMAC-SHA1-128 MIC, AES key wrap).  No TKIP, no
 * SAE, no PMF (an AP that requires it is refused), no 802.1X.
 *
 * No MINIX and no transport here, as wcn_wlfw.c: an EAPOL frame comes in
 * as bytes, the answer goes out as bytes, keys come out as bytes.  The
 * firmware does CCMP itself; installing the keys is the caller's.
 * libcrypto (OpenSSL 3) does the arithmetic.  Host stand: port/test/wcn
 * (run-wpa.sh).
 *
 * An EAPOL frame here is what follows the Ethernet type 0x888e: the
 * 802.1X header (version, type, body length) and the body.  Bytes after
 * the body (Ethernet padding) are ignored.
 */

#include <stddef.h>
#include <stdint.h>

#define WPA_PMK_LEN	32
#define WPA_NONCE_LEN	32
#define WPA_KCK_LEN	16
#define WPA_KEK_LEN	16
#define WPA_TK_LEN	16		/* CCMP */
#define WPA_PTK_LEN	48		/* KCK | KEK | TK */
#define WPA_GTK_MAX	32
#define WPA_RSC_LEN	8
#define WPA_IE_MAX	257		/* an element: id, length, 255 */
#define WPA_RSNIE_LEN	22		/* the station's own, wpa_rsnie() */

/* The EAPOL-Key frame (802.1X header included). */
#define WPA_HDR_LEN	99		/* everything before the key data */
#define WPA_FRAME_MAX	(WPA_HDR_LEN + WPA_IE_MAX)	/* message 2 */
#define WPA_RX_MAX	2048		/* longer frames are refused */

/* Results: 0 or one of these (negative). */
#define WPA_EINVAL	-1	/* bad argument (passphrase, SSID, element) */
#define WPA_ENOTSUP	-2	/* AP wants what is not here (PMF, TKIP...) */
#define WPA_ESHORT	-3	/* frame truncated or lengths disagree */
#define WPA_EFRAME	-4	/* not an RSN EAPOL-Key frame */
#define WPA_EBITS	-5	/* key information wrong for any message */
#define WPA_ESTATE	-6	/* a message not expected now */
#define WPA_EREPLAY	-7	/* replay counter not above the last */
#define WPA_EMIC	-8	/* MIC does not verify */
#define WPA_ENONCE	-9	/* ANonce of message 3 is not message 1's */
#define WPA_EIE		-10	/* RSNE of message 3 is not the beacon's */
#define WPA_EKDE	-11	/* key data: unwrap failed, GTK missing... */
#define WPA_ECRYPTO	-12	/* libcrypto failed */
#define WPA_ERANDOM	-13	/* the random callback failed */

const char *wpa_strerror(int err);

/*
 * The PMK of WPA2-Personal (12.7.1.3, J.4): PBKDF2-HMAC-SHA1, 4096
 * rounds, the SSID as salt, 32 bytes.  The passphrase is 8..63 printable
 * ASCII characters; 64 hexadecimal digits are the PSK itself.  SSID
 * 1..32 bytes (not needed, may be NULL, for the hex form).
 */
int wpa_pmk(const uint8_t *ssid, size_t ssid_len, const char *passphrase,
	uint8_t pmk[WPA_PMK_LEN]);

/* PRF-n of 12.7.1.2 with HMAC-SHA1; n = 8 * outlen bits. */
int wpa_prf_sha1(const uint8_t *key, size_t klen, const char *label,
	const uint8_t *data, size_t dlen, uint8_t *out, size_t outlen);

/* PTK = PRF-384(PMK, "Pairwise key expansion", min/max of MACs, nonces). */
int wpa_ptk(const uint8_t pmk[WPA_PMK_LEN], const uint8_t aa[6],
	const uint8_t spa[6], const uint8_t anonce[WPA_NONCE_LEN],
	const uint8_t snonce[WPA_NONCE_LEN], uint8_t ptk[WPA_PTK_LEN]);

/*
 * The station's RSN element (9.4.2.25): version 1, group CCMP, pairwise
 * CCMP, AKM PSK, the given capabilities (0 for no PMF; the PTKSA replay
 * counter bits are the caller's).  WPA_RSNIE_LEN bytes, or 0 if cap is
 * too small.  The same bytes go to the Association Request and to
 * wpa_init() - message 2 repeats them.
 */
size_t wpa_rsnie(uint8_t *buf, size_t cap, uint16_t caps);

/* An RSN element taken apart, with the defaults of 9.4.2.25.1. */
#define WPA_CAP_MFPR	0x0040	/* management frame protection required */
#define WPA_CAP_MFPC	0x0080	/* ... capable */
struct wpa_rsn {
	uint16_t version;
	uint32_t group;			/* suite as OUI << 8 | type */
	int pairwise_ccmp;		/* CCMP among the pairwise suites */
	int akm_psk;			/* PSK among the AKM suites */
	uint16_t caps;
	int mfpr, mfpc;			/* from caps */
};
#define WPA_SUITE_CCMP	0x000fac04
#define WPA_SUITE_PSK	0x000fac02

int wpa_rsn_parse(const uint8_t *ie, size_t len, struct wpa_rsn *r);

/* The SNonce's source: fill buf with n random bytes, 0 or -1. */
typedef int (*wpa_random_fn)(void *arg, uint8_t *buf, size_t n);

/* Handshake state. */
#define WPA_S_IDLE	0	/* waiting for message 1 */
#define WPA_S_M2	1	/* message 2 sent, waiting for message 3 */
#define WPA_S_DONE	2	/* PTK in place */

/*
 * The state: a plain structure, so a caller may copy it (the stand
 * does); it holds the PMK and keys - wpa_clear() it when done.
 */
struct wpa_sm {
	uint8_t own[6], ap[6];
	uint8_t pmk[WPA_PMK_LEN];
	uint8_t own_ie[WPA_IE_MAX], ap_ie[WPA_IE_MAX];
	size_t own_ie_len, ap_ie_len;
	wpa_random_fn rnd;
	void *rnd_arg;
	int state;
	int authorized;			/* a 4-way handshake has completed */
	int have_rc;
	uint64_t rc;			/* last replay counter of a valid frame */
	uint8_t anonce[WPA_NONCE_LEN], snonce[WPA_NONCE_LEN];
	uint8_t tptk[WPA_PTK_LEN];	/* the handshake's, before message 3 */
	int have_ptk;
	uint8_t ptk[WPA_PTK_LEN];	/* in use */
	int have_gtk;
	unsigned gtk_idx;
	size_t gtk_len;
	uint8_t gtk[WPA_GTK_MAX];	/* last installed */
};

/*
 * own and ap: the MAC addresses; own_ie: what wpa_rsnie() gave (or the
 * caller's own, if it is CCMP/CCMP/PSK); ap_ie: the RSN element of the
 * AP's beacon or probe response, byte for byte - message 3 is compared
 * with it.  WPA_ENOTSUP if the AP does not offer CCMP/CCMP/PSK or
 * requires PMF.
 */
int wpa_init(struct wpa_sm *sm, const uint8_t own[6], const uint8_t ap[6],
	const uint8_t pmk[WPA_PMK_LEN], const uint8_t *own_ie,
	size_t own_ie_len, const uint8_t *ap_ie, size_t ap_ie_len,
	wpa_random_fn rnd, void *rnd_arg);

/*
 * What a received frame gives.  Order matters for the caller: send the
 * frame first, then install the keys (message 4 goes out unencrypted;
 * with the TK installed first the firmware would encrypt it).  A key is
 * given once: a retransmitted message 3 or group message 1 with the key
 * already installed gets its answer again, but no key (installing the
 * same key twice would reset its packet number).
 */
struct wpa_out {
	size_t len;			/* frame to send, 0 if none */
	uint8_t frame[WPA_FRAME_MAX];
	int set_ptk;			/* install tk for the AP's address */
	uint8_t tk[WPA_TK_LEN];
	int set_gtk;			/* install gtk at gtk_idx */
	unsigned gtk_idx;
	int gtk_tx;
	size_t gtk_len;
	uint8_t gtk[WPA_GTK_MAX];
	uint8_t rsc[WPA_RSC_LEN];	/* receive sequence counter, as sent */
	int done;			/* this frame completed a handshake */
};

/*
 * One received EAPOL frame.  0 and *out, or an error, and then sm is
 * exactly as before and out has nothing to send or install.
 */
int wpa_rx(struct wpa_sm *sm, const uint8_t *frame, size_t len,
	struct wpa_out *out);

/* The 802.1X port may carry data: a 4-way handshake has completed. */
int wpa_authorized(const struct wpa_sm *sm);

/* Forget the keys. */
void wpa_clear(struct wpa_sm *sm);
void wpa_out_clear(struct wpa_out *out);

#endif /* _WCN_WPA_H */
