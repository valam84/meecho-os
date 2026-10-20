/*
 * Host stand for minix/drivers/net/wcn/wcn_htt.c.  The expected bytes are
 * written out by hand from the offsets a compiler gave for Linux ath10k's
 * declarations (htt.h, rx_desc.h; computed outside the tree, only the
 * numbers are here) - not from the code under test.  Parsers get crafted
 * messages, then every shorter copy of each in a buffer of exactly that
 * size, so a read past the end is an ASan report, not luck.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_htt.h"

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

/* hex string to bytes; spaces and '|' are separators */
static size_t
unhex(const char *s, uint8_t *out, size_t cap)
{
	size_t k = 0;
	unsigned v;

	while (*s) {
		while (*s == ' ' || *s == '|')
			s++;
		if (!*s)
			break;
		if (k == cap || sscanf(s, "%2x", &v) != 1)
			abort();
		out[k++] = v;
		s += 2;
	}
	return k;
}

static int
same(const uint8_t *got, size_t n, const char *hex, const char *what)
{
	uint8_t want[256];
	size_t k = unhex(hex, want, sizeof(want)), i;

	if (n == k && memcmp(got, want, k) == 0)
		return 1;
	printf("FAIL %s:\n  got  ", what);
	for (i = 0; i < n; i++)
		printf("%02x ", got[i]);
	printf("\n  want ");
	for (i = 0; i < k; i++)
		printf("%02x ", want[i]);
	printf("\n");
	return 0;
}

/* A copy of n bytes in a buffer of exactly n bytes. */
static uint8_t *
exact(const uint8_t *m, size_t n)
{
	uint8_t *p = malloc(n ? n : 1);

	if (p == NULL)
		abort();
	memcpy(p, m, n);
	return p;
}

/* ---- builders ---- */

#define BANK	0x0000012345678000ull

