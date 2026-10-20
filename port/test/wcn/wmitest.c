/*
 * Host stand for minix/drivers/net/wcn/wcn_wmi.c.
 *
 * Commands: every builder against the bytes ath10k lays out for the same
 * arguments - derived outside the tree by filling Linux's own structure
 * definitions (the compiler placing every field) and kept here as bytes
 * only; then every builder with every buffer too short (exact-size heap
 * buffers, so the sanitizer sees a write past the end) and with the
 * arguments it must refuse.
 *
 * Events: made up here, well formed and not - cut at every byte, a TLV
 * running past the end, structures shorter than their size - and
 * SERVICE_READY's bitmap, built from the word masks of htc-wmi.md, 5.1,
 * checked service by service against what Linux reported on the tablet
 * (pmos-20261019/pass1/wifi/debugfs/wmi_services.txt, the argument).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_wmi.h"

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

/* ---- the expected commands (bytes; see the head of the file) ---- */

static const char X_init0[] =	/* 220 bytes */
    "01 00 00 00 1c 00 4a 00 00 00 00 01 35 00 00 00 "
    "51 43 41 5f 4d 4c 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 b0 00 4b 00 04 00 00 00 21 00 00 00 "
    "04 00 00 00 04 00 00 00 02 00 00 00 42 00 00 00 "
    "10 00 00 00 07 00 00 00 07 00 00 00 64 00 00 00 "
    "64 00 00 00 64 00 00 00 28 00 00 00 01 00 00 00 "
    "04 00 00 00 04 00 00 00 04 00 00 00 08 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 "
    "02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 02 00 00 00 20 04 00 00 02 00 00 00 "
    "01 00 00 00 20 00 00 00 02 00 00 00 05 00 00 00 "
    "16 00 00 00 06 00 00 00 00 00 00 00 01 00 00 00 "
    "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 02 00 00 00 00 12 00 "
;
static const char X_init2ext[] =	/* 260 bytes */
    "01 00 00 00 1c 00 4a 00 00 00 00 01 35 00 00 00 "
    "51 43 41 5f 4d 4c 00 00 00 00 00 00 00 00 00 00 "
    "02 00 00 00 b0 00 4b 00 04 00 00 00 21 00 00 00 "
    "04 00 00 00 04 00 00 00 02 00 00 00 42 00 00 00 "
    "10 00 00 00 07 00 00 00 07 00 00 00 64 00 00 00 "
    "64 00 00 00 64 00 00 00 28 00 00 00 01 00 00 00 "
    "04 00 00 00 04 00 00 00 04 00 00 00 08 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 "
    "02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 02 00 00 00 20 04 00 00 02 00 00 00 "
    "01 00 00 00 20 00 00 00 02 00 00 00 05 00 00 00 "
    "16 00 00 00 06 00 00 00 00 00 00 00 01 00 00 00 "
    "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 02 04 00 28 00 12 00 10 00 4c 00 "
    "08 00 00 00 00 60 45 23 00 00 04 00 01 00 00 00 "
    "10 00 4c 00 03 00 00 00 00 00 bc 9a 00 10 00 00 "
    "00 00 00 00 "
;
static const char X_init_noreorder[] =	/* 220 bytes */
    "01 00 00 00 1c 00 4a 00 00 00 00 01 35 00 00 00 "
    "51 43 41 5f 4d 4c 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 b0 00 4b 00 04 00 00 00 21 00 00 00 "
    "00 00 00 00 00 00 00 00 02 00 00 00 42 00 00 00 "
    "10 00 00 00 07 00 00 00 07 00 00 00 64 00 00 00 "
    "64 00 00 00 64 00 00 00 28 00 00 00 01 00 00 00 "
    "04 00 00 00 04 00 00 00 04 00 00 00 08 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 04 00 00 "
    "02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 02 00 00 00 20 04 00 00 02 00 00 00 "
    "01 00 00 00 20 00 00 00 02 00 00 00 05 00 00 00 "
    "16 00 00 00 06 00 00 00 00 00 00 00 01 00 00 00 "
    "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 02 00 00 00 00 12 00 "
;
static const char X_pdev_param[] =	/* 20 bytes */
    "03 40 00 00 0c 00 52 00 00 00 00 00 22 00 00 00 "
    "01 00 00 00 "
;
static const char X_regdomain[] =	/* 32 bytes */
    "01 40 00 00 18 00 51 00 00 00 00 00 37 00 00 00 "
    "37 00 00 00 37 00 00 00 10 00 00 00 30 00 00 00 "
;
static const char X_chanlist[] =	/* 100 bytes */
    "03 30 00 00 04 00 4f 00 03 00 00 00 54 00 12 00 "
    "18 00 50 00 6c 09 00 00 6c 09 00 00 00 00 00 00 "
    "41 19 00 00 00 28 28 00 00 28 00 00 18 00 50 00 "
    "a8 09 00 00 a8 09 00 00 00 00 00 00 81 18 00 00 "
    "00 28 28 00 00 28 00 00 18 00 50 00 8c 14 00 00 "
    "8c 14 00 00 00 00 00 00 c0 1c 00 00 00 2e 2e 00 "
    "03 2e 00 00 "
;
static const char X_startscan[] =	/* 188 bytes */
    "01 30 00 00 64 00 4d 00 00 a0 00 00 01 a0 00 00 "
    "00 00 00 00 01 00 00 00 1f 01 00 00 32 00 00 00 "
    "96 00 00 00 32 00 00 00 f4 01 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 20 4e 00 00 05 00 00 00 "
    "30 00 00 00 00 00 00 00 03 00 00 00 01 00 00 00 "
    "01 00 00 00 06 00 00 00 03 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 0c 00 10 00 "
    "6c 09 00 00 85 09 00 00 3c 14 00 00 24 00 13 00 "
    "06 00 00 00 6d 65 65 63 68 6f 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 08 00 13 00 ff ff ff ff ff ff 00 00 "
    "08 00 11 00 01 04 02 04 0b 16 00 00 "
;
static const char X_startscan_passive[] =	/* 140 bytes */
    "01 30 00 00 64 00 4d 00 05 a0 00 00 02 a0 00 00 "
    "01 00 00 00 01 00 00 00 1f 01 00 00 32 00 00 00 "
    "96 00 00 00 32 00 00 00 f4 01 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 20 4e 00 00 05 00 00 00 "
    "11 10 00 00 00 00 00 00 00 00 00 00 02 00 00 00 "
    "00 00 00 00 00 00 00 00 03 00 00 00 02 00 00 12 "
    "34 56 00 00 ff ff ff 00 00 00 00 00 00 00 10 00 "
    "00 00 13 00 10 00 13 00 ff ff ff ff ff ff 00 00 "
    "aa bb cc dd ee 01 00 00 00 00 11 00 "
;
static const char X_stopscan[] =	/* 24 bytes */
    "02 30 00 00 10 00 4e 00 01 a0 00 00 00 a0 00 00 "
    "00 00 00 00 00 00 00 00 "
;
static const char X_vdev_create[] =	/* 28 bytes */
    "01 50 00 00 14 00 56 00 00 00 00 00 02 00 00 00 "
    "00 00 00 00 02 11 22 33 44 55 00 00 "
;
static const char X_vdev_delete[] =	/* 12 bytes */
    "02 50 00 00 04 00 57 00 01 00 00 00 "
;
static const char X_vdev_param[] =	/* 20 bytes */
    "08 50 00 00 0c 00 5f 00 00 00 00 00 30 00 00 00 "
    "01 00 00 00 "
;
static const char X_vdev_start[] =	/* 112 bytes */
    "03 50 00 00 48 00 58 00 00 00 00 00 00 00 00 00 "
    "64 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "18 00 50 00 3c 14 00 00 46 14 00 00 00 00 00 00 "
    "06 00 00 00 00 2e 2e 00 03 2e 00 00 00 00 12 00 "
;
static const char X_vdev_start_ssid[] =	/* 112 bytes */
    "03 50 00 00 48 00 58 00 02 00 00 00 00 00 00 00 "
    "c8 00 00 00 03 00 00 00 03 00 00 00 05 00 00 00 "
    "68 65 6c 6c 6f 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "18 00 50 00 85 09 00 00 85 09 00 00 00 00 00 00 "
    "05 00 00 00 00 28 24 00 02 28 00 00 00 00 12 00 "
;
static const char X_vdev_stop[] =	/* 12 bytes */
    "06 50 00 00 04 00 5d 00 00 00 00 00 "
;
static const char X_vdev_up[] =	/* 24 bytes */
    "05 50 00 00 10 00 5c 00 00 00 00 00 05 00 00 00 "
    "aa bb cc dd ee 01 00 00 "
;
static const char X_vdev_down[] =	/* 12 bytes */
    "07 50 00 00 04 00 5e 00 00 00 00 00 "
;
static const char X_peer_create[] =	/* 24 bytes */
    "01 60 00 00 10 00 61 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 00 00 00 00 "
