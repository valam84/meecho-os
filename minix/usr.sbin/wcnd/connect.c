/*
 * wcnd: joining a WPA2-PSK (or open) network - what mac80211 and
 * wpa_supplicant do for ath10k, in the order port/clover/wifi/sta.md
 * section 3 gives: the vdev started on the AP's channel, the AP's peer,
 * Authentication and Association (frames by reference through
 * MGMT_TX_SEND), PEER_ASSOC and VDEV_UP, the 4-way handshake over the
 * service's EAPOL path, the keys, the port authorized.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "wcn_ioc.h"
#include "wcn_wmi.h"
#include "wcn_mlme.h"
#include "wcn_wpa.h"
#include "wcnd.h"

#define VDEV	0

static struct wcn_sta sta;
static struct wpa_sm sm;
static uint8_t out3[2048];

static struct wmi_buf *
wb(void)
{
	static struct wmi_buf b;

	b.p = out3;
	b.cap = sizeof(out3);
	b.len = 0;
	b.err = 0;
	return &b;
}

static long
ms_since(const struct timespec *t0)
{
	struct timespec n;

	clock_gettime(CLOCK_MONOTONIC, &n);
	return (n.tv_sec - t0->tv_sec) * 1000 + (n.tv_nsec - t0->tv_nsec) /
	    1000000;
}

/* Wait up to ms for the WMI event "id"; MGMT_RX frames other than the
 * one wanted are passed by.  0 and *ev (in *msg), or -1. */
static int
wait_event(uint32_t id, struct wcn_msg *msg, struct wmi_ev *ev, long ms)
{
	struct timespec t0;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (ms_since(&t0) < ms) {
		if (wmi_wait(msg, 200) != 0 ||
		    wmi_event(msg->data, msg->len, ev) != 0)
			continue;
		if (ev->id == id)
			return 0;
	}
	return -1;
}

/* A management frame sent by reference; the completion is not waited
 * for - the answer is. */
static int
send_mgmt(const uint8_t *f, size_t len, unsigned desc)
{
	struct wcn_msg m;

	memset(&m, 0, sizeof(m));
	m.len = len;
	memcpy(m.data, f, len);
	if (ioctl(dev, WCNIOC_FRAME, &m) != 0) {
		say(LOG_ERR, "frame: %s", strerror(errno));
		return -1;
	}
	return wmi_send(out3, wmi_mgmt_tx(wb(), VDEV, desc, 0, m.da, f, len),
	    "MGMT_TX_SEND");
}

/* The next MGMT_RX of the given subtype from the BSS: 0 and the frame. */
static int
wait_mgmt(uint8_t subtype, struct wcn_msg *msg, struct wmi_mgmt_rx *mr,
	long ms)
{
	struct timespec t0;
	struct wmi_ev ev;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (ms_since(&t0) < ms) {
		if (wmi_wait(msg, 200) != 0 ||
		    wmi_event(msg->data, msg->len, &ev) != 0 ||
		    ev.id != WMI_EV_MGMT_RX || wmi_mgmt_rx(&ev, mr) != 0 ||
		    mr->len < 24)
			continue;
		if ((mr->frame[0] & 0xfc) == subtype &&
		    memcmp(mr->frame + 16, sta.bssid, 6) == 0)
			return 0;
	}
	return -1;
}

static int
rnd(void *arg, uint8_t *buf, size_t n)
{
	int fd;
	ssize_t r;

	(void)arg;
	if ((fd = open("/dev/random", O_RDONLY)) < 0)
		return -1;
	r = read(fd, buf, n);
	close(fd);
	return r == (ssize_t)n ? 0 : -1;
}

/* The RSN element of the BSS, whole (id, length, body). */
static const uint8_t *
bss_rsn(const struct wcn_bss *b, size_t *len)
{
	size_t at = 0;

	while (at + 2 <= b->ies_len && at + 2 + b->ies[at + 1] <= b->ies_len) {
		if (b->ies[at] == 48) {
			*len = 2 + b->ies[at + 1];
			return b->ies + at;
		}
		at += 2 + b->ies[at + 1];
	}
	return NULL;
}