static void
builders(void)
{
	uint8_t b[256], *e;
	size_t n, k;
	struct htt_tx t;
	uint8_t frame[100];
	unsigned i;

	n = htt_version_req(b, sizeof(b));
	CHECK(same(b, n, "00 00 00 00", "VERSION_REQ"), "VERSION_REQ");

	n = htt_frag_desc_bank(b, sizeof(b), BANK, HTT_MAX_PENDING_TX);
	CHECK(same(b, n,
	    "06 00 01 48"
	    "| 00 80 67 45 23 01 00 00 | 00 00 00 00 00 00 00 00"
	    "  00 00 00 00 00 00 00 00 | 00 00 00 00 00 00 00 00"
	    "| 00 00 1f 04 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00"
	    "| 00 00 00 00 | 00 00 | 00 00 | 01 | 00 | 00 00",
	    "FRAG_DESC_BANK_CFG"), "FRAG_DESC_BANK_CFG");

	/* The table of htc-wmi.md, 3.3, item 3. */
	n = htt_rx_ring_cfg(b, sizeof(b), 0x8a001000, 0x18a002000ull, 2048,
	    2047);
	CHECK(same(b, n,
	    "02 01 00 00 | 00 10 00 8a 00 00 00 00 | 00 20 00 8a 01 00 00 00"
	    "| 00 08 | 00 08 | ff ff | ff 07"
	    "| 4a 00 5a 00 1b 00 25 00 04 00 1a 00 07 00 0c 00 01 00 02 00",
	    "RX_RING_CFG"), "RX_RING_CFG");

	n = htt_aggr_cfg(b, sizeof(b), 64, 3);
	CHECK(same(b, n, "05 40 03 00", "AGGR_CFG"), "AGGR_CFG");
	CHECK(htt_aggr_cfg(b, sizeof(b), 65, 3) == 0, "ampdu 65");
	CHECK(htt_aggr_cfg(b, sizeof(b), 64, 32) == 0, "amsdu 32");
	CHECK(htt_aggr_cfg(b, sizeof(b), 0, 3) == 0, "ampdu 0");
	CHECK(htt_rx_ring_cfg(b, sizeof(b), 1, 2, 2048, 2048) == 0,
	    "index past the ring");
	CHECK(htt_rx_ring_cfg(b, sizeof(b), 1, 2, 4096, 0) == 0,
	    "ring too long");
	CHECK(htt_frag_desc_bank(b, sizeof(b), BANK, 0) == 0, "no descs");

	for (i = 0; i < sizeof(frame); i++)
		frame[i] = 0x80 + i;

	/* EAPOL, native wifi, non-QoS, before the keys. */
	memset(&t, 0, sizeof(t));
	t.pkt = HTT_PKT_NWIFI;
	t.vdev = 0;
	t.tid = HTT_TID_NONQOS;
	t.id = 5;
	t.len = 100;
	t.frags_paddr = htt_frag_paddr(BANK, 5);
	t.no_encrypt = 1;
	CHECK(t.frags_paddr == BANK + 360, "frag paddr of id 5");
	n = htt_tx_frm(b, sizeof(b), &t, NULL);
	CHECK(same(b, n, "01 25 00 0c 64 00 05 00 68 81 67 45 23 01 00 00"
	    " ff ff 00 00", "TX_FRM eapol"), "TX_FRM eapol");

	/* QoS data TID 6 on vdev 1, checksum offload, prefetch attached. */
	t.vdev = 1;
	t.tid = 6;
	t.no_encrypt = 0;
	t.csum = 1;
	t.id = 0x41f;
	t.frags_paddr = htt_frag_paddr(BANK, 0x41f);
	n = htt_tx_frm(b, sizeof(b), &t, frame);
	CHECK(n == 20 + 52, "TX_FRM with prefetch: %zu", n);
	CHECK(same(b, 20, "01 21 81 69 64 00 1f 04 b8 a8 68 45 23 01 00 00"
	    " ff ff 00 00", "TX_FRM qos"), "TX_FRM qos");
	CHECK(memcmp(b + 20, frame, 52) == 0, "prefetch bytes");

	/* A short frame: prefetch rounded up to 4, zeros past the end. */
	t.len = 30;
	n = htt_tx_frm(b, sizeof(b), &t, frame);
	CHECK(n == 20 + 32 && memcmp(b + 20, frame, 30) == 0 &&
	    b[50] == 0 && b[51] == 0, "short prefetch %zu", n);
	CHECK(htt_tx_prefetch(1) == 4 && htt_tx_prefetch(50) == 52 &&
	    htt_tx_prefetch(2000) == 52, "prefetch length");

	/* Management through HTT: the frame itself, TID 17, off channel. */
	memset(&t, 0, sizeof(t));
	t.pkt = HTT_PKT_MGMT;
	t.tid = HTT_TID_MGMT;
	t.id = 0;
	t.len = 0x1f0;
	t.frags_paddr = 0x9abc0040;
	t.freq = 2412;
	n = htt_tx_frm(b, sizeof(b), &t, NULL);
	CHECK(same(b, n, "01 61 40 0c f0 01 00 00 40 00 bc 9a 00 00 00 00"
	    " ff ff 6c 09", "TX_FRM mgmt"), "TX_FRM mgmt");

	/* Ethernet: no 802.11 header present. */
	t.pkt = HTT_PKT_ETH;
	t.tid = 0;
	t.freq = 0;
	n = htt_tx_frm(b, sizeof(b), &t, NULL);
	CHECK(n == 20 && b[1] == 0x40, "TX_FRM eth flags0 %02x", b[1]);

	t.pkt = 4;
	CHECK(htt_tx_frm(b, sizeof(b), &t, NULL) == 0, "pkt type 4");
	t.pkt = HTT_PKT_NWIFI;
	t.vdev = 64;
	CHECK(htt_tx_frm(b, sizeof(b), &t, NULL) == 0, "vdev 64");
	t.vdev = 0;
	t.tid = 32;
	CHECK(htt_tx_frm(b, sizeof(b), &t, NULL) == 0, "tid 32");
	t.tid = 0;
	t.len = 0;
	CHECK(htt_tx_frm(b, sizeof(b), &t, NULL) == 0, "length 0");

	/* The fragment descriptor. */
	CHECK(htt_frag_desc(b, sizeof(b), 0x123456780ull, 1500, 0) == 0,
	    "frag desc");
	CHECK(same(b, 72,
	    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
	    "| 00 00 | 00 | 00"
	    "| 80 67 45 23 01 00 dc 05 | 00 00 00 00 00 00 00 00"
	    "| 00 00 00 00 00 00 00 00 | 00 00 00 00 00 00 00 00"
	    "| 00 00 00 00 00 00 00 00 | 00 00 00 00 00 00 00 00",
	    "frag desc"), "frag desc bytes");
	CHECK(htt_frag_desc(b, sizeof(b), 0x123456780ull, 1500, 1) == 0 &&
	    b[12] == 0 && b[13] == 0 && b[14] == 0x1f && b[15] == 0,
	    "frag desc checksum");
	CHECK(htt_frag_desc(b, sizeof(b), 1ull << 48, 1500, 0) == EINVAL,
	    "address past 48 bits");

	/* Native wifi header, to the AP, protected. */
	{
		static const uint8_t bss[6] = { 0xb0, 1, 2, 3, 4, 5 };
		static const uint8_t me[6] = { 0x8a, 0x6e, 0x41, 9, 0x8e, 0x2f };
		static const uint8_t da[6] = { 0xd0, 1, 2, 3, 4, 5 };

		n = htt_nwifi_hdr(b, sizeof(b), bss, me, da, 0x888e, 1);
		CHECK(same(b, n, "08 41 00 00 b0 01 02 03 04 05"
		    " 8a 6e 41 09 8e 2f d0 01 02 03 04 05 00 00"
		    " aa aa 03 00 00 00 88 8e", "nwifi header"), "nwifi");
		n = htt_nwifi_hdr(b, sizeof(b), bss, me, da, 0x0800, 0);
		CHECK(n == 32 && b[1] == 0x01 && b[30] == 8 && b[31] == 0,
		    "nwifi unprotected");
		/* Every short buffer refused, nothing written past it. */
		for (k = 0; k < 32; k++) {
			e = malloc(k ? k : 1);
			CHECK(htt_nwifi_hdr(e, k, bss, me, da, 1, 0) == 0,
			    "nwifi cap %zu", k);
			free(e);
		}
	}

	/* Each builder against every short buffer, exact-size heap. */
	for (k = 0; k < 72; k++) {
		e = malloc(k ? k : 1);
		if (k < 4)
			CHECK(htt_version_req(e, k) == 0, "ver cap %zu", k);
		if (k < 64)
			CHECK(htt_frag_desc_bank(e, k, BANK, 1) == 0,
			    "bank cap %zu", k);
		if (k < 48)
			CHECK(htt_rx_ring_cfg(e, k, 1, 2, 8, 0) == 0,
			    "ring cap %zu", k);
		if (k < 4)
			CHECK(htt_aggr_cfg(e, k, 1, 1) == 0, "aggr cap %zu", k);
		t.pkt = HTT_PKT_NWIFI;
		t.len = 100;
		t.frags_paddr = 1;
		if (k < 20)
			CHECK(htt_tx_frm(e, k, &t, NULL) == 0, "tx cap %zu", k);
		else
			CHECK(htt_tx_frm(e, k, &t, frame) == 0,
			    "tx+prefetch cap %zu", k);
		CHECK(htt_frag_desc(e, k, 1, 1, 0) == EINVAL, "frag cap %zu",
		    k);
		free(e);
	}
}