;
static const char X_peer_delete[] =	/* 20 bytes */
    "02 60 00 00 0c 00 62 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 "
;
static const char X_peer_param[] =	/* 28 bytes */
    "04 60 00 00 14 00 64 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 03 00 00 00 01 00 00 00 "
;
static const char X_peer_assoc[] =	/* 132 bytes */
    "05 60 00 00 4c 00 65 00 aa bb cc dd ee 01 00 00 "
    "00 00 00 00 01 00 00 00 03 00 00 00 07 10 00 00 "
    "11 04 00 00 01 00 00 00 2c 01 00 00 ff ff 00 00 "
    "08 00 00 00 0c 00 00 00 01 00 00 00 00 00 00 00 "
    "05 00 00 00 00 00 00 00 00 00 00 00 0c 00 00 00 "
    "08 00 00 00 0c 00 11 00 82 84 8b 96 0c 12 18 24 "
    "30 48 60 6c 08 00 11 00 00 01 02 03 04 05 06 07 "
    "10 00 66 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 "
;
static const char X_peer_assoc_vht[] =	/* 120 bytes */
    "05 60 00 00 4c 00 65 00 aa bb cc dd ee 01 00 00 "
    "01 00 00 00 00 00 00 00 ff 01 00 00 01 00 00 06 "
    "11 00 00 00 01 00 00 00 00 00 00 00 ff 1f 00 00 "
    "10 00 00 00 00 00 00 00 02 00 00 00 b2 01 80 33 "
    "0a 00 00 00 11 00 00 00 22 00 00 00 05 00 00 00 "
    "00 00 00 00 08 00 11 00 0c 18 30 60 6c 00 00 00 "
    "00 00 11 00 10 00 66 00 63 03 00 00 fa ff 00 00 "
    "62 03 00 00 fe ff 00 00 "
;
static const char X_key_ptk[] =	/* 120 bytes */
    "09 50 00 00 5c 00 60 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 00 00 00 00 00 00 00 00 04 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 "
    "00 00 00 00 10 00 11 00 00 01 02 03 04 05 06 07 "
    "08 09 0a 0b 0c 0d 0e 0f "
;
static const char X_key_odd[] =	/* 136 bytes */
    "09 50 00 00 5c 00 60 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 02 00 00 00 01 00 00 00 02 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 1e 00 00 00 07 00 00 00 "
    "07 00 00 00 20 00 11 00 a0 a1 a2 a3 a4 a5 a6 a7 "
    "a8 a9 aa ab ac ad ae af b0 b1 b2 b3 b4 b5 b6 b7 "
    "b8 b9 ba bb bc bd 00 00 "
;
static const char X_key_none[] =	/* 104 bytes */
    "09 50 00 00 5c 00 60 00 00 00 00 00 aa bb cc dd "
    "ee 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 11 00 "
;
static const char X_mgmt_auth[] =	/* 72 bytes */
    "08 70 00 00 1c 00 a6 01 00 00 00 00 07 00 00 00 "
    "00 00 00 00 80 67 45 23 01 00 00 00 1e 00 00 00 "
    "20 00 00 00 20 00 11 00 b0 01 02 03 04 05 06 07 "
    "08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 "
    "18 19 1a 1b 1c 1d 00 00 "
;
static const char X_mgmt_assoc[] =	/* 104 bytes */
    "08 70 00 00 1c 00 a6 01 01 00 00 00 31 00 00 00 "
    "85 09 00 00 00 aa 00 80 00 00 00 00 64 00 00 00 "
    "40 00 00 00 40 00 11 00 00 41 42 43 44 45 46 47 "
    "48 49 4a 4b 4c 4d 4e 4f 50 51 52 53 54 55 56 57 "
    "58 59 5a 5b 5c 5d 5e 5f 60 61 62 63 64 65 66 67 "
    "68 69 6a 6b 6c 6d 6e 6f 70 71 72 73 74 75 76 77 "
    "78 79 7a 7b 7c 7d 7e 7f "
;
static const char X_ps_mode[] =	/* 16 bytes */
    "01 90 00 00 08 00 6c 00 00 00 00 00 00 00 00 00 "
;
static const char X_ps_param[] =	/* 20 bytes */
    "02 90 00 00 0c 00 6d 00 00 00 00 00 01 00 00 00 "
    "01 00 00 00 "
;

static size_t
unhex(const char *hex, uint8_t *out, size_t cap)
{
	size_t k = 0;
	unsigned v;
	const char *p = hex;

	while (*p) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (sscanf(p, "%2x", &v) != 1 || k == cap)
			break;
		out[k++] = v;
		p += 2;
	}
	return k;
}

static int
same(const uint8_t *got, size_t n, const char *hex, const char *what)
{
	uint8_t want[2048];
	size_t k, i;

	k = unhex(hex, want, sizeof(want));
	if (n == k && memcmp(got, want, k) == 0)
		return 1;
	printf("FAIL %s: %zu bytes, want %zu\n", what, n, k);
	for (i = 0; i < n || i < k; i++) {
		if (i < n && i < k && got[i] == want[i])
			continue;
		printf("  first difference at byte %zu: got %02x want %02x\n",
		    i, i < n ? got[i] : 0x100, i < k ? want[i] : 0x100);
		break;
	}
	return 0;
}

/* ---- the arguments ---- */

static const uint8_t self[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t bssid[6] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 };

static const struct wmi_mem_chunk chunks[2] = {
	{ 8, 0x123456000ULL, 0x40000 },
	{ 3, 0x9abc0000ULL, 0x1000 },
};

static const struct wmi_chan chans[3] = {
	{ 2412, 2412, 0, WMI_MODE_11G, WMI_CHAN_ADHOC_ALLOWED |
	    WMI_CHAN_ALLOW_HT | WMI_CHAN_ALLOW_VHT | WMI_CHAN_HT40_PLUS,
	    0, 40, 40, 0 },
	{ 2472, 2472, 0, WMI_MODE_11G, WMI_CHAN_PASSIVE | WMI_CHAN_ALLOW_HT |
	    WMI_CHAN_ALLOW_VHT, 0, 40, 40, 0 },
	{ 5260, 5260, 0, WMI_MODE_11A, WMI_CHAN_PASSIVE | WMI_CHAN_DFS |
	    WMI_CHAN_ALLOW_HT | WMI_CHAN_ALLOW_VHT | WMI_CHAN_HT40_PLUS,
	    0, 46, 46, 3 },
};

static const uint32_t freqs[3] = { 2412, 2437, 5180 };
static const uint8_t probe_ie[6] = { 1, 4, 2, 4, 0x0b, 0x16 };
static struct wmi_ssid ssid_meecho = { 6, "meecho" };
static const uint8_t two_bssids[2][6] = {
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	{ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x01 },
};

static const uint8_t legacy[12] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12,
	0x18, 0x24, 0x30, 0x48, 0x60, 0x6c };
