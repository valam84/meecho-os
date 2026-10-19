/*
 * Host stand for minix/drivers/net/wcn/wcn_wlfw.c: every request against
 * the bytes Linux's qmi_encdec puts on the wire for ath10k on this tree
 * (port/clover/wifi/wlfw.md, section 2.3), and the answers - the CAP the
 * tablet's firmware gave (pass1/wifi/dmesg.txt) and answers made to be
 * wrong.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_wlfw.h"

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

static uint8_t mem[WLFW_MSG_MAX];
static struct qmi_buf b = { mem, sizeof(mem), 0, 0 };

#define TXN	0x1234

/* Compare with a hex string; "TT TT" is the transaction id. */
static int
same(size_t n, const char *hex, const char *what)
{
	uint8_t want[1024];
	size_t k = 0;
	unsigned v;
	const char *p = hex;

	while (*p) {
		while (*p == ' ' || *p == '|')
			p++;
		if (!*p)
			break;
		if (p[0] == 'T' && p[1] == 'T') {
			want[k] = k == 1 ? (TXN & 0xff) : TXN >> 8;
			k++;
			p += 2;
			continue;
		}
		if (sscanf(p, "%2x", &v) != 1)
			break;
		want[k++] = v;
		p += 2;
	}
	if (n != k || memcmp(mem, want, k) != 0) {
		size_t i;

		printf("FAIL %s:\n  got ", what);
		for (i = 0; i < n; i++)
			printf("%02x ", mem[i]);
		printf("\n want ");
		for (i = 0; i < k; i++)
			printf("%02x ", want[i]);
		printf("\n");
		return 0;
	}
	return 1;
}

static void
requests(void)
{
	uint8_t seg[WLFW_BDF_SEG + 1];
	size_t n, total = 19152, off, piece;
	uint32_t s;
	unsigned i;

	n = wlfw_ind_register(&b, TXN);
	CHECK(same(n, "00 TT TT 20 00 0f 00 | 10 01 00 01 | 13 01 00 01 |"
	    " 15 04 00 4c 45 4e 4b", "IND_REGISTER"), "IND_REGISTER");
	n = wlfw_host_cap(&b, TXN);
	CHECK(same(n, "00 TT TT 34 00 04 00 | 10 01 00 00", "HOST_CAP"),
	    "HOST_CAP 8-bit");
	n = wlfw_msa_info(&b, TXN, 0x85700000, 0x100000);
	CHECK(same(n, "00 TT TT 2d 00 12 00 | 01 08 00 00 00 70 85 00 00 00 00"
	    " | 02 04 00 00 00 10 00", "MSA_INFO"), "MSA_INFO");
	n = wlfw_msa_ready(&b, TXN);
	CHECK(same(n, "00 TT TT 2e 00 00 00", "MSA_READY"), "MSA_READY");
	n = wlfw_cap(&b, TXN);
	CHECK(same(n, "00 TT TT 24 00 00 00", "CAP"), "CAP");
	n = wlfw_cal_report(&b, TXN);
	CHECK(same(n, "00 TT TT 26 00 04 00 | 01 01 00 00", "CAL_REPORT"),
	    "CAL_REPORT");
	n = wlfw_wlan_mode(&b, TXN, WLFW_MODE_MISSION);
	CHECK(same(n, "00 TT TT 22 00 0b 00 | 01 04 00 00 00 00 00 |"
	    " 10 01 00 00", "WLAN_MODE"), "WLAN_MODE");
	n = wlfw_ini(&b, TXN, 1);
	CHECK(same(n, "00 TT TT 2f 00 04 00 | 10 01 00 01", "INI"), "INI");

	/* WLAN_CFG: msg_len 552; the three arrays with one-byte counts. */
	n = wlfw_wlan_cfg(&b, TXN);
	CHECK(n == 7 + 552 && mem[5] == (552 & 0xff) && mem[6] == 552 >> 8,
	    "WLAN_CFG length %zu", n);
	CHECK(mem[7] == 0x11 && mem[8] == 241 && mem[9] == 0 && mem[10] == 12,
	    "tgt_cfg header");
	CHECK(mem[7 + 3 + 241] == 0x12 && mem[7 + 3 + 241 + 3] == 21,
	    "svc_cfg header");
	/* CE7 in tgt_cfg: pipe 7, dir 4, 0, 0, flags 8. */
	{
		const uint8_t *e = mem + 11 + 7 * 20;
		CHECK(e[0] == 7 && e[4] == 4 && e[8] == 0 && e[12] == 0 &&
		    e[16] == 8, "ce7 entry");
	}
	/* The last svc entry is the zero terminator. */
	{
		const uint8_t *e = mem + 7 + 3 + 241 + 3 + 1 + 20 * 12;
		static const uint8_t z[12];
		CHECK(memcmp(e, z, 12) == 0, "svc terminator");
		e = mem + 7 + 3 + 241 + 3 + 1 + 14 * 12; /* HTT data out */
		CHECK(e[0] == 0x00 && e[1] == 0x03 && e[4] == 2 && e[8] == 4,
		    "htt out entry");
	}
	/* Shadow: (3, 0x3c) second. */
	{
		const uint8_t *e = mem + 7 + 3 + 241 + 3 + 253 + 3 + 1 + 4;
		CHECK(mem[7 + 3 + 241 + 3 + 253] == 0x13 && e[0] == 3 &&
		    e[2] == 0x3c, "shadow entry");
	}

	/* BDF: 19152 bytes in segments of 6144; end=1 on the last only. */
	for (i = 0; i < sizeof(seg); i++)
		seg[i] = i * 13;
	for (off = 0, s = 0; off < total; off += piece, s++) {
		int last;

		piece = total - off > WLFW_BDF_SEG ? WLFW_BDF_SEG : total - off;
		last = off + piece == total;

		n = wlfw_bdf(&b, TXN, total, s, seg, piece, last);
		CHECK(n == 7 + 34 + piece, "bdf %u length %zu", s, n);
		CHECK(mem[7] == 0x01 && mem[11] == 0x10 && mem[18] == 0x11 &&
		    mem[25] == 0x12 && mem[32] == 0x13, "bdf %u order", s);
		CHECK((unsigned)(mem[28] | mem[29] << 8) == s, "bdf seg %u", s);
		CHECK((size_t)(mem[33] | mem[34] << 8) == piece + 2 &&
		    (size_t)(mem[35] | mem[36] << 8) == piece, "bdf %u data count", s);
		CHECK(mem[n - 4] == 0x14 && mem[n - 1] == last, "bdf %u end",
		    s);
	}
	CHECK(s == 4, "segments %u", s);
	CHECK(wlfw_bdf(&b, TXN, 7000, 0, seg, WLFW_BDF_SEG + 1, 0) == 0,
	    "oversized segment built");

	/* Too small a buffer: nothing, not a cut message. */
	{
		uint8_t small[20];
		struct qmi_buf t = { small, sizeof(small), 0, 0 };
		CHECK(wlfw_wlan_cfg(&t, TXN) == 0, "cfg in 20 bytes");
	}
}