/* ---- parsers ---- */

/*
 * Feed m[0..n) and every prefix: below min the parser must refuse; the
 * check function sees the full message.  ASan polices the reads.
 */
typedef int (*parser)(const uint8_t *, size_t);

static void
prefixes(const uint8_t *m, size_t n, size_t min, parser f, const char *what)
{
	size_t k;
	uint8_t *e;
	int r;

	for (k = 0; k <= n; k++) {
		e = exact(m, k);
		r = f(e, k);
		if (k < min)
			CHECK(r == EINVAL, "%s: %zu bytes accepted", what, k);
		else
			CHECK(r == 0, "%s: %zu bytes refused (%d)", what, k, r);
		free(e);
	}
}

static int
p_ver(const uint8_t *m, size_t n)
{
	unsigned a, b;

	return htt_version_conf(m, n, &a, &b);
}

static int
p_map(const uint8_t *m, size_t n)
{
	struct htt_peer_map pm;

	return htt_peer_map(m, n, &pm);
}

static int
p_unmap(const uint8_t *m, size_t n)
{
	unsigned id;

	return htt_peer_unmap(m, n, &id);
}

static int
p_compl(const uint8_t *m, size_t n)
{
	struct htt_tx_compl c;
	unsigned i, s = 0;
	int r;

	if ((r = htt_tx_compl(m, n, &c)) == 0) {
		for (i = 0; i < c.count; i++)
			s += htt_tx_compl_id(&c, i) + htt_tx_compl_rssi(&c, i);
		(void)s;
	}
	return r;
}

static int
p_inord(const uint8_t *m, size_t n)
{
	struct htt_in_ord io;
	uint64_t pa;
	unsigned i, l, f;
	int r;

	if ((r = htt_in_ord(m, n, &io)) == 0)
		for (i = 0; i < io.count; i++)
			htt_in_ord_msdu(&io, i, &pa, &l, &f);
	return r;
}

static int
p_rxind(const uint8_t *m, size_t n)
{
	struct htt_rx_ind ri;

	return htt_rx_ind(m, n, &ri);
}

static int
p_sec(const uint8_t *m, size_t n)
{
	struct htt_sec_ind s;

	return htt_sec_ind(m, n, &s);
}