static const uint8_t mcs[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const uint8_t legacy5[5] = { 0x0c, 0x18, 0x30, 0x60, 0x6c };

static uint8_t key16[16], key30[30], auth[30], assoc[100];

static struct wmi_scan
scan_active(void)
{
	struct wmi_scan s;

	wmi_scan_defaults(&s);
	s.freqs = freqs;
	s.nchan = 3;
	s.ssids = &ssid_meecho;
	s.nssid = 1;
	s.ie = probe_ie;
	s.ie_len = sizeof(probe_ie);
	return s;
}

static struct wmi_scan
scan_passive(void)
{
	struct wmi_scan s;

	wmi_scan_defaults(&s);
	s.vdev_id = 1;
	s.scan_id = 5;
	s.req_id = 2;
	s.flags |= WMI_SCAN_PASSIVE | WMI_SCAN_SPOOFED_MAC |
	    WMI_SCAN_FILTER_PROBE_REQ;
	s.bssids = two_bssids;
	s.nbssid = 2;
	memcpy(s.mac_addr, "\x02\x00\x00\x12\x34\x56", 6);
	memcpy(s.mac_mask, "\xff\xff\xff\x00\x00\x00", 6);
	return s;
}

static struct wmi_vdev_start
vstart(void)
{
	struct wmi_vdev_start v;

	memset(&v, 0, sizeof(v));
	v.vdev_id = 0;
	v.bcn_intval = 100;
	v.dtim_period = 1;
	v.chan.freq = 5180;
	v.chan.center1 = 5190;
	v.chan.mode = WMI_MODE_11NA_HT40;
	v.chan.max_power = 46;
	v.chan.reg_power = 46;
	v.chan.antenna_max = 3;
	return v;
}

static struct wmi_vdev_start
vstart_ssid(void)
{
	struct wmi_vdev_start v;

	memset(&v, 0, sizeof(v));
	v.vdev_id = 2;
	v.bcn_intval = 200;
	v.dtim_period = 3;
	v.flags = WMI_VDEV_START_HIDDEN_SSID | WMI_VDEV_START_PMF_ENABLED;
	v.ssid = (const uint8_t *)"hello";
	v.ssid_len = 5;
	v.chan.freq = 2437;
	v.chan.center1 = 2437;
	v.chan.mode = WMI_MODE_11NG_HT20;
	v.chan.max_power = 40;
	v.chan.reg_power = 36;
	v.chan.antenna_max = 2;
	return v;
}

static struct wmi_peer_assoc
passoc(void)
{
	struct wmi_peer_assoc a;

	memset(&a, 0, sizeof(a));
	memcpy(a.mac, bssid, 6);
	a.aid = 3;
	a.flags = WMI_PEER_F_AUTH | WMI_PEER_F_QOS | WMI_PEER_F_NEED_PTK_4_WAY |
	    WMI_PEER_F_HT;
	a.caps = 0x0411;
	a.listen_intval = 1;
	a.ht_caps = 0x012c;
	a.max_mpdu = 65535;
	a.mpdu_density = 8;
	a.rate_caps = WMI_RC_HT | WMI_RC_SGI;
	a.nss = 1;
	a.phy_mode = WMI_MODE_11NG_HT20;
	a.legacy = legacy;
	a.nlegacy = 12;
	a.ht = mcs;
	a.nht = 8;
	return a;
}

static struct wmi_peer_assoc
passoc_vht(void)
{
	struct wmi_peer_assoc a;

	memset(&a, 0, sizeof(a));
	memcpy(a.mac, bssid, 6);
	a.vdev_id = 1;
	a.reassoc = 1;
	a.aid = 0x1ff;
	a.flags = WMI_PEER_F_AUTH | WMI_PEER_F_VHT | WMI_PEER_F_80MHZ;
	a.caps = 0x0011;
	a.listen_intval = 1;
	a.max_mpdu = 0x1fff;
	a.mpdu_density = 16;
	a.nss = 2;
	a.vht_caps = 0x338001b2;
	a.phy_mode = WMI_MODE_11AC_VHT80;
	a.ht_info[0] = 0x11;
	a.ht_info[1] = 0x22;
	a.legacy = legacy5;
	a.nlegacy = 5;
	a.vht_rx_max = 867;
	a.vht_rx_mcs = 0xfffa;
	a.vht_tx_max = 866;
	a.vht_tx_mcs = 0xfffe;
	return a;
}

static struct wmi_key
kptk(void)
{
	struct wmi_key k;

	memset(&k, 0, sizeof(k));
	memcpy(k.mac, bssid, 6);
	k.idx = 0;
	k.flags = WMI_KEY_PAIRWISE;
	k.cipher = WMI_CIPHER_AES_CCM;
	k.key = key16;
	k.key_len = 16;
	return k;
}

static struct wmi_key
kodd(void)
{
	struct wmi_key k;

	memset(&k, 0, sizeof(k));
	memcpy(k.mac, bssid, 6);
	k.idx = 2;
	k.flags = WMI_KEY_GROUP;
	k.cipher = WMI_CIPHER_TKIP;
	k.key = key30;
	k.key_len = 30;
	k.txmic_len = 7;
	k.rxmic_len = 7;
	return k;
}

static struct wmi_key
knone(void)
{
	struct wmi_key k;

	memset(&k, 0, sizeof(k));
	memcpy(k.mac, bssid, 6);
	k.cipher = WMI_CIPHER_NONE;
	return k;
}

/* ---- the builders, by case number ---- */

static size_t
build(int c, struct wmi_buf *b)
{
	struct wmi_scan s;
	struct wmi_vdev_start v;
	struct wmi_peer_assoc a;
	struct wmi_key k;

	switch (c) {
	case 0: return wmi_init(b, WMI_INIT_RX_REORDER, NULL, 0);
	case 1: return wmi_init(b, WMI_INIT_RX_REORDER | WMI_INIT_TX_ACK_RSSI |
	    WMI_INIT_EXT_ADDR, chunks, 2);
	case 2: return wmi_init(b, 0, NULL, 0);
	case 3: return wmi_pdev_set_param(b, WMI_PDEV_PMF_QOS, 1);
	case 4: return wmi_pdev_set_regdomain(b, 0x37, 0x37, 0x37, 0x10, 0x30);
	case 5: return wmi_scan_chan_list(b, chans, 3);
	case 6: s = scan_active(); return wmi_start_scan(b, &s);
	case 7: s = scan_passive(); return wmi_start_scan(b, &s);
	case 8: return wmi_stop_scan(b, WMI_SCAN_STOP_ONE, 0, 1, 0);
	case 9: return wmi_vdev_create(b, 0, WMI_VDEV_TYPE_STA,
	    WMI_VDEV_SUBTYPE_NONE, self);
	case 10: return wmi_vdev_delete(b, 1);
	case 11: return wmi_vdev_set_param(b, 0, WMI_VDEV_TX_ENCAP_TYPE,
	    WMI_TXRX_NATIVE_WIFI);
	case 12: v = vstart(); return wmi_vdev_start(b, &v);
	case 13: v = vstart_ssid(); return wmi_vdev_start(b, &v);
	case 14: return wmi_vdev_stop(b, 0);
	case 15: return wmi_vdev_up(b, 0, 5, bssid);
	case 16: return wmi_vdev_down(b, 0);
	case 17: return wmi_peer_create(b, 0, bssid, WMI_PEER_TYPE_DEFAULT);
	case 18: return wmi_peer_delete(b, 0, bssid);
	case 19: return wmi_peer_set_param(b, 0, bssid, WMI_PEER_AUTHORIZE, 1);
	case 20: a = passoc(); return wmi_peer_assoc(b, &a);
	case 21: a = passoc_vht(); return wmi_peer_assoc(b, &a);
	case 22: k = kptk(); return wmi_vdev_install_key(b, &k);
	case 23: k = kodd(); return wmi_vdev_install_key(b, &k);
	case 24: k = knone(); return wmi_vdev_install_key(b, &k);
	case 25: return wmi_mgmt_tx(b, 0, 7, 0, 0x123456780ULL, auth,
	    sizeof(auth));
	case 26: return wmi_mgmt_tx(b, 1, 49, 2437, 0x8000aa00ULL, assoc,
	    sizeof(assoc));
	case 27: return wmi_sta_ps_mode(b, 0, WMI_STA_PS_MODE_DISABLED);
	case 28: return wmi_sta_ps_param(b, 0, WMI_STA_PS_TX_WAKE_THRESHOLD,
	    WMI_STA_PS_TX_WAKE_ALWAYS);
	}
	return 0;
}

static const struct {
	const char *name;
	const char *hex;
} cases[] = {
	{ "init, no chunks", X_init0 },
	{ "init, two chunks, 64-bit", X_init2ext },
	{ "init, no reorder", X_init_noreorder },
	{ "pdev_set_param", X_pdev_param },
	{ "pdev_set_regdomain", X_regdomain },
	{ "scan_chan_list", X_chanlist },
	{ "start_scan, active", X_startscan },
	{ "start_scan, passive", X_startscan_passive },
	{ "stop_scan", X_stopscan },
	{ "vdev_create", X_vdev_create },
	{ "vdev_delete", X_vdev_delete },
	{ "vdev_set_param", X_vdev_param },
	{ "vdev_start", X_vdev_start },
	{ "vdev_start, ssid", X_vdev_start_ssid },
	{ "vdev_stop", X_vdev_stop },
	{ "vdev_up", X_vdev_up },
	{ "vdev_down", X_vdev_down },
	{ "peer_create", X_peer_create },
	{ "peer_delete", X_peer_delete },
	{ "peer_set_param", X_peer_param },
	{ "peer_assoc", X_peer_assoc },
	{ "peer_assoc, vht", X_peer_assoc_vht },
	{ "install_key, ptk", X_key_ptk },
	{ "install_key, odd length", X_key_odd },
	{ "install_key, none", X_key_none },
	{ "mgmt_tx, auth", X_mgmt_auth },
	{ "mgmt_tx, assoc", X_mgmt_assoc },
	{ "sta_ps_mode", X_ps_mode },
	{ "sta_ps_param", X_ps_param },
};

#define NCASES	(int)(sizeof(cases) / sizeof(cases[0]))

static void
commands(void)
{
	struct wmi_buf b;
	uint8_t *p;
	size_t n, want, cap;
	int c, i;

	for (i = 0; i < 16; i++)
		key16[i] = i;
	for (i = 0; i < 30; i++)
		key30[i] = 0xa0 + i;
	auth[0] = 0xb0;			/* authentication */
	for (i = 1; i < 30; i++)
		auth[i] = i;
	assoc[0] = 0x00;		/* association request */
	for (i = 1; i < 100; i++)
		assoc[i] = 0x40 + i;

	for (c = 0; c < NCASES; c++) {
		uint8_t want_b[2048];

		want = unhex(cases[c].hex, want_b, sizeof(want_b));
		p = malloc(want);
		b.p = p;
		b.cap = want;
		n = build(c, &b);
		checks++;
		if (!same(p, n, cases[c].hex, cases[c].name))
			fails++;
		free(p);
		/* Every shorter buffer: 0, and nothing past its end. */
		for (cap = 0; cap < want; cap++) {
			p = malloc(cap ? cap : 1);
			b.p = p;
			b.cap = cap;
			n = build(c, &b);
			CHECK(n == 0, "%s fit in %zu of %zu", cases[c].name,
			    cap, want);
			free(p);
		}
		/* A longer buffer changes nothing. */
		p = malloc(want + 64);
		b.p = p;
		b.cap = want + 64;
		n = build(c, &b);
		CHECK(n == want, "%s in a big buffer: %zu", cases[c].name, n);
		free(p);
	}
}

static void
refusals(void)
{
	static uint8_t mem[4096];
	struct wmi_buf b = { mem, sizeof(mem), 0, 0 };
	struct wmi_mem_chunk many[WMI_MAX_MEM_REQS + 1];
	struct wmi_chan ch;
	struct wmi_scan s;
	struct wmi_vdev_start v;
	struct wmi_peer_assoc a;
	struct wmi_key k;
	struct wmi_ssid longssid;
	uint32_t f65[65];
	uint8_t ie[257], frame[64];
	unsigned i;

	memset(many, 0, sizeof(many));
	memset(f65, 0, sizeof(f65));
	memset(ie, 0, sizeof(ie));

	/* A chunk above 4 GiB without SUPPORT_EXTEND_ADDRESS. */
	CHECK(wmi_init(&b, WMI_INIT_RX_REORDER, chunks, 2) == 0,
	    "64-bit chunk taken without the service");
	CHECK(wmi_init(&b, WMI_INIT_RX_REORDER, chunks + 1, 1) == 228 + 20 - 8,
	    "a low chunk without the service: %zu", b.len);
	CHECK(wmi_init(&b, 0, many, WMI_MAX_MEM_REQS) != 0, "16 chunks");
	CHECK(wmi_init(&b, 0, many, WMI_MAX_MEM_REQS + 1) == 0, "17 chunks");
	CHECK(wmi_init(&b, 0, NULL, 1) == 0, "no chunk array");

	ch = chans[0];
	ch.mode = 64;
	CHECK(wmi_scan_chan_list(&b, &ch, 1) == 0, "mode 64");
	ch = chans[0];
	ch.flags |= 0x1;
	CHECK(wmi_scan_chan_list(&b, &ch, 1) == 0, "flags in the mode bits");
	CHECK(wmi_scan_chan_list(&b, NULL, 1) == 0, "no channels");
	CHECK(wmi_scan_chan_list(&b, NULL, 0) == 16, "empty list: %zu", b.len);

	s = scan_active();
	s.freqs = f65;
	s.nchan = 64;
	CHECK(wmi_start_scan(&b, &s) != 0, "64 channels");
	s.nchan = 65;
	CHECK(wmi_start_scan(&b, &s) == 0, "65 channels");
	s = scan_active();
	s.nssid = 17;
	CHECK(wmi_start_scan(&b, &s) == 0, "17 SSIDs");
	s = scan_active();
	s.nbssid = 5;
	CHECK(wmi_start_scan(&b, &s) == 0, "5 BSSIDs");
	s = scan_active();
	s.ie = ie;
	s.ie_len = 256;
	CHECK(wmi_start_scan(&b, &s) != 0, "256 IE bytes");
	s.ie_len = 257;
	CHECK(wmi_start_scan(&b, &s) == 0, "257 IE bytes");
	s = scan_active();
	longssid.len = 33;
	s.ssids = &longssid;
	CHECK(wmi_start_scan(&b, &s) == 0, "33-byte SSID");
	s = scan_active();
	s.scan_id = 0x1000;
	CHECK(wmi_start_scan(&b, &s) == 0, "scan id 0x1000");
	s = scan_active();
	s.req_id = 0x1000;
	CHECK(wmi_start_scan(&b, &s) == 0, "requestor 0x1000");
	s = scan_active();
	s.ie = NULL;
	CHECK(wmi_start_scan(&b, &s) == 0, "IE length without IEs");

	CHECK(wmi_stop_scan(&b, WMI_SCAN_STOP_ONE, 0x1000, 1, 0) == 0,
	    "stop scan id 0x1000");
	CHECK(wmi_stop_scan(&b, WMI_SCAN_STOP_ALL, 0x1000, 1, 0) != 0,
	    "stop all ignores the scan id");
	CHECK(wmi_stop_scan(&b, WMI_SCAN_STOP_ALL, 0, 0x1000, 0) == 0,
	    "stop requestor 0x1000");

	v = vstart();
	v.ssid_len = 33;
	v.ssid = ie;
	CHECK(wmi_vdev_start(&b, &v) == 0, "33-byte SSID");
	v = vstart();
	v.flags = WMI_VDEV_START_HIDDEN_SSID;
	CHECK(wmi_vdev_start(&b, &v) == 0, "hidden SSID without one");

	a = passoc();
	a.mpdu_density = 17;
	CHECK(wmi_peer_assoc(&b, &a) == 0, "density 17");
	a = passoc();
	a.nlegacy = 129;
	CHECK(wmi_peer_assoc(&b, &a) == 0, "129 rates");
	a = passoc();
	a.ht = NULL;
	CHECK(wmi_peer_assoc(&b, &a) == 0, "HT rates counted, none given");

	k = kptk();
	k.idx = 4;
	CHECK(wmi_vdev_install_key(&b, &k) == 0, "key index 4");
	k = kptk();
	k.key_len = 33;
	CHECK(wmi_vdev_install_key(&b, &k) == 0, "33-byte key");
	k = kptk();
	k.key = NULL;
	CHECK(wmi_vdev_install_key(&b, &k) == 0, "CCMP without a key");
	k = knone();
	k.key = key16;
	CHECK(wmi_vdev_install_key(&b, &k) == 0, "NONE with a key");

	memset(frame, 0, sizeof(frame));
	frame[0] = 0xb0;
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) != 0, "24-byte frame");
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 23) == 0, "23-byte frame");
	CHECK(wmi_mgmt_tx(&b, 0, 50, 0, 0, frame, 24) == 0, "desc_id 50");
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, NULL, 24) == 0, "no frame");
	frame[0] = 0x08;		/* data */
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) == 0, "a data frame");
	frame[0] = 0x48;		/* Null */
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) != 0, "a Null");
	frame[0] = 0xc8;		/* QoS Null */
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) != 0, "a QoS Null");
	frame[0] = 0xb1;		/* protocol version 1 */
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) == 0, "version 1");
	frame[0] = 0xd0;		/* action, 24 bytes: inline 24 */
	CHECK(wmi_mgmt_tx(&b, 0, 0, 0, 0, frame, 24) == 4 + 4 + 28 + 4 + 24,
	    "24-byte action: %zu", b.len);
	CHECK(mem[4 + 4 + 24] == 24 && mem[4 + 4 + 20] == 24,
	    "buf_len and frame_len of a 24-byte frame");

	/* The defaults ath10k scans with. */
	wmi_scan_defaults(&s);
	CHECK(s.req_id == 1 && s.scan_id == 0 && s.priority == 1 &&
	    s.dwell_active == 50 && s.dwell_passive == 150 &&
	    s.min_rest == 50 && s.max_rest == 500 &&
	    s.max_scan_time == 20000 && s.probe_delay == 5 &&
	    s.notify == 0x11f && s.flags == WMI_SCAN_CHAN_STAT_EVENT &&
	    s.nbssid == 1 && s.bssids[0][0] == 0xff && s.bssids[0][5] == 0xff,
	    "scan defaults");
	for (i = 0; i < 6; i++)
		CHECK(s.mac_addr[i] == 0 && s.mac_mask[i] == 0, "no spoof mac");
}