static int
install(const uint8_t *key, unsigned len, unsigned idx, int group)
{
	struct wmi_key k;
	struct wcn_msg msg;
	struct wmi_ev ev;

	memset(&k, 0, sizeof(k));
	k.vdev_id = VDEV;
	memcpy(k.mac, sta.bssid, 6);
	k.idx = idx;
	k.flags = group ? WMI_KEY_GROUP : WMI_KEY_PAIRWISE;
	k.cipher = WMI_CIPHER_AES_CCM;
	k.key = key;
	k.key_len = len;
	if (wmi_send(out3, wmi_vdev_install_key(wb(), &k), "INSTALL_KEY") != 0)
		return -1;
	if (wait_event(WMI_EV_VDEV_INSTALL_KEY_DONE, &msg, &ev, 3000) != 0)
		say(LOG_WARNING, "no INSTALL_KEY_COMPLETE (%s key %u)",
		    group ? "group" : "pairwise", idx);
	return 0;
}

static int
link_set(int up, int authorized, unsigned freq)
{
	struct wcn_link l;

	memset(&l, 0, sizeof(l));
	l.up = up;
	l.authorized = authorized;
	l.vdev = VDEV;
	l.freq = freq;
	memcpy(l.bssid, sta.bssid, 6);
	if (ioctl(dev, WCNIOC_LINK, &l) != 0) {
		say(LOG_ERR, "link: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int
join(const struct wcn_bss_table *t, const char *ssid, const char *pass,
	const struct wmi_svc_ready *svr, const uint8_t self[6])
{
	const struct wcn_bss *b;
	struct wmi_vdev_start vs;
	struct wmi_vdev_start_resp vr;
	struct wmi_peer_assoc pa;
	struct wcn_assoc_opts ao;
	struct wcn_auth_resp ar;
	struct wcn_assoc_resp as;
	struct wmi_mgmt_rx mr;
	struct wcn_msg msg;
	struct wmi_ev ev;
	struct wpa_out wo;
	uint8_t frame[512], rsn[64], pmk[WPA_PMK_LEN], rates[16], prates[16];
	uint8_t mcs[8], eth[WCN_MSG_MAX];
	const uint8_t *aprsn = NULL;
	size_t n, rsnl = 0, aprsnl = 0;
	unsigned freq, center, width, i, nr, try;
	int secure, r;
	struct timespec t0;

	b = wcn_bss_best(t, (const uint8_t *)ssid, strlen(ssid),
	    WCN_BIT(WCN_SEC_OPEN) | WCN_BIT(WCN_SEC_WPA2_PSK) |
	    WCN_BIT(WCN_SEC_WPA2_WPA3));
	if (b == NULL) {
		say(LOG_ERR, "no usable BSS of that SSID in the scan");
		return -1;
	}
	secure = b->sec != WCN_SEC_OPEN;
	memcpy(sta.self, self, 6);
	memcpy(sta.bssid, b->bssid, 6);
	sta.seq = 0;
	wcn_bss_chandef(b, 0, &freq, &center, &width);
	say(LOG_INFO, "joining %02x:%02x:%02x:%02x:%02x:%02x on %u MHz, %d dBm,"
	    " %s", b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3],
	    b->bssid[4], b->bssid[5], freq, b->rssi, secure ? "WPA2" : "open");

	/* The interface's settings (sta.md 1.3). */
	if (wmi_send(out3, wmi_vdev_set_param(wb(), VDEV,
	    WMI_VDEV_TX_ENCAP_TYPE, WMI_TXRX_NATIVE_WIFI), "ENCAP") != 0 ||
	    wmi_send(out3, wmi_sta_ps_param(wb(), VDEV,
	    WMI_STA_PS_RX_WAKE_POLICY, WMI_STA_PS_RX_WAKE), "PS") != 0 ||
	    wmi_send(out3, wmi_sta_ps_param(wb(), VDEV,
	    WMI_STA_PS_TX_WAKE_THRESHOLD, WMI_STA_PS_TX_WAKE_ALWAYS), "PS") !=
	    0 || wmi_send(out3, wmi_sta_ps_param(wb(), VDEV,
	    WMI_STA_PS_PSPOLL_COUNT, WMI_STA_PS_PSPOLL_NO_MAX), "PS") != 0)
		return -1;

	/* The vdev on the AP's channel, HT20. */
	memset(&vs, 0, sizeof(vs));
	vs.vdev_id = VDEV;
	vs.bcn_intval = b->bintval ? b->bintval : 100;
	vs.dtim_period = 1;
	vs.chan.freq = freq;
	vs.chan.center1 = center;
	vs.chan.mode = b->band == WCN_BAND_2G ? WMI_MODE_11NG_HT20 :
	    WMI_MODE_11NA_HT20;
	vs.chan.flags = WMI_CHAN_ALLOW_HT;
	vs.chan.max_power = vs.chan.reg_power = 40;
	if (wmi_send(out3, wmi_vdev_start(wb(), &vs), "VDEV_START") != 0)
		return -1;
	if (wait_event(WMI_EV_VDEV_START_RESP, &msg, &ev, 5000) != 0 ||
	    wmi_vdev_start_resp(&ev, &vr) != 0 || vr.status != 0) {
		say(LOG_ERR, "vdev did not start");
		return -1;
	}
	(void)wmi_send(out3, wmi_sta_ps_mode(wb(), VDEV,
	    WMI_STA_PS_MODE_DISABLED), "PS_MODE");
	if (wmi_send(out3, wmi_peer_create(wb(), VDEV, sta.bssid, 0),
	    "PEER_CREATE") != 0)
		return -1;
	usleep(100000);		/* HTT PEER_MAP comes to the service */

	/* Authentication, open system. */
	for (try = 0; try < 3; try++) {
		if ((n = wcn_build_auth(&sta, frame, sizeof(frame))) == 0 ||
		    send_mgmt(frame, n, 0) != 0)
			return -1;
		if (wait_mgmt(0xb0, &msg, &mr, 1000) == 0 &&
		    wcn_parse_auth(&sta, mr.frame, mr.len, &ar) == 0)
			break;
	}
	if (try == 3) {
		say(LOG_ERR, "no answer to Authentication");
		return -1;
	}
	if (ar.status != 0) {
		say(LOG_ERR, "Authentication refused: status %u", ar.status);
		return -1;
	}
	say(LOG_INFO, "authenticated");

	/* Association, with our RSN element. */
	if (secure) {
		rsnl = wpa_rsnie(rsn, sizeof(rsn), 0);
		if ((aprsn = bss_rsn(b, &aprsnl)) == NULL || rsnl == 0) {
			say(LOG_ERR, "no RSN element");
			return -1;
		}
	}
	memset(&ao, 0, sizeof(ao));
	ao.rsn_ie = secure ? rsn : NULL;
	ao.rsn_len = rsnl;
	ao.ht_capinfo = wcn_ht_capinfo(svr->ht_cap_info, svr->vht_cap_info);
	ao.nss = 1;
	for (try = 0; try < 3; try++) {
		if ((n = wcn_build_assoc(&sta, b, &ao, frame,
		    sizeof(frame))) == 0 || send_mgmt(frame, n, 1) != 0)
			return -1;
		if (wait_mgmt(0x10, &msg, &mr, 1000) == 0 &&
		    wcn_parse_assoc(&sta, mr.frame, mr.len, &as) == 0)
			break;
	}
	if (try == 3) {
		say(LOG_ERR, "no answer to Association");
		return -1;
	}
	if (as.status != 0) {
		say(LOG_ERR, "Association refused: status %u", as.status);
		return -1;
	}
	say(LOG_INFO, "associated, aid %u", as.aid);

	/* PEER_ASSOC and VDEV_UP (sta.md 3.5). */
	memset(&pa, 0, sizeof(pa));
	memcpy(pa.mac, sta.bssid, 6);
	pa.vdev_id = VDEV;
	pa.aid = as.aid;
	pa.flags = WMI_PEER_F_AUTH | (secure ? WMI_PEER_F_NEED_PTK_4_WAY : 0) |
	    (as.wmm || b->wmm ? WMI_PEER_F_QOS : 0);
	pa.caps = as.capab;
	pa.listen_intval = 1;
	pa.nss = 1;
	if ((r = wcn_rates_join(b, b->ht, rates, sizeof(rates))) > 0) {
		nr = (unsigned)r;
		wcn_rates_peer(rates, nr, prates);
		pa.legacy = prates;
		pa.nlegacy = nr;
	}
	if (as.ht || b->ht) {
		pa.flags |= WMI_PEER_F_HT;
		pa.rate_caps = WMI_RC_HT;
		pa.ht_caps = as.ht ? as.ht_capinfo : b->ht_capinfo;
		pa.max_mpdu = (1u << (13 + ((as.ht ? as.ht_ampdu :
		    b->ht_ampdu) & 3))) - 1;
		pa.mpdu_density = 0;
		for (i = 0; i < 8; i++)
			mcs[i] = i;
		pa.ht = mcs;
		pa.nht = 8;
		pa.phy_mode = b->band == WCN_BAND_2G ? WMI_MODE_11NG_HT20 :
		    WMI_MODE_11NA_HT20;
	} else
		pa.phy_mode = b->band == WCN_BAND_2G ? WMI_MODE_11G :
		    WMI_MODE_11A;
	if (wmi_send(out3, wmi_peer_assoc(wb(), &pa), "PEER_ASSOC") != 0 ||
	    wmi_send(out3, wmi_vdev_up(wb(), VDEV, as.aid, sta.bssid),
	    "VDEV_UP") != 0)
		return -1;
	(void)wmi_send(out3, wmi_peer_set_param(wb(), VDEV, sta.bssid,
	    WMI_PEER_DUMMY_VAR, 1), "DUMMY_VAR");
	if (link_set(1, !secure, freq) != 0)
		return -1;
	if (!secure) {
		say(LOG_INFO, "open network: the port is open");
		return 0;
	}

	/* The 4-way handshake. */
	if (wpa_pmk((const uint8_t *)ssid, strlen(ssid), pass, pmk) != 0) {
		say(LOG_ERR, "the passphrase is not 8..63 characters or a PSK");
		return -1;
	}
	if ((r = wpa_init(&sm, self, sta.bssid, pmk, rsn, rsnl, aprsn, aprsnl,
	    rnd, NULL)) != 0) {
		say(LOG_ERR, "WPA: %s", wpa_strerror(r));
		return -1;
	}
	memset(pmk, 0, sizeof(pmk));
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (!wpa_authorized(&sm) && ms_since(&t0) < 10000) {
		memset(&msg, 0, sizeof(msg));
		msg.ms = 500;
		if (ioctl(dev, WCNIOC_EAPOL_RX, &msg) != 0 || msg.len < 14)
			continue;
		wpa_out_clear(&wo);
		if ((r = wpa_rx(&sm, msg.data + 14, msg.len - 14, &wo)) != 0) {
			say(LOG_WARNING, "EAPOL: %s", wpa_strerror(r));
			continue;
		}
		if (wo.len > 0) {
			/* Ethernet: to the AP, from us, 0x888e. */
			memcpy(eth, sta.bssid, 6);
			memcpy(eth + 6, self, 6);
			eth[12] = 0x88;
			eth[13] = 0x8e;
			memcpy(eth + 14, wo.frame, wo.len);
			memset(&msg, 0, sizeof(msg));
			msg.len = 14 + wo.len;
			memcpy(msg.data, eth, msg.len);
			if (ioctl(dev, WCNIOC_EAPOL_TX, &msg) != 0)
				say(LOG_ERR, "EAPOL out: %s", strerror(errno));
		}
		if (wo.set_ptk)
			(void)install(wo.tk, sizeof(wo.tk), 0, 0);
		if (wo.set_gtk)
			(void)install(wo.gtk, wo.gtk_len, wo.gtk_idx, 1);
	}
	if (!wpa_authorized(&sm)) {
		say(LOG_ERR, "no 4-way handshake in 10 s (wrong passphrase?)");
		return -1;
	}
	(void)wmi_send(out3, wmi_peer_set_param(wb(), VDEV, sta.bssid,
	    WMI_PEER_AUTHORIZE, 1), "AUTHORIZE");
	if (link_set(1, 1, freq) != 0)
		return -1;
	say(LOG_INFO, "WPA2 keys in, the port is open");
	return 0;
}

/* The group key handshake when the AP renews its key; the rest of the
 * time nothing.  Runs until the link is lost. */
void
keep(void)
{
	struct wcn_msg msg;
	struct wpa_out wo;
	uint8_t eth[WCN_MSG_MAX];
	int r;

	for (;;) {
		memset(&msg, 0, sizeof(msg));
		msg.ms = 5000;
		if (ioctl(dev, WCNIOC_EAPOL_RX, &msg) != 0 || msg.len < 14)
			continue;
		wpa_out_clear(&wo);
		if ((r = wpa_rx(&sm, msg.data + 14, msg.len - 14, &wo)) != 0)
			continue;
		if (wo.len > 0) {
			memcpy(eth, sta.bssid, 6);
			memcpy(eth + 6, sta.self, 6);
			eth[12] = 0x88;
			eth[13] = 0x8e;
			memcpy(eth + 14, wo.frame, wo.len);
			memset(&msg, 0, sizeof(msg));
			msg.len = 14 + wo.len;
			memcpy(msg.data, eth, msg.len);
			(void)ioctl(dev, WCNIOC_EAPOL_TX, &msg);
		}
		if (wo.set_ptk)
			(void)install(wo.tk, sizeof(wo.tk), 0, 0);
		if (wo.set_gtk)
			(void)install(wo.gtk, wo.gtk_len, wo.gtk_idx, 1);
	}
}