static void
parsers(void)
{
	uint8_t m[512];
	size_t n;
	unsigned maj, min, id, l, f, i;
	struct htt_peer_map pm;
	struct htt_tx_compl c;
	struct htt_in_ord io;
	struct htt_rx_ind ri;
	struct htt_sec_ind s;
	struct htt_rx_offload o;
	uint64_t pa;

	memset(m, 0, sizeof(m));
	CHECK(htt_t2h_type(m, 0) == -1, "empty type");

	/* The tablet's answer: htt-ver 3.56. */
	n = unhex("00 38 03 00", m, sizeof(m));
	CHECK(htt_t2h_type(m, n) == HTT_T2H_VERSION_CONF, "type");
	CHECK(htt_version_conf(m, n, &maj, &min) == 0 && maj == 3 &&
	    min == 56, "version %u.%u", maj, min);
	prefixes(m, n, 3, p_ver, "VERSION_CONF");
	m[0] = 1;
	CHECK(htt_version_conf(m, n, &maj, &min) == EINVAL, "wrong type");

	n = unhex("03 02 05 01 b0 01 02 03 04 05 00 00", m, sizeof(m));
	CHECK(htt_peer_map(m, n, &pm) == 0 && pm.vdev == 2 &&
	    pm.peer_id == 0x105 && pm.addr[0] == 0xb0 && pm.addr[5] == 5,
	    "PEER_MAP");
	prefixes(m, n, 10, p_map, "PEER_MAP");

	n = unhex("04 00 05 01", m, sizeof(m));
	CHECK(htt_peer_unmap(m, n, &id) == 0 && id == 0x105, "PEER_UNMAP");
	prefixes(m, n, 4, p_unmap, "PEER_UNMAP");

	/* TX_COMPL_IND without RSSI: NO_ACK, TID 6, three ids. */
	n = unhex("07 32 03 00 | 05 00 06 00 1f 04", m, sizeof(m));
	CHECK(htt_tx_compl(m, n, &c) == 0 && c.status == HTT_TX_NO_ACK &&
	    c.tid == 6 && !c.tid_invalid && c.count == 3 && c.rssi == NULL &&
	    htt_tx_compl_id(&c, 0) == 5 && htt_tx_compl_id(&c, 2) == 0x41f &&
	    htt_tx_compl_id(&c, 3) == HTT_INVALID_MSDU, "TX_COMPL plain");
	prefixes(m, n, n, p_compl, "TX_COMPL plain");

	/*
	 * With RSSI (flags2 bit 3), an odd count padded with ffff, retries
	 * (bit 0) and timestamp (bit 1) records, then three RSSI.
	 */
	n = unhex("07 80 03 0b | 05 00 06 00 07 00 ff ff | 05 00 02 00"
	    "| 44 33 22 11 | 2a 00 2b 00 2c 00", m, sizeof(m));
	CHECK(htt_tx_compl(m, n, &c) == 0 && c.status == HTT_TX_OK &&
	    c.tid_invalid && c.count == 3 && c.rssi != NULL &&
	    htt_tx_compl_rssi(&c, 0) == 0x2a &&
	    htt_tx_compl_rssi(&c, 2) == 0x2c && htt_tx_compl_rssi(&c, 3) == -1,
	    "TX_COMPL rssi");
	prefixes(m, n, 10, p_compl, "TX_COMPL rssi");
	/* Cut inside the RSSI: ids still delivered, RSSI absent. */
	CHECK(htt_tx_compl(m, n - 1, &c) == 0 && c.rssi == NULL &&
	    htt_tx_compl_id(&c, 2) == 7, "TX_COMPL rssi cut");
	/* Even count, RSSI only. */
	n = unhex("07 00 02 08 | 01 00 02 00 | 30 00 31 00", m, sizeof(m));
	CHECK(htt_tx_compl(m, n, &c) == 0 &&
	    htt_tx_compl_rssi(&c, 0) == 0x30 &&
	    htt_tx_compl_rssi(&c, 1) == 0x31, "TX_COMPL even rssi");
	/* 255 ids promised, 3 given. */
	n = unhex("07 00 ff 00 | 01 00 02 00 03 00", m, sizeof(m));
	CHECK(htt_tx_compl(m, n, &c) == EINVAL, "TX_COMPL overlong count");

	/* RX_IN_ORD_PADDR_IND: TID 3, frag, peer 0x105, vdev 0, two. */
	n = unhex("12 43 05 01 00 00 02 00"
	    "| 00 08 00 9a 01 00 00 00 | 5c 00 | 81 | 00"
	    "| 00 10 00 9a 01 00 00 00 | 98 06 | 02 | 00", m, sizeof(m));
	CHECK(htt_in_ord(m, n, &io) == 0 && io.tid == 3 && !io.offload &&
	    io.frag && io.peer_id == 0x105 && io.vdev == 0 && io.count == 2,
	    "IN_ORD header");
	htt_in_ord_msdu(&io, 0, &pa, &l, &f);
	CHECK(pa == 0x19a000800ull && l == 0x5c && f == 0x81, "IN_ORD msdu 0");
	htt_in_ord_msdu(&io, 1, &pa, &l, &f);
	CHECK(pa == 0x19a001000ull && l == 1688 && f == 2, "IN_ORD msdu 1");
	htt_in_ord_msdu(&io, 2, &pa, &l, &f);
	CHECK(pa == 0 && l == 0, "IN_ORD past the count");
	prefixes(m, n, n, p_inord, "IN_ORD");
	m[1] = 0x20;
	CHECK(htt_in_ord(m, n, &io) == 0 && io.offload && io.tid == 0,
	    "IN_ORD offload");
	n = unhex("12 00 00 00 00 00 00 00", m, sizeof(m));
	CHECK(htt_in_ord(m, n, &io) == 0 && io.count == 0, "IN_ORD empty");
	/* Records of the 32-bit form (8 bytes): too short for 64-bit. */
	n = unhex("12 00 00 00 00 00 02 00 | 00 08 00 9a 5c 00 81 00"
	    "| 00 10 00 9a 98 06 02 00", m, sizeof(m));
	CHECK(htt_in_ord(m, n, &io) == EINVAL, "IN_ORD 32-bit records");

	/* RX_IND: TID 2, release valid, peer 7, one fw desc, two ranges. */
	memset(m, 0, sizeof(m));
	m[0] = 0x01;
	m[1] = 0x42;
	m[2] = 7;
	/* info1: flush 1..2, release 3..4, 2 ranges */
	{
		uint32_t i1 = 1 | 2 << 6 | 3 << 12 | 4 << 18 | 2u << 24;

		m[4] = i1;
		m[5] = i1 >> 8;
		m[6] = i1 >> 16;
		m[7] = i1 >> 24;
	}
	m[44] = 3;		/* three descriptor bytes */
	m[48] = 0xa0;
	m[49] = 0xa1;
	m[50] = 0xa2;
	m[52] = 2;		/* range 0: 2 mpdus, ok */
	m[53] = 1;
	m[56] = 5;		/* range 1: 5 mpdus, status 2 */
	m[57] = 2;
	n = 60;
	CHECK(htt_rx_ind(m, n, &ri) == 0 && ri.tid == 2 && !ri.flush_valid &&
	    ri.release_valid && ri.peer_id == 7 && ri.flush_start == 1 &&
	    ri.flush_end == 2 && ri.release_start == 3 &&
	    ri.release_end == 4 && ri.nranges == 2 && ri.fw_desc_len == 3 &&
	    ri.fw_desc[2] == 0xa2 && ri.ranges[0] == 2 && ri.ranges[5] == 2 &&
	    ri.mpdus == 7, "RX_IND");
	prefixes(m, n, n, p_rxind, "RX_IND");
	m[44] = 0xff;
	m[45] = 0xff;
	CHECK(htt_rx_ind(m, n, &ri) == EINVAL, "RX_IND huge fw desc");

	/* SEC_IND: CCMP, unicast, peer 0x105. */
	n = unhex("0b 86 05 01 | 01 02 03 04 05 06 07 08"
	    "| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", m, sizeof(m));
	CHECK(htt_sec_ind(m, n, &s) == 0 && s.type == HTT_SEC_CCMP &&
	    s.unicast && s.peer_id == 0x105 && s.michael[7] == 8, "SEC_IND");
	prefixes(m, n, 12, p_sec, "SEC_IND");

	/* Offload header. */
	n = unhex("04 00 05 01 01 02 7f | de ad be ef", m, sizeof(m));
	CHECK(htt_rx_offload(m, n, &o) == 0 && o.len == 4 && o.peer_id == 0x105
	    && o.vdev == 1 && o.tid == 2 && o.fw_desc == 0x7f && o.off == 7,
	    "offload");
	for (i = 0; i < n; i++) {
		uint8_t *e = exact(m, i);

		CHECK(htt_rx_offload(e, i, &o) == EINVAL, "offload %u", i);
		free(e);
	}

	/* Every type byte against every parser: only its own accepted. */
	memset(m, 0, sizeof(m));
	for (i = 0; i < 256; i++) {
		m[0] = i;
		CHECK((p_ver(m, 64) == 0) == (i == HTT_T2H_VERSION_CONF) &&
		    (p_map(m, 64) == 0) == (i == HTT_T2H_PEER_MAP) &&
		    (p_unmap(m, 64) == 0) == (i == HTT_T2H_PEER_UNMAP) &&
		    (p_compl(m, 64) == 0) == (i == HTT_T2H_TX_COMPL_IND) &&
		    (p_inord(m, 64) == 0) == (i == HTT_T2H_RX_IN_ORD_PADDR) &&
		    (p_rxind(m, 64) == 0) == (i == HTT_T2H_RX_IND) &&
		    (p_sec(m, 64) == 0) == (i == HTT_T2H_SEC_IND),
		    "type %u", i);
	}
}