/* Build an answer: header then raw TLV bytes. */
static uint8_t ans[1024];

static size_t
answer(uint16_t id, const uint8_t *tlvs, size_t n)
{

	ans[0] = QMI_RESPONSE;
	ans[1] = TXN & 0xff;
	ans[2] = TXN >> 8;
	ans[3] = id;
	ans[4] = id >> 8;
	ans[5] = n;
	ans[6] = n >> 8;
	memcpy(ans + 7, tlvs, n);
	return 7 + n;
}

static void
answers(void)
{
	struct qmi_msg m;
	struct wlfw_cap c;
	struct wlfw_region r[2];
	unsigned res, err, nr;
	uint64_t st;
	uint8_t t[512];
	size_t n, k;
	const char *ts = "2018-08-31 21:42";
	const char *id = "QC_IMAGE_VERSION_STRING=WLAN.HL.1.0.1.c2-00496-"
	    "QCAHLSWMTPLZ-1";

	/* CAP as the tablet's firmware answered (dmesg). */
	k = 0;
	t[k++] = 0x02; t[k++] = 4; t[k++] = 0;
	t[k++] = 0; t[k++] = 0; t[k++] = 0; t[k++] = 0;
	t[k++] = 0x10; t[k++] = 8; t[k++] = 0;
	t[k++] = 0x14; t[k++] = 0x02; t[k++] = 0x03; t[k++] = 0;
	t[k++] = 0x01; t[k++] = 0x40; t[k++] = 0; t[k++] = 0;
	t[k++] = 0x11; t[k++] = 4; t[k++] = 0;
	t[k++] = 0xff; t[k++] = 0; t[k++] = 0; t[k++] = 0;
	t[k++] = 0x12; t[k++] = 4; t[k++] = 0;
	t[k++] = 0x00; t[k++] = 0x00; t[k++] = 0x05; t[k++] = 0x40;
	t[k++] = 0x13; t[k++] = 5 + strlen(ts); t[k++] = 0;
	t[k++] = 0xf0; t[k++] = 0x81; t[k++] = 0x1f; t[k++] = 0x10;
	t[k++] = strlen(ts);
	memcpy(t + k, ts, strlen(ts));
	k += strlen(ts);
	t[k++] = 0x14; t[k++] = strlen(id); t[k++] = 0;
	memcpy(t + k, id, strlen(id));
	k += strlen(id);
	t[k++] = 0x99; t[k++] = 2; t[k++] = 0; t[k++] = 7; t[k++] = 7;
	n = answer(WLFW_CAP, t, k);
	CHECK(qmi_parse(ans, n, &m) == 0 && m.type == QMI_RESPONSE &&
	    m.txn == TXN && m.id == WLFW_CAP, "cap header");
	CHECK(qmi_result(&m, &res, &err) == 0 && res == 0 && err == 0,
	    "cap result");
	CHECK(wlfw_cap_resp(&m, &c) == 0, "cap parse");
	CHECK(c.chip_id == 0x30214 && c.chip_family == 0x4001 &&
	    c.board_id == 0xff && c.soc_id == 0x40050000 &&
	    c.fw_version == 0x101f81f0, "cap numbers %x %x %x %x %x",
	    c.chip_id, c.chip_family, c.board_id, c.soc_id, c.fw_version);
	CHECK(strcmp(c.fw_timestamp, ts) == 0 &&
	    strcmp(c.fw_build_id, id) == 0, "cap strings '%s' '%s'",
	    c.fw_timestamp, c.fw_build_id);

	/* msg_len disagreeing with the datagram. */
	ans[5]++;
	CHECK(qmi_parse(ans, n, &m) == EINVAL, "wrong msg_len taken");
	ans[5]--;
	/* A TLV running past the end. */
	ans[7 + 8]++;
	CHECK(qmi_parse(ans, n, &m) == 0 && wlfw_cap_resp(&m, &c) == EINVAL,
	    "TLV past the end taken");
	ans[7 + 8]--;
	/* A timestamp of 33 bytes: one too long. */
	{
		uint8_t u[64];
		size_t j = 0;
		u[j++] = 0x02; u[j++] = 4; u[j++] = 0;
		u[j++] = 0; u[j++] = 0; u[j++] = 0; u[j++] = 0;
		u[j++] = 0x13; u[j++] = 5 + 33; u[j++] = 0;
		u[j++] = 1; u[j++] = 0; u[j++] = 0; u[j++] = 0; u[j++] = 33;
		memset(u + j, 'x', 33);
		j += 33;
		n = answer(WLFW_CAP, u, j);
		CHECK(qmi_parse(ans, n, &m) == 0 &&
		    wlfw_cap_resp(&m, &c) == EINVAL, "33-byte string taken");
	}
	/* No chip or board info: 0xff, as ath10k. */
	{
		uint8_t u[] = { 0x02, 4, 0, 0, 0, 0, 0 };
		n = answer(WLFW_CAP, u, sizeof(u));
		CHECK(qmi_parse(ans, n, &m) == 0 && wlfw_cap_resp(&m, &c) ==
		    0 && c.board_id == 0xff && c.chip_id == 0xff && !c.has_fw,
		    "bare cap");
	}
	/* An unknown mandatory TLV (0x05): not understood. */
	{
		uint8_t u[] = { 0x02, 4, 0, 0, 0, 0, 0, 0x05, 1, 0, 9 };
		n = answer(WLFW_CAP, u, sizeof(u));
		CHECK(qmi_parse(ans, n, &m) == 0 &&
		    wlfw_cap_resp(&m, &c) == EINVAL, "unknown 0x05 taken");
	}

	/* IND_REGISTER: fw_status with FW_READY. */
	{
		uint8_t u[] = { 0x02, 4, 0, 0, 0, 0, 0,
		    0x10, 8, 0, 0x02, 0, 0, 0, 0, 0, 0, 0 };
		n = answer(WLFW_IND_REGISTER, u, sizeof(u));
		CHECK(qmi_parse(ans, n, &m) == 0 &&
		    wlfw_ind_register_resp(&m, &st) == 0 &&
		    (st & WLFW_FW_STATUS_READY), "fw_status");
	}

	/* MSA_INFO: two regions inside, then one outside, then three. */
	{
		uint8_t u[64];
		size_t j = 0;
		int q;

		u[j++] = 0x02; u[j++] = 4; u[j++] = 0;
		u[j++] = 0; u[j++] = 0; u[j++] = 0; u[j++] = 0;
		u[j++] = 0x03; u[j++] = 1 + 26; u[j++] = 0;
		u[j++] = 2;
		/* 0x85700000, 0x80000, secure 1 */
		u[j++] = 0; u[j++] = 0; u[j++] = 0x70; u[j++] = 0x85;
		u[j++] = 0; u[j++] = 0; u[j++] = 0; u[j++] = 0;
		u[j++] = 0; u[j++] = 0; u[j++] = 0x08; u[j++] = 0;
		u[j++] = 1;
		/* 0x85780000, 0x80000, secure 0 */
		u[j++] = 0; u[j++] = 0; u[j++] = 0x78; u[j++] = 0x85;
		u[j++] = 0; u[j++] = 0; u[j++] = 0; u[j++] = 0;
		u[j++] = 0; u[j++] = 0; u[j++] = 0x08; u[j++] = 0;
		u[j++] = 0;
		n = answer(WLFW_MSA_INFO, u, j);
		q = qmi_parse(ans, n, &m) == 0 ? wlfw_msa_info_resp(&m,
		    0x85700000, 0x100000, r, &nr) : -1;
		CHECK(q == 0 && nr == 2 && r[0].addr == 0x85700000 &&
		    r[1].size == 0x80000 && r[0].secure == 1 &&
		    r[1].secure == 0, "msa regions");
		/* Second region one byte past the end. */
		ans[7 + 11 + 13 + 8] = 1;	/* size 0x80001 */
		CHECK(wlfw_msa_info_resp(&m, 0x85700000, 0x100000, r, &nr) ==
		    EINVAL && nr == 0, "region past the end taken");
		ans[7 + 11 + 13 + 8] = 0;
		/* Below the start. */
		ans[7 + 11 + 2] = 0x6f;	/* 0x856f0000 */
		CHECK(wlfw_msa_info_resp(&m, 0x85700000, 0x100000, r, &nr) ==
		    EINVAL, "region below taken");
		ans[7 + 11 + 2] = 0x70;
		/* A count of three. */
		ans[7 + 10] = 3;
		CHECK(wlfw_msa_info_resp(&m, 0x85700000, 0x100000, r, &nr) ==
		    EINVAL, "three regions taken");
		/* An address near 2^64 must not wrap the check. */
		ans[7 + 10] = 2;
		memset(ans + 7 + 11, 0xff, 8);
		CHECK(wlfw_msa_info_resp(&m, 0x85700000, 0x100000, r, &nr) ==
		    EINVAL, "wrapping region taken");
	}

	/* A failed answer: result 1, error 94 (HOST_CAP not supported). */
	{
		uint8_t u[] = { 0x02, 4, 0, 1, 0, 94, 0 };
		n = answer(WLFW_HOST_CAP, u, sizeof(u));
		CHECK(qmi_parse(ans, n, &m) == 0 && qmi_result(&m, &res, &err)
		    == 0 && res == 1 && err == QMI_ERR_NOT_SUPPORTED,
		    "not supported");
	}
	/* No result TLV. */
	{
		uint8_t u[] = { 0x10, 1, 0, 0 };
		n = answer(WLFW_HOST_CAP, u, sizeof(u));
		CHECK(qmi_parse(ans, n, &m) == 0 &&
		    qmi_result(&m, &res, &err) == EINVAL, "no result taken");
	}
	/* An indication: FW_READY_IND, empty. */
	{
		uint8_t ind[] = { 4, 0, 0, 0x21, 0, 0, 0 };
		CHECK(qmi_parse(ind, sizeof(ind), &m) == 0 &&
		    m.type == QMI_INDICATION && m.id == WLFW_FW_READY_IND &&
		    m.len == 0, "fw ready ind");
	}
	/* Every truncation of the CAP answer: no read past the end. */
	n = answer(WLFW_CAP, t, k);
	for (k = 0; k < n; k++) {
		uint8_t *cut = malloc(k ? k : 1);
		memcpy(cut, ans, k);
		if (k >= 7) {
			cut[5] = (k - 7);
			cut[6] = (k - 7) >> 8;
		}
		if (qmi_parse(cut, k, &m) == 0)
			(void)wlfw_cap_resp(&m, &c);
		free(cut);
	}
}

int
main(void)
{

	requests();
	answers();
	printf("wlfw: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