/* ---- events ---- */

static uint8_t ev[4096];
static size_t evlen;
static size_t bound[64];		/* TLV ends, for the cuts */
static unsigned nbound;

static void
e32(uint8_t *p, uint32_t v)
{

	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
ev_begin(uint32_t id)
{

	e32(ev, id);
	evlen = 4;
	nbound = 0;
	bound[nbound++] = 4;
}

static void
ev_tlv(unsigned tag, const void *v, size_t n)
{

	ev[evlen] = n;
	ev[evlen + 1] = n >> 8;
	ev[evlen + 2] = tag;
	ev[evlen + 3] = tag >> 8;
	if (n > 0)
		memcpy(ev + evlen + 4, v, n);
	evlen += 4 + n;
	bound[nbound++] = evlen;
}

static void
ev_words(unsigned tag, const uint32_t *w, unsigned n)
{
	uint8_t tmp[1024];
	unsigned i;

	for (i = 0; i < n; i++)
		e32(tmp + 4 * i, w[i]);
	ev_tlv(tag, tmp, 4 * n);
}

typedef int (*parser)(const struct wmi_ev *);

/*
 * The event as built must parse; every cut must not read past the end,
 * and may parse only at a TLV boundary at or after ok_from.
 */
static void
cuts(parser fn, size_t ok_from, const char *what)
{
	struct wmi_ev e;
	uint8_t *copy;
	size_t k;
	unsigned i;
	int r, at_bound;

	copy = malloc(evlen);
	memcpy(copy, ev, evlen);
	CHECK(wmi_event(copy, evlen, &e) == 0 && fn(&e) == 0,
	    "%s: whole event refused", what);
	free(copy);
	for (k = 0; k < evlen; k++) {
		copy = malloc(k ? k : 1);
		memcpy(copy, ev, k);
		r = wmi_event(copy, k, &e);
		if (r == 0)
			r = fn(&e);
		at_bound = 0;
		for (i = 0; i < nbound; i++)
			if (bound[i] == k)
				at_bound = 1;
		if (r == 0)
			CHECK(at_bound && k >= ok_from,
			    "%s: cut at %zu of %zu parsed", what, k, evlen);
		free(copy);
	}
	/* Each TLV length one too long, and the largest. */
	for (i = 0; i + 1 < nbound; i++) {
		size_t at = bound[i];
		uint16_t old = ev[at] | ev[at + 1] << 8;
		uint16_t bad[2] = { (uint16_t)(old + 1), 0xffff };
		unsigned j;

		for (j = 0; j < 2; j++) {
			ev[at] = bad[j];
			ev[at + 1] = bad[j] >> 8;
			copy = malloc(evlen);
			memcpy(copy, ev, evlen);
			wmi_event(copy, evlen, &e);
			CHECK(fn(&e) != 0, "%s: TLV %u length %u past the end "
			    "taken", what, i, bad[j]);
			free(copy);
		}
		ev[at] = old;
		ev[at + 1] = old >> 8;
	}
}

/* Parsers as one signature, keeping the last result for inspection. */
static struct wmi_svc_ready g_sr;
static struct wmi_svc g_svc;
static struct wmi_ready g_rdy;
static struct wmi_scan_ev g_scan;
static struct wmi_mgmt_rx g_rx;
static struct wmi_vdev_start_resp g_vsr;
static struct wmi_tx_compl g_tc, g_tcs[8];
static unsigned g_ntc, g_tcmax = 8;
static struct wmi_key_done g_kd;
static struct wmi_kickout g_ko;

static int p_svc_ready(const struct wmi_ev *e) { return wmi_svc_ready(e, &g_sr, &g_svc); }
static int p_svc_avail(const struct wmi_ev *e) { return wmi_svc_available(e, &g_svc); }
static int p_ready(const struct wmi_ev *e) { return wmi_ready(e, &g_rdy); }
static int p_scan(const struct wmi_ev *e) { return wmi_scan_event(e, &g_scan); }
static int p_rx(const struct wmi_ev *e) { return wmi_mgmt_rx(e, &g_rx); }
static int p_vsr(const struct wmi_ev *e) { return wmi_vdev_start_resp(e, &g_vsr); }
static int p_tc(const struct wmi_ev *e) { return wmi_mgmt_tx_compl(e, &g_tc); }
static int p_tcb(const struct wmi_ev *e) { return wmi_mgmt_tx_bundle(e, g_tcs, g_tcmax, &g_ntc); }
static int p_kd(const struct wmi_ev *e) { return wmi_install_key_done(e, &g_kd); }
static int p_ko(const struct wmi_ev *e) { return wmi_peer_kickout(e, &g_ko); }

static int
parse(parser fn)
{
	struct wmi_ev e;

	if (wmi_event(ev, evlen, &e) != 0)
		return -1;
	return fn(&e);
}

/* SERVICE_READY's map from htc-wmi.md 5.1: word, mask. */
static const uint8_t svc_masks[][2] = {
	{ 0, 0xd }, { 1, 0x7 }, { 2, 0xd }, { 3, 0x3 }, { 4, 0xf }, { 5, 0x9 },
	{ 6, 0xb }, { 7, 0xf }, { 8, 0xa }, { 9, 0x9 }, { 10, 0x2 },
	{ 11, 0xa }, { 12, 0x1 }, { 13, 0xd }, { 14, 0x7 }, { 15, 0xf },
	{ 16, 0xb }, { 19, 0x1 }, { 23, 0x1 },
};

/* Linux's names of the services it maps from TLV numbers (wmi-tlv.h). */
static const struct {
	const char *name;
	unsigned tlv;
} svc_names[] = {
	{ "WMI_SERVICE_BEACON_OFFLOAD", 0 },
	{ "WMI_SERVICE_SCAN_OFFLOAD", 1 },
	{ "WMI_SERVICE_ROAM_SCAN_OFFLOAD", 2 },
	{ "WMI_SERVICE_BCN_MISS_OFFLOAD", 3 },
	{ "WMI_SERVICE_STA_PWRSAVE", 4 },
	{ "WMI_SERVICE_STA_ADVANCED_PWRSAVE", 5 },
	{ "WMI_SERVICE_AP_UAPSD", 6 },
	{ "WMI_SERVICE_AP_DFS", 7 },
	{ "WMI_SERVICE_11AC", 8 },
	{ "WMI_SERVICE_BLOCKACK", 9 },
	{ "WMI_SERVICE_PHYERR", 10 },
	{ "WMI_SERVICE_BCN_FILTER", 11 },
	{ "WMI_SERVICE_RTT", 12 },
	{ "WMI_SERVICE_WOW", 13 },
	{ "WMI_SERVICE_RATECTRL_CACHE", 14 },
	{ "WMI_SERVICE_IRAM_TIDS", 15 },
	{ "WMI_SERVICE_ARPNS_OFFLOAD", 16 },
	{ "WMI_SERVICE_NLO", 17 },
	{ "WMI_SERVICE_GTK_OFFLOAD", 18 },
	{ "WMI_SERVICE_SCAN_SCH", 19 },
	{ "WMI_SERVICE_CSA_OFFLOAD", 20 },
	{ "WMI_SERVICE_CHATTER", 21 },
	{ "WMI_SERVICE_COEX_FREQAVOID", 22 },
	{ "WMI_SERVICE_PACKET_POWER_SAVE", 23 },
	{ "WMI_SERVICE_FORCE_FW_HANG", 24 },
	{ "WMI_SERVICE_GPIO", 25 },
	{ "WMI_SERVICE_STA_DTIM_PS_MODULATED_DTIM", 26 },
	{ "WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG", 27 },
	{ "WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG", 28 },
	{ "WMI_SERVICE_STA_KEEP_ALIVE", 29 },
	{ "WMI_SERVICE_TX_ENCAP", 30 },
	{ "WMI_SERVICE_AP_PS_DETECT_OUT_OF_SYNC", 31 },
	{ "WMI_SERVICE_EARLY_RX", 32 },
	{ "WMI_SERVICE_STA_SMPS", 33 },
	{ "WMI_SERVICE_FWTEST", 34 },
	{ "WMI_SERVICE_STA_WMMAC", 35 },
	{ "WMI_SERVICE_TDLS", 36 },
	{ "WMI_SERVICE_BURST", 37 },
	{ "WMI_SERVICE_MCC_BCN_INTERVAL_CHANGE", 38 },
	{ "WMI_SERVICE_ADAPTIVE_OCS", 39 },
	{ "WMI_SERVICE_BA_SSN_SUPPORT", 40 },
	{ "WMI_SERVICE_FILTER_IPSEC_NATKEEPALIVE", 41 },
	{ "WMI_SERVICE_WLAN_HB", 42 },
	{ "WMI_SERVICE_LTE_ANT_SHARE_SUPPORT", 43 },
	{ "WMI_SERVICE_BATCH_SCAN", 44 },
	{ "WMI_SERVICE_QPOWER", 45 },
	{ "WMI_SERVICE_PLMREQ", 46 },
	{ "WMI_SERVICE_THERMAL_MGMT", 47 },
	{ "WMI_SERVICE_RMC", 48 },
	{ "WMI_SERVICE_MHF_OFFLOAD", 49 },
	{ "WMI_SERVICE_COEX_SAR", 50 },
	{ "WMI_SERVICE_BCN_TXRATE_OVERRIDE", 51 },
	{ "WMI_SERVICE_NAN", 52 },
	{ "WMI_SERVICE_L1SS_STAT", 53 },
	{ "WMI_SERVICE_ESTIMATE_LINKSPEED", 54 },
	{ "WMI_SERVICE_OBSS_SCAN", 55 },
	{ "WMI_SERVICE_TDLS_OFFCHAN", 56 },
	{ "WMI_SERVICE_TDLS_UAPSD_BUFFER_STA", 57 },
	{ "WMI_SERVICE_TDLS_UAPSD_SLEEP_STA", 58 },
	{ "WMI_SERVICE_IBSS_PWRSAVE", 59 },
	{ "WMI_SERVICE_LPASS", 60 },
	{ "WMI_SERVICE_EXTSCAN", 61 },
	{ "WMI_SERVICE_D0WOW", 62 },
	{ "WMI_SERVICE_HSOFFLOAD", 63 },
	{ "WMI_SERVICE_ROAM_HO_OFFLOAD", 64 },
	{ "WMI_SERVICE_RX_FULL_REORDER", 65 },
	{ "WMI_SERVICE_DHCP_OFFLOAD", 66 },
	{ "WMI_SERVICE_STA_RX_IPA_OFFLOAD_SUPPORT", 67 },
	{ "WMI_SERVICE_MDNS_OFFLOAD", 68 },
	{ "WMI_SERVICE_SAP_AUTH_OFFLOAD", 69 },
	{ "WMI_SERVICE_MGMT_TX_WMI", 76 },
	{ "WMI_SERVICE_MESH_11S", 103 },
	{ "WMI_SERVICE_SYNC_DELETE_CMDS", 92 },
	{ "WMI_SERVICE_PEER_STATS", 116 },
	{ "WMI_SERVICE_SPOOF_MAC_SUPPORT", 155 },
	{ "WMI_SERVICE_THERM_THROT", 140 },
	{ "WMI_SERVICE_TX_DATA_ACK_RSSI", 174 },
	{ "WMI_SERVICE_SUPPORT_EXTEND_ADDRESS", 179 },
};

static uint32_t svc_ev[104 / 4];

static void
svc_ready_event(unsigned nmem, unsigned hdr_nmem, int dbs, size_t map_bytes,
	unsigned inner_tag, int skip)
{
	uint32_t reg[9], map[64], dbsw[2] = { 1, 2 };
	uint8_t mem[17 * 20];
	unsigned i;

	memset(svc_ev, 0, sizeof(svc_ev));
	svc_ev[0] = 0x12345678;		/* fw_build_vers */
	svc_ev[1] = WMI_ABI_VER0;
	svc_ev[2] = 0x23b;		/* abi_ver1: 571 */
	svc_ev[3] = WMI_ABI_NS0;
	svc_ev[4] = WMI_ABI_NS1;
	svc_ev[7] = 0x3;		/* phy_capability */
	svc_ev[8] = 2;			/* max_frag_entry */
	svc_ev[9] = 2;			/* num_rf_chains, +36 */
	svc_ev[10] = 0x19ef;		/* ht_cap_info, +40 */
	svc_ev[11] = 0x339959b2;	/* vht_cap_info, +44 */
	svc_ev[12] = 0xfffa;		/* vht_supp_mcs */
	svc_ev[13] = 0;			/* hw_min_tx_power */
	svc_ev[14] = 30;		/* hw_max_tx_power */
	svc_ev[15] = 0x2;		/* sys_cap_info: RF kill */
	svc_ev[17] = 0x200;		/* max_bcn_ie_size */
	svc_ev[18] = hdr_nmem;		/* num_mem_reqs, +72 */
	svc_ev[19] = 64;		/* max_num_scan_chans */
	svc_ev[20] = 0x77;		/* hw_bd_id */
	for (i = 0; i < 9; i++)
		reg[i] = 0x100 + i;
	reg[0] = 0x6a;			/* eeprom_rd */
	memset(map, 0, sizeof(map));
	for (i = 0; i < sizeof(svc_masks) / sizeof(svc_masks[0]); i++)
		map[svc_masks[i][0]] = svc_masks[i][1];
	for (i = 0; i < nmem; i++) {
		uint8_t *t = mem + 20 * i;

		t[0] = 16; t[1] = 0;
		t[2] = inner_tag; t[3] = inner_tag >> 8;
		e32(t + 4, 8 + i);	/* req_id */
		e32(t + 8, 0x1000);	/* unit_size */
		e32(t + 12, 0x2);	/* num_unit_info: peers */
		e32(t + 16, i);		/* num_units */
	}

	ev_begin(WMI_EV_SERVICE_READY | 0x12000000);	/* PLT_PRIV bits set */
	if (skip != 1)
		ev_words(WMI_TAG_SERVICE_READY_EVENT, svc_ev, 26);
	if (skip != 2)
		ev_words(WMI_TAG_HAL_REG_CAPABILITIES, reg, 9);
	if (skip != 3) {
		uint8_t tmp[256];
		for (i = 0; i < 64; i++)
			e32(tmp + 4 * i, map[i]);
		ev_tlv(WMI_TAG_ARRAY_UINT32, tmp, map_bytes);
	}
	if (skip != 4)
		ev_tlv(WMI_TAG_ARRAY_STRUCT, mem, 20 * nmem);
	if (dbs)
		ev_words(WMI_TAG_ARRAY_UINT32, dbsw, 2);
}

static void
service_ready(const char *capture)
{
	FILE *f;
	char line[256], name[128], state[32];
	unsigned i, on = 0, lines = 0, id;
	int found, en;

	svc_ready_event(1, 1, 1, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == 0, "service ready refused");
	CHECK(g_sr.fw_build == 0x12345678 && g_sr.abi_ver0 == WMI_ABI_VER0 &&
	    g_sr.abi_ver1 == 0x23b && g_sr.phy_capability == 3 &&
	    g_sr.max_frag_entry == 2 && g_sr.num_rf_chains == 2 &&
	    g_sr.ht_cap_info == 0x19ef && g_sr.vht_cap_info == 0x339959b2 &&
	    g_sr.vht_supp_mcs == 0xfffa && g_sr.hw_max_tx_power == 30 &&
	    g_sr.sys_cap_info == 2 && g_sr.max_bcn_ie_size == 0x200 &&
	    g_sr.max_num_scan_chans == 64 && g_sr.hw_bd_id == 0x77,
	    "service ready fields");
	CHECK(g_sr.eeprom_rd == 0x6a && g_sr.eeprom_rd_ext == 0x101 &&
	    g_sr.regcap1 == 0x102 && g_sr.regcap2 == 0x103 &&
	    g_sr.wireless_modes == 0x104 && g_sr.low_2ghz_chan == 0x105 &&
	    g_sr.high_2ghz_chan == 0x106 && g_sr.low_5ghz_chan == 0x107 &&
	    g_sr.high_5ghz_chan == 0x108, "hal reg capabilities");
	CHECK(g_sr.num_mem_reqs == 1 && g_sr.mem_req[0].req_id == 8 &&
	    g_sr.mem_req[0].unit_size == 0x1000 &&
	    g_sr.mem_req[0].num_unit_info == 2 &&
	    g_sr.mem_req[0].num_units == 0, "memory request");
	CHECK(g_svc.len == 128, "map length %zu", g_svc.len);

	/* The map, service by service, against the tablet's Linux. */
	f = capture ? fopen(capture, "r") : NULL;
	if (f == NULL) {
		printf("(no wmi_services.txt - the map against 5.1 only)\n");
	} else {
		while (fgets(line, sizeof(line), f) != NULL) {
			if (sscanf(line, "%127s %31s", name, state) != 2)
				continue;
			lines++;
			en = strcmp(state, "enabled") == 0;
			found = 0;
			for (i = 0; i < sizeof(svc_names) / sizeof(svc_names[0]);
			    i++) {
				if (strcmp(svc_names[i].name, name) != 0)
					continue;
				found = 1;
				id = svc_names[i].tlv;
				CHECK(wmi_svc_enabled(&g_svc, id) == en,
				    "%s (TLV %u): map says %d, tablet %s",
				    name, id, wmi_svc_enabled(&g_svc, id),
				    state);
			}
			/* Linux could only have set it from a TLV bit. */
			if (!found)
				CHECK(!en, "%s enabled but not a TLV service",
				    name);
			on += en;
		}
		fclose(f);
		CHECK(lines == 122 && on == 47,
		    "capture: %u lines, %u enabled", lines, on);
	}
	/* Bits the stand knows by name. */
	CHECK(wmi_svc_enabled(&g_svc, WMI_SVC_RX_FULL_REORDER) &&
	    wmi_svc_enabled(&g_svc, WMI_SVC_MGMT_TX_WMI) &&
	    wmi_svc_enabled(&g_svc, WMI_SVC_SYNC_DELETE_CMDS) &&
	    wmi_svc_enabled(&g_svc, WMI_SVC_STA_KEEP_ALIVE) &&
	    !wmi_svc_enabled(&g_svc, WMI_SVC_BURST) &&
	    !wmi_svc_enabled(&g_svc, WMI_SVC_EXTEND_ADDRESS) &&
	    !wmi_svc_enabled(&g_svc, 127) && !wmi_svc_enabled(&g_svc, 300),
	    "named services");
	/* A shorter map: services past its length are off (Linux's bound). */
	svc_ready_event(1, 1, 1, 64, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == 0 && g_svc.len == 64 &&
	    wmi_svc_enabled(&g_svc, 63) && !wmi_svc_enabled(&g_svc, 65),
	    "64-byte map");

	/* Cuts: valid from the end of the memory requests on. */
	svc_ready_event(2, 2, 1, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	cuts(p_svc_ready, bound[4], "service ready");

	for (i = 1; i <= 4; i++) {
		svc_ready_event(1, 1, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, i);
		CHECK(parse(p_svc_ready) == EINVAL, "part %u missing taken", i);
	}
	svc_ready_event(16, 16, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == 0 && g_sr.mem_req[15].num_units == 15,
	    "16 memory requests");
	svc_ready_event(17, 16, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == EINVAL, "17 memory requests");
	svc_ready_event(1, 2, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == EINVAL, "count above the requests");
	svc_ready_event(0, 0, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == 0 && g_sr.num_mem_reqs == 0,
	    "no memory requests");
	svc_ready_event(1, 1, 0, 128, WMI_TAG_ARRAY_UINT32, 0);
	CHECK(parse(p_svc_ready) == EINVAL, "a stranger in the requests");
	svc_ready_event(1, 1, 0, 126, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
	CHECK(parse(p_svc_ready) == EINVAL, "map not whole words");

	/* The ABI words, each. */
	for (i = 0; i < 6; i++) {
		svc_ready_event(1, 1, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 0);
		e32(ev + 4 + 4 + 4 + 4 * i, 0xdead);
		CHECK(parse(p_svc_ready) == (i == 1 ? 0 : ENOTSUP),
		    "abi word %u", i);
	}
	/* A structure one byte short of its size (Linux's policy). */
	svc_ready_event(1, 1, 0, 128, WMI_TAG_WLAN_HOST_MEM_REQ, 1);
	{
		uint8_t save[4096];
		size_t n = evlen;

		memcpy(save, ev + 4, n - 4);
		ev_begin(WMI_EV_SERVICE_READY);
		ev_tlv(WMI_TAG_SERVICE_READY_EVENT, svc_ev, 103);
		memcpy(ev + evlen, save, n - 4);
		evlen += n - 4;
		CHECK(parse(p_svc_ready) == EINVAL, "103-byte service ready");
	}
}

static void
service_available(void)
{
	uint32_t w[5] = { 4, 0, 1u << (179 - 160), 0, 0x80000000u };

	ev_begin(WMI_EV_SERVICE_AVAILABLE);
	ev_words(WMI_TAG_SERVICE_AVAILABLE_EVENT, w, 5);
	memset(&g_svc, 0, sizeof(g_svc));
	CHECK(parse(p_svc_avail) == 0 && g_svc.ext_words == 4,
	    "service available");
	CHECK(wmi_svc_enabled(&g_svc, WMI_SVC_EXTEND_ADDRESS) &&
	    !wmi_svc_enabled(&g_svc, WMI_SVC_SPOOF_MAC) &&
	    !wmi_svc_enabled(&g_svc, WMI_SVC_THERM_THROT) &&
	    wmi_svc_enabled(&g_svc, 255) && !wmi_svc_enabled(&g_svc, 256),
	    "extended services");
	cuts(p_svc_avail, bound[1], "service available");
	/* Two words only: services past them are off. */
	ev_begin(WMI_EV_SERVICE_AVAILABLE);
	ev_words(WMI_TAG_SERVICE_AVAILABLE_EVENT, w, 3);
	w[2] = 0;
	CHECK(parse(p_svc_avail) == 0 && g_svc.ext_words == 2 &&
	    wmi_svc_enabled(&g_svc, 179) && !wmi_svc_enabled(&g_svc, 255),
	    "short extended map");
	ev_begin(WMI_EV_SERVICE_AVAILABLE);
	ev_words(WMI_TAG_SERVICE_AVAILABLE_EVENT, w, 0);
	CHECK(parse(p_svc_avail) == EINVAL, "empty service available");
}

static void
ready(void)
{
	uint8_t r[36];

	memset(r, 0, sizeof(r));
	e32(r, WMI_ABI_VER0);
	e32(r + 4, 0x23b);
	memcpy(r + 24, "\x00\x03\x7f\x12\x34\x56", 6);
	r[30] = 0xee;			/* padding: not the mac */
	e32(r + 32, 0);
	ev_begin(WMI_EV_READY);
	ev_tlv(WMI_TAG_READY_EVENT, r, 36);
	CHECK(parse(p_ready) == 0 && g_rdy.abi_ver0 == WMI_ABI_VER0 &&
	    g_rdy.abi_ver1 == 0x23b && g_rdy.status == 0 &&
	    memcmp(g_rdy.mac, "\x00\x03\x7f\x12\x34\x56", 6) == 0,
	    "ready");
	cuts(p_ready, bound[1], "ready");
	ev_begin(WMI_EV_READY);
	ev_tlv(WMI_TAG_READY_EVENT, r, 35);
	CHECK(parse(p_ready) == EINVAL, "35-byte ready");
	ev_begin(WMI_EV_READY);
	CHECK(parse(p_ready) == EINVAL, "ready without its TLV");
}

static void
scan_events(void)
{
	uint32_t s[6] = { WMI_SCAN_EV_FOREIGN_CHANNEL, 0, 2437, 0xa001,
	    0xa000, 0 };
	uint8_t junk[3] = { 1, 2, 3 };

	ev_begin(WMI_EV_SCAN);
	ev_tlv(0x7ff, junk, 3);		/* an unknown TLV first */
	ev_words(WMI_TAG_SCAN_EVENT, s, 6);
	CHECK(parse(p_scan) == 0 && g_scan.type == WMI_SCAN_EV_FOREIGN_CHANNEL &&
	    g_scan.freq == 2437 && g_scan.req_id == 0xa001 &&
	    g_scan.scan_id == 0xa000 && g_scan.vdev_id == 0, "scan event");
	cuts(p_scan, bound[2], "scan event");
	s[0] = WMI_SCAN_EV_COMPLETED;
	s[1] = WMI_SCAN_R_CANCELLED;
	ev_begin(WMI_EV_SCAN);
	ev_words(WMI_TAG_SCAN_EVENT, s, 6);
	CHECK(parse(p_scan) == 0 && g_scan.type == WMI_SCAN_EV_COMPLETED &&
	    g_scan.reason == WMI_SCAN_R_CANCELLED, "scan completed");
	ev_begin(WMI_EV_SCAN);
	ev_tlv(WMI_TAG_SCAN_EVENT, s, 23);
	CHECK(parse(p_scan) == EINVAL, "23-byte scan event");
}

static void
mgmt_rx(void)
{
	uint32_t h[10] = { 6, 40, 1000, WMI_MODE_11G, 30, WMI_RX_OK,
	    40, 0x80, 0, 0 };
	uint8_t frame[32];
	unsigned i;

	for (i = 0; i < 32; i++)
		frame[i] = i < 30 ? 0x80 + i : 0;	/* a beacon, padded */
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	CHECK(parse(p_rx) == 0 && g_rx.channel == 6 && g_rx.snr == 40 &&
	    g_rx.rate == 1000 && g_rx.phy_mode == WMI_MODE_11G &&
	    g_rx.status == 0 && g_rx.rssi[0] == 40 && g_rx.rssi[1] == 0x80 &&
	    g_rx.len == 30 && g_rx.frame == ev + 4 + 44 + 4 &&
	    g_rx.frame[0] == 0x80 && g_rx.frame[29] == 0x80 + 29,
	    "mgmt rx");
	cuts(p_rx, bound[2], "mgmt rx");
	/* buf_len past the array. */
	h[4] = 33;
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	CHECK(parse(p_rx) == EINVAL, "buf_len past the frame");
	h[4] = 32;
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	CHECK(parse(p_rx) == 0 && g_rx.len == 32, "buf_len = the array");
	h[4] = 0xffffffffu;
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	CHECK(parse(p_rx) == EINVAL, "buf_len 4 GiB");
	h[4] = 30;
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	CHECK(parse(p_rx) == EINVAL, "no frame");
	ev_begin(WMI_EV_MGMT_RX);
	ev_tlv(WMI_TAG_MGMT_RX_HDR, h, 39);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	CHECK(parse(p_rx) == EINVAL, "39-byte header");
	/* A short known structure anywhere spoils the event, as in Linux. */
	ev_begin(WMI_EV_MGMT_RX);
	ev_words(WMI_TAG_MGMT_RX_HDR, h, 10);
	ev_tlv(WMI_TAG_ARRAY_BYTE, frame, 32);
	ev_tlv(WMI_TAG_SCAN_EVENT, frame, 20);
	CHECK(parse(p_rx) == EINVAL, "short scan event inside mgmt rx");
}

static void
vdev_start_resp(void)
{
	uint32_t r[4] = { 0, 1, WMI_VDEV_RESP_START, 0 };

	ev_begin(WMI_EV_VDEV_START_RESP);
	ev_words(WMI_TAG_VDEV_START_RESPONSE, r, 4);
	CHECK(parse(p_vsr) == 0 && g_vsr.vdev_id == 0 && g_vsr.req_id == 1 &&
	    g_vsr.resp_type == 0 && g_vsr.status == 0, "vdev start resp");
	cuts(p_vsr, bound[1], "vdev start resp");
	ev_begin(WMI_EV_VDEV_START_RESP);
	ev_tlv(WMI_TAG_VDEV_START_RESPONSE, r, 15);
	CHECK(parse(p_vsr) == EINVAL, "15-byte vdev start resp");
}

static void
tx_completions(void)
{
	uint32_t c[5] = { 7, 0, 0, 0x1234, 50 };
	uint32_t num = 3, ids[3] = { 4, 9, 49 }, st[3] = { 0, 1, 2 };
	uint32_t pp[3] = { 11, 12, 13 }, rs[2] = { 1, 2 };

	ev_begin(WMI_EV_MGMT_TX_COMPLETION);
	ev_words(WMI_TAG_MGMT_TX_COMPL_EVENT, c, 4);
	CHECK(parse(p_tc) == 0 && g_tc.desc_id == 7 && g_tc.status == 0 &&
	    g_tc.ppdu_id == 0x1234 && !g_tc.has_ack_rssi, "tx completion");
	cuts(p_tc, bound[1], "tx completion");
	ev_begin(WMI_EV_MGMT_TX_COMPLETION);
	ev_words(WMI_TAG_MGMT_TX_COMPL_EVENT, c, 5);
	CHECK(parse(p_tc) == 0 && g_tc.has_ack_rssi && g_tc.ack_rssi == 50,
	    "tx completion with rssi");
	ev_begin(WMI_EV_MGMT_TX_COMPLETION);
	ev_tlv(WMI_TAG_MGMT_TX_COMPL_EVENT, c, 15);
	CHECK(parse(p_tc) == EINVAL, "15-byte tx completion");

	ev_begin(WMI_EV_MGMT_TX_BUNDLE_COMPL);
	ev_words(WMI_TAG_MGMT_TX_COMPL_BUNDLE, &num, 1);
	ev_words(WMI_TAG_ARRAY_UINT32, ids, 3);
	ev_words(WMI_TAG_ARRAY_UINT32, st, 3);
	ev_words(WMI_TAG_ARRAY_UINT32, pp, 3);
	ev_words(WMI_TAG_ARRAY_UINT32, rs, 2);	/* short: not used */
	g_tcmax = 8;
	CHECK(parse(p_tcb) == 0 && g_ntc == 3 &&
	    g_tcs[0].desc_id == 4 && g_tcs[1].desc_id == 9 &&
	    g_tcs[2].desc_id == 49 && g_tcs[0].status == 0 &&
	    g_tcs[1].status == 1 && g_tcs[2].status == 2 &&
	    g_tcs[2].ppdu_id == 13 && !g_tcs[0].has_ack_rssi,
	    "bundle (status from its own array)");
	cuts(p_tcb, bound[3], "bundle");
	g_tcmax = 2;
	CHECK(parse(p_tcb) == ENOSPC, "bundle larger than the room");
	g_tcmax = 8;
	ev_begin(WMI_EV_MGMT_TX_BUNDLE_COMPL);
	ev_words(WMI_TAG_MGMT_TX_COMPL_BUNDLE, &num, 1);
	ev_words(WMI_TAG_ARRAY_UINT32, ids, 3);
	ev_words(WMI_TAG_ARRAY_UINT32, st, 2);
	CHECK(parse(p_tcb) == EINVAL, "status shorter than the count");
	num = 1000;
	ev_begin(WMI_EV_MGMT_TX_BUNDLE_COMPL);
	ev_words(WMI_TAG_MGMT_TX_COMPL_BUNDLE, &num, 1);
	ev_words(WMI_TAG_ARRAY_UINT32, ids, 3);
	ev_words(WMI_TAG_ARRAY_UINT32, st, 3);
	CHECK(parse(p_tcb) == EINVAL, "count past the arrays");
}

static void
keys_and_kickout(void)
{
	uint8_t k[24], m[16];

	memset(k, 0, sizeof(k));
	e32(k, 0);
	memcpy(k + 4, bssid, 6);
	e32(k + 12, 1);
	e32(k + 16, WMI_KEY_GROUP);
	e32(k + 20, 0);
	ev_begin(WMI_EV_VDEV_INSTALL_KEY_DONE);
	ev_tlv(WMI_TAG_VDEV_INSTALL_KEY_DONE, k, 24);
	CHECK(parse(p_kd) == 0 && g_kd.full && g_kd.key_idx == 1 &&
	    g_kd.key_flags == WMI_KEY_GROUP &&
	    memcmp(g_kd.mac, bssid, 6) == 0, "install key done");
	cuts(p_kd, bound[1], "install key done");
	ev_begin(WMI_EV_VDEV_INSTALL_KEY_DONE);
	ev_tlv(WMI_TAG_VDEV_INSTALL_KEY_DONE, k, 4);
	CHECK(parse(p_kd) == 0 && !g_kd.full, "install key done, short");
	ev_begin(WMI_EV_VDEV_INSTALL_KEY_DONE);
	ev_tlv(WMI_TAG_VDEV_INSTALL_KEY_DONE, k, 3);
	CHECK(parse(p_kd) == EINVAL, "3-byte install key done");

	memset(m, 0, sizeof(m));
	memcpy(m, bssid, 6);
	e32(m + 8, 2);
	e32(m + 12, 33);
	ev_begin(WMI_EV_PEER_STA_KICKOUT);
	ev_tlv(WMI_TAG_PEER_STA_KICKOUT, m, 8);
	CHECK(parse(p_ko) == 0 && memcmp(g_ko.mac, bssid, 6) == 0 &&
	    !g_ko.has_reason, "kickout");
	cuts(p_ko, bound[1], "kickout");
	ev_begin(WMI_EV_PEER_STA_KICKOUT);
	ev_tlv(WMI_TAG_PEER_STA_KICKOUT, m, 16);
	CHECK(parse(p_ko) == 0 && g_ko.has_reason && g_ko.reason == 2 &&
	    g_ko.has_rssi && g_ko.rssi == 33, "kickout with reason");
	ev_begin(WMI_EV_PEER_STA_KICKOUT);
	ev_tlv(WMI_TAG_PEER_STA_KICKOUT, m, 7);
	CHECK(parse(p_ko) == EINVAL, "7-byte kickout");
}

static void
framing(void)
{
	struct wmi_ev e;
	struct wmi_tlv_it it;
	const uint8_t *v;
	size_t n;
	unsigned t;
	uint8_t a[4] = { 1, 2, 3, 4 };

	CHECK(wmi_event(ev, 3, &e) == EINVAL, "3-byte event");
	ev_begin(0xff003001);
	CHECK(wmi_event(ev, evlen, &e) == 0 && e.id == WMI_EV_SCAN &&
	    e.len == 0, "id masked, empty body");
	ev_tlv(0x11, a, 4);
	ev_tlv(0x10, a, 0);
	ev_tlv(0x11, a, 2);
	wmi_event(ev, evlen, &e);
	CHECK(wmi_tlv_find(&e, 0x11, 1, &v, &n) == 1 && n == 2 &&
	    v == ev + 4 + 8 + 4 + 4, "second array byte");
	CHECK(wmi_tlv_find(&e, 0x11, 2, &v, &n) == 0, "no third");
	CHECK(wmi_tlv_find(&e, 0x10, 0, &v, &n) == 1 && n == 0, "empty TLV");
	/* The length comes first: a reversed header is no TLV. */
	ev_begin(WMI_EV_SCAN);
	ev[4] = 0x24; ev[5] = 0; ev[6] = 24; ev[7] = 0;	/* tag, len swapped */
	memset(ev + 8, 0, 24);
	evlen = 32;
	wmi_event(ev, evlen, &e);
	CHECK(wmi_scan_event(&e, &g_scan) == EINVAL,
	    "tag-first header taken");
	/* Three stray bytes after the TLVs. */
	ev_begin(WMI_EV_SCAN);
	ev_tlv(0x11, a, 4);
	evlen += 3;
	wmi_event(ev, evlen, &e);
	CHECK(wmi_tlv_find(&e, 0x11, 0, &v, &n) == -1, "stray tail taken");
	wmi_tlv_begin(&it, ev + 4, evlen - 4);
	CHECK(wmi_tlv_next(&it, &t, &v, &n) == 1 && t == 0x11 &&
	    wmi_tlv_next(&it, &t, &v, &n) == -1, "iterator on a stray tail");
}

int
main(int argc, char **argv)
{

	commands();
	refusals();
	framing();
	service_ready(argc > 1 ? argv[1] : NULL);
	service_available();
	ready();
	scan_events();
	mgmt_rx();
	vdev_start_resp();
	tx_completions();
	keys_and_kickout();
	printf("wmitest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