/* ---- the rx descriptor ---- */

static void
put32(uint8_t *p, uint32_t v)
{

	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/*
 * A buffer as the firmware leaves it: descriptor offsets from rx_desc.h
 * by a compiler - attention 4, frag info 8, mpdu start 16 (info1 24),
 * msdu start 28 (info1 36), msdu end 48 (info0 64, this chip's info1 80),
 * mpdu end 104, rx_hdr_status 296, payload 360.
 */
static void
mkbuf(uint8_t *b, uint32_t att, unsigned len, unsigned decap)
{

	memset(b, 0, HTT_RX_BUF_SIZE);
	b[0] = 0x20;
	put32(b + 4, att);
	b[8 + 2] = 0;
	/* peer 0x123, from DS, encrypted, seq 0xabc, CCMP */
	put32(b + 16, 0x123 | 1u << 11 | 1u << 13 | 0xabcu << 16 | 6u << 28);
	put32(b + 24, 5u << 28);			/* TID 5 */
	put32(b + 28, len | 0x15u << 14);		/* + ip offset */
	put32(b + 36, 3 | decap << 8 | 1u << 10);	/* msdu 3, ipv4 */
	put32(b + 64, 0x1ff | 1u << 14 | 1u << 15);	/* first, last */
	put32(b + 80, 2u << 10);			/* l3 pad field 2 */
	put32(b + 104, 0x5a5a);
	b[296] = 0x88;
	b[297] = 0x42;
}

static void
rxdesc(void)
{
	uint8_t b[HTT_RX_BUF_SIZE], *e;
	struct htt_rxd d;
	size_t off, len, rest, k;

	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_FIRST_MPDU | HTT_ATT_LAST_MPDU,
	    1500, HTT_DECAP_NWIFI);
	CHECK(htt_rxd_parse(b, sizeof(b), &d) == 0, "parse");
	CHECK(d.fw_desc == 0x20 && d.peer_idx == 0x123 && d.from_ds &&
	    !d.to_ds && d.encrypted && d.seq == 0xabc && d.enc == HTT_ENC_CCMP
	    && d.tid == 5, "mpdu_start fields");
	CHECK(d.msdu_len == 1500 && d.decap == HTT_DECAP_NWIFI &&
	    d.msdu_num == 3, "msdu_start fields: len %u decap %u",
	    d.msdu_len, d.decap);
	CHECK(d.first_msdu && d.last_msdu && d.l3pad_field == 2,
	    "msdu_end fields");
	CHECK(d.mpdu_end == 0x5a5a && d.hdr[0] == 0x88 && d.hdr == b + 296,
	    "mpdu_end and header");
	CHECK(d.decrypted && !htt_rxd_bad(&d), "decrypted, good");

	CHECK(htt_rxd_payload(&d, sizeof(b), HTT_LEN_DESC, &off, &len,
	    &rest) == 0 && off == 360 && len == 1500 && rest == 0,
	    "payload from desc %zu %zu %zu", off, len, rest);
	CHECK(htt_rxd_payload(&d, sizeof(b), 92, &off, &len, &rest) == 0 &&
	    off == 360 && len == 92 && rest == 0, "payload from indication");
	/* Longer than the buffer: the rest goes to chained buffers. */
	CHECK(htt_rxd_payload(&d, sizeof(b), 3000, &off, &len, &rest) == 0 &&
	    len == 1688 && rest == 3000 - 1688, "chained");
	/* A smaller buffer limits it too. */
	CHECK(htt_rxd_payload(&d, 400, HTT_LEN_DESC, &off, &len, &rest) == 0
	    && len == 40 && rest == 1460, "short buffer");

	/* Length error: ath10k takes 0. */
	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_MSDU_LEN_ERR, 1500, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(htt_rxd_payload(&d, sizeof(b), HTT_LEN_DESC, &off, &len,
	    &rest) == 0 && len == 0 && rest == 0, "length error");

	/* Not done: EIO, and bad. */
	mkbuf(b, 0, 1500, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(htt_rxd_payload(&d, sizeof(b), 10, &off, &len, &rest) == EIO,
	    "not done");
	CHECK(htt_rxd_bad(&d), "not done is bad");

	/* Each error bit alone makes it bad; FCS and cipher undo decrypted. */
	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_FCS_ERR, 100, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(htt_rxd_bad(&d) && !d.decrypted, "fcs");
	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_DECRYPT_ERR, 100, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(htt_rxd_bad(&d) && !d.decrypted, "decrypt");
	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_TKIP_MIC_ERR, 100, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(htt_rxd_bad(&d) && d.decrypted, "mic");
	mkbuf(b, HTT_ATT_MSDU_DONE | HTT_ATT_PEER_INVALID, 100, 1);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(!htt_rxd_bad(&d) && !d.decrypted, "peer invalid");
	/* No cipher: EAPOL before the keys. */
	mkbuf(b, HTT_ATT_MSDU_DONE, 100, 1);
	put32(b + 16, 7u << 28);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(d.enc == HTT_ENC_NONE && !d.encrypted && !d.decrypted &&
	    !htt_rxd_bad(&d), "no cipher");
	/* Chained buffers and decap formats. */
	b[10] = 2;
	put32(b + 36, 3u << 8);
	put32(b + 64, 1u << 14);
	htt_rxd_parse(b, sizeof(b), &d);
	CHECK(d.chained == 2 && d.decap == HTT_DECAP_8023 && d.first_msdu &&
	    !d.last_msdu, "chained, 802.3, more msdus");

	for (k = 0; k < HTT_RX_DESC_SIZE; k++) {
		e = exact(b, k);
		CHECK(htt_rxd_parse(e, k, &d) == EINVAL, "desc of %zu", k);
		free(e);
	}
	e = exact(b, HTT_RX_DESC_SIZE);
	CHECK(htt_rxd_parse(e, HTT_RX_DESC_SIZE, &d) == 0, "desc alone");
	CHECK(htt_rxd_payload(&d, HTT_RX_DESC_SIZE, 0, &off, &len, &rest) == 0
	    && len == 0, "desc alone, payload 0");
	free(e);
}

/* ---- native wifi to Ethernet ---- */

static void
nwifi(void)
{
	uint8_t f[128], *e;
	size_t n, k;
	long at;
	static const char *from_ap =
	    /* data, from DS: addr1 me, addr2 bssid, addr3 the source */
	    "08 02 00 00 | 8a 6e 41 09 8e 2f | b0 01 02 03 04 05"
	    "| 5a 01 02 03 04 05 | 10 00"
	    "| aa aa 03 00 00 00 08 00 | 45 00 00 14";

	n = unhex(from_ap, f, sizeof(f));
	at = htt_nwifi_to_eth(f, n);
	CHECK(at == 18 && same(f + at, n - at, "8a 6e 41 09 8e 2f"
	    " 5a 01 02 03 04 05 08 00 45 00 00 14", "eth from ap"),
	    "from AP: %ld", at);

	/* Bridge tunnel SNAP too. */
	n = unhex(from_ap, f, sizeof(f));
	f[29] = 0xf8;
	CHECK(htt_nwifi_to_eth(f, n) == 18, "bridge tunnel");

	/* No SNAP: 802.3 with a length. */
	n = unhex("08 02 00 00 | 8a 6e 41 09 8e 2f | b0 01 02 03 04 05"
	    "| 5a 01 02 03 04 05 | 10 00 | 42 42 03 01 02", f, sizeof(f));
	at = htt_nwifi_to_eth(f, n);
	CHECK(at == 10 && same(f + at, n - at, "8a 6e 41 09 8e 2f"
	    " 5a 01 02 03 04 05 00 05 42 42 03 01 02", "802.3"), "802.3");

	/* To DS (what we send, looped): DA is addr3, SA addr2. */
	n = unhex("08 01 00 00 | b0 01 02 03 04 05 | 8a 6e 41 09 8e 2f"
	    "| d0 01 02 03 04 05 | 00 00 | aa aa 03 00 00 00 88 8e | 01",
	    f, sizeof(f));
	at = htt_nwifi_to_eth(f, n);
	CHECK(at == 18 && same(f + at, n - at, "d0 01 02 03 04 05"
	    " 8a 6e 41 09 8e 2f 88 8e 01", "to ds"), "to DS");

	/* Four addresses, QoS: header 32, SA addr4. */
	n = unhex("88 03 00 00 | 11 11 11 11 11 11 | 22 22 22 22 22 22"
	    "| 33 33 33 33 33 33 | 00 00 | 44 44 44 44 44 44 | 05 00"
	    "| aa aa 03 00 00 00 08 06", f, sizeof(f));
	at = htt_nwifi_to_eth(f, n);
	CHECK(at == 26 && same(f + at, n - at, "33 33 33 33 33 33"
	    " 44 44 44 44 44 44 08 06", "4addr"), "4addr qos: %ld", at);

	/* Management is not data. */
	n = unhex("80 00 00 00 | 8a 6e 41 09 8e 2f | b0 01 02 03 04 05"
	    "| 5a 01 02 03 04 05 | 10 00", f, sizeof(f));
	CHECK(htt_nwifi_to_eth(f, n) == -1, "mgmt refused");

	n = unhex(from_ap, f, sizeof(f));
	for (k = 0; k < 24; k++) {
		e = exact(f, k);
		CHECK(htt_nwifi_to_eth(e, k) == -1, "nwifi %zu", k);
		free(e);
	}
	for (k = 24; k <= n; k++) {
		e = exact(f, k);
		CHECK(htt_nwifi_to_eth(e, k) >= 0, "nwifi %zu", k);
		free(e);
	}
}

/* ---- the rx ring ---- */

static uint64_t
le64(const uint8_t *p)
{
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

static uint32_t rnd = 12345;

static uint32_t
next(void)
{

	rnd = rnd * 1103515245 + 12345;
	return rnd >> 8;
}

static struct htt_ring r;
static uint8_t ring[8 * HTT_RX_RING_SIZE];
static uint64_t pa[4096];

static void
ringtest(void)
{
	unsigned i, j, bad, round;
	uint32_t tag;
	uint64_t p;
	unsigned order[HTT_RX_RING_FILL];

	CHECK(htt_ring_init(&r, 1000, 10, 1) == EINVAL, "size not 2^n");
	CHECK(htt_ring_init(&r, 4096, 10, 1) == EINVAL, "size too big");
	CHECK(htt_ring_init(&r, 16, 16, 1) == EINVAL, "level = size");

	/*
	 * Buffer addresses 2 KiB apart with the same low bits, the way a
	 * pool hands them out - the worst case for a hash on low bits.
	 */
	for (i = 0; i < 4096; i++)
		pa[i] = 0x180000000ull + (uint64_t)i * 2048 + 8;

	CHECK(htt_ring_init(&r, HTT_RX_RING_SIZE, HTT_RX_RING_FILL, 1) == 0,
	    "init");
	CHECK(htt_ring_deficit(&r) == 2047, "deficit at start");
	for (i = 0; i < HTT_RX_RING_FILL; i++)
		if (htt_ring_post(&r, ring, pa[i], i) != 0)
			break;
	CHECK(i == HTT_RX_RING_FILL, "posted %u", i);
	CHECK(htt_ring_index(&r) == 2047 && htt_ring_deficit(&r) == 0,
	    "index %u", htt_ring_index(&r));
	CHECK(htt_ring_post(&r, ring, pa[3000], 3000) == ENOSPC, "full");
	for (bad = 0, i = 0; i < HTT_RX_RING_FILL; i++)
		bad += le64(ring + 8 * i) != pa[i];
	CHECK(bad == 0, "ring entries: %u wrong", bad);

	/* Take them back by address in random order. */
	for (i = 0; i < HTT_RX_RING_FILL; i++)
		order[i] = i;
	for (i = HTT_RX_RING_FILL - 1; i > 0; i--) {
		j = next() % (i + 1);
		tag = order[i];
		order[i] = order[j];
		order[j] = tag;
	}
	for (bad = 0, round = 0; round < 3; round++) {
		/* half out, check the rest still found, refill, again */
		for (i = 0; i < 1000; i++) {
			j = order[(i + round * 677) % HTT_RX_RING_FILL];
			if (htt_ring_take(&r, pa[j], &tag) != 0 || tag != j)
				bad++;
			CHECK(htt_ring_take(&r, pa[j], &tag) == ENOENT,
			    "taken twice");
		}
		CHECK(htt_ring_deficit(&r) == 1000, "deficit %u",
		    htt_ring_deficit(&r));
		for (i = 0; i < 1000; i++) {
			j = order[(i + round * 677) % HTT_RX_RING_FILL];
			if (htt_ring_post(&r, ring, pa[j], j) != 0)
				bad++;
		}
		/* The index went round the ring. */
		CHECK(htt_ring_index(&r) == (2047 + 1000 * (round + 1)) % 2048,
		    "index after round %u: %u", round, htt_ring_index(&r));
	}
	CHECK(bad == 0, "take/repost: %u wrong", bad);
	CHECK(htt_ring_post(&r, ring, pa[order[5]], 1) == ENOSPC ||
	    htt_ring_post(&r, ring, pa[order[5]], 1) == EEXIST, "dup");
	CHECK(htt_ring_take(&r, 0x7777000, &tag) == ENOENT, "stranger");
	for (bad = 0, i = 0; i < HTT_RX_RING_FILL; i++)
		if (htt_ring_take(&r, pa[i], &tag) != 0 || tag != i)
			bad++;
	CHECK(bad == 0 && htt_ring_deficit(&r) == 2047, "all back: %u", bad);
	CHECK(htt_ring_post(&r, ring, pa[0], 0) == 0 &&
	    htt_ring_post(&r, ring, pa[0], 0) == EEXIST, "duplicate address");
	CHECK(htt_ring_post(&r, ring, 0, 0) == ENOSPC, "address 0");
	CHECK(htt_ring_index_ok(&r, 2047) && !htt_ring_index_ok(&r, 2048),
	    "index check");

	/* Ring order (RX_IND): buffers come back as posted, entries zeroed. */
	CHECK(htt_ring_init(&r, 8, 7, 0) == 0, "init small");
	for (i = 0; i < 7; i++)
		CHECK(htt_ring_post(&r, ring, pa[100 + i], i) == 0, "post %u",
		    i);
	CHECK(htt_ring_post(&r, ring, pa[200], 9) == ENOSPC, "small full");
	for (round = 0; round < 20; round++) {
		CHECK(htt_ring_next(&r, ring, &p, &tag) == 0 &&
		    p == pa[100 + tag], "next %u", round);
		CHECK(le64(ring + 8 * ((round) % 8)) == 0, "entry zeroed");
		CHECK(htt_ring_post(&r, ring, p, tag) == 0, "repost %u", round);
	}
	/* Taken by address in ring mode: next skips nothing, says ENOENT. */
	for (i = 0; i < 7; i++)
		CHECK(htt_ring_next(&r, ring, &p, &tag) == 0, "drain %u", i);
	CHECK(htt_ring_next(&r, ring, &p, &tag) == ENOENT, "empty");
	CHECK(htt_ring_post(&r, ring, pa[1], 1) == 0 &&
	    htt_ring_take(&r, pa[1], &tag) == 0 &&
	    htt_ring_next(&r, ring, &p, &tag) == ENOENT, "took by address");
}

int
main(void)
{

	builders();
	parsers();
	rxdesc();
	nwifi();
	ringtest();
	printf("htttest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
