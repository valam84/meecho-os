/*
 * Host stand for minix/drivers/net/wcn/wcn_fw.c: the two files the
 * tablet's Linux reads, checked against what that Linux said about them
 * (port/clover/pmos-20261019/pass1/wifi/: dmesg.txt and
 * debugfs/fw_checksums.txt), plus files made here to be wrong.
 *
 * fwtest DIR - DIR holds firmware-5.bin and board-2.bin (outside the tree:
 * D:\minix\clover-flash\pmos\fw\ath10k\WCN3990\hw1.0).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_fw.h"

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

/*
 * The sum fw_checksums and dmesg print: Linux's crc32_le(0, ...) - the
 * reflected CRC-32 polynomial with neither the initial nor the final
 * inversion of zlib's crc32(), so zlib's number for the same file differs.
 */
static uint32_t
crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0;
	int k;

	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320 & -(c & 1));
	}
	return c;
}

static uint8_t *
slurp(const char *dir, const char *name, size_t *len)
{
	char path[1024];
	FILE *f;
	uint8_t *b;
	long n;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "rb")) == NULL) {
		perror(path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n > 0 ? n : 1);
	if (fread(b, 1, n, f) != (size_t)n) {
		perror(path);
		exit(2);
	}
	fclose(f);
	*len = n;
	return b;
}

static int nboards;

static void
count(const char *name, size_t nl, size_t dl, void *arg)
{

	(void)arg;
	nboards++;
	if (getenv("WCN_LIST"))
		printf("  %.*s (%zu bytes)\n", (int)nl, name, dl);
}

/* Little builder for made-up files. */
static uint8_t mk[4096];
static size_t mkl;

static void
put32(uint32_t v)
{

	mk[mkl++] = v;
	mk[mkl++] = v >> 8;
	mk[mkl++] = v >> 16;
	mk[mkl++] = v >> 24;
}

static void
putie(uint32_t id, const void *d, size_t l)
{

	put32(id);
	put32(l);
	memcpy(mk + mkl, d, l);
	mkl += l;
	while (mkl & 3)
		mk[mkl++] = 0xee;
}

static void
magic(const char *m)
{

	mkl = 0;
	memcpy(mk, m, strlen(m) + 1);
	mkl = strlen(m) + 1;
	while (mkl & 3)
		mk[mkl++] = 0x77;
}

/* A BOARD element holding the given (name..., data) list. */
static void
board(const char **names, int n, const char *data)
{
	size_t start, l;
	int i;

	start = mkl;
	put32(WCN_BD_IE_BOARD);
	put32(0);
	for (i = 0; i < n; i++)
		putie(WCN_BD_IE_NAME, names[i], strlen(names[i]));
	putie(WCN_BD_IE_DATA, data, strlen(data));
	l = mkl - start - 8;
	mk[start + 4] = l;
	mk[start + 5] = l >> 8;
	mk[start + 6] = 0;
	mk[start + 7] = 0;
}

static void
real(const char *dir)
{
	struct wcn_fw_info fi;
	const uint8_t *bd;
	uint8_t *fw, *b2;
	size_t fwl, b2l, bdl;
	char name[WCN_BD_NAME_MAX];
	int r, which;

	fw = slurp(dir, "firmware-5.bin", &fwl);
	b2 = slurp(dir, "board-2.bin", &b2l);

	/* fw_checksums: the files are the ones the tablet read. */
	CHECK(crc32(fw, fwl) == 0x022c9087, "firmware-5.bin crc %08x",
	    crc32(fw, fwl));
	CHECK(crc32(b2, b2l) == 0xecd45d37, "board-2.bin crc %08x",
	    crc32(b2, b2l));

	/*
	 * dmesg: "firmware ver  api 5 features wowlan,no-4addr-pad,
	 * allows-mesh-bcast,mgmt-tx-by-reference,non-bmi,
	 * single-chan-info-per-channel", "wmi-op 4 htt-op 3"; the version is
	 * empty (two spaces after "ver").
	 */
	r = wcn_fw_parse(fw, fwl, &fi);
	CHECK(r == 0, "fw parse %d", r);
	CHECK(fi.features == ((1ULL << WCN_FW_FEAT_WOWLAN) |
	    (1ULL << WCN_FW_FEAT_NO_4ADDR_PAD) |
	    (1ULL << WCN_FW_FEAT_MESH_BCAST) |
	    (1ULL << WCN_FW_FEAT_MGMT_TX_BY_REF) |
	    (1ULL << WCN_FW_FEAT_NON_BMI) |
	    (1ULL << WCN_FW_FEAT_SINGLE_CHAN_INFO)),
	    "features %llx", (unsigned long long)fi.features);
	CHECK(fi.wmi_op == 4, "wmi op %d", fi.wmi_op);
	CHECK(fi.htt_op == 3, "htt op %d", fi.htt_op);
	CHECK(fi.version[0] == '\0', "version '%s'", fi.version);
	CHECK(fi.image_len == 0, "image %zu", fi.image_len);
	printf("firmware-5.bin: timestamp %u, features %llx, wmi-op %d, "
	    "htt-op %d\n", fi.timestamp, (unsigned long long)fi.features,
	    fi.wmi_op, fi.htt_op);

	r = wcn_bd_walk(b2, b2l, count, NULL);
	CHECK(r == 0 && nboards > 0, "walk %d, %d boards", r, nboards);
	printf("board-2.bin: %d boards\n", nboards);

	/*
	 * QMI said board_id 0xff, chip_id 0x30214; the tree has no
	 * calibration variant.  fw_checksums: "board 380d3b6d" - the data
	 * the tablet sent.  Look up as the driver will: most specific first.
	 */
	r = ENOENT;
	for (which = 0; which < 3 && r == ENOENT; which++) {
		if (wcn_bd_name(name, sizeof(name), which, 0xff, 0x30214,
		    NULL) < 0)
			continue;
		r = wcn_bd_find(b2, b2l, name, &bd, &bdl);
		printf("board '%s': %s\n", name, r == 0 ? "found" :
		    r == ENOENT ? "none" : "error");
	}
	CHECK(r == 0, "no board for 0xff/0x30214: %d", r);
	if (r == 0) {
		CHECK(crc32(bd, bdl) == 0x380d3b6d, "board data crc %08x",
		    crc32(bd, bdl));
		CHECK(bd > b2 && bd + bdl <= b2 + b2l, "board data outside");
		printf("board data: %zu bytes, crc %08x\n", bdl,
		    crc32(bd, bdl));
	}

	/* The built-in features: what this very file says. */
	{
		struct wcn_fw_info bi;

		wcn_fw_builtin(&bi);
		CHECK(bi.features == fi.features && bi.wmi_op == fi.wmi_op &&
		    bi.htt_op == fi.htt_op, "built-in differs from the file");
	}

	/*
	 * The modem's image: bdwlan.bin is the board 0xff of board-2.bin,
	 * byte for byte - the kit carries no board-2.bin and takes this.
	 */
	if (getenv("MODEM") != NULL && r == 0) {
		char bn[32];
		uint8_t *raw;
		size_t rawl;

		CHECK(wcn_bdf_name(bn, sizeof(bn), 0xff) > 0 &&
		    strcmp(bn, "bdwlan.bin") == 0, "0xff: '%s'", bn);
		raw = slurp(getenv("MODEM"), bn, &rawl);
		CHECK(rawl == bdl && memcmp(raw, bd, bdl) == 0,
		    "bdwlan.bin is not board-2.bin's board 0xff");
		CHECK(crc32(raw, rawl) == 0x380d3b6d, "bdwlan.bin crc %08x",
		    crc32(raw, rawl));
		printf("bdwlan.bin: %zu bytes, crc %08x - the same board\n",
		    rawl, crc32(raw, rawl));
		free(raw);
	}

	/* Every truncation of either file: no read past the end, no hit. */
	for (r = 0; (size_t)r < fwl; r++)
		(void)wcn_fw_parse(fw, r, &fi);
	{
		size_t cut;
		int bad = 0;

		for (cut = 0; cut < b2l; cut += 997) {
			int s = wcn_bd_find(b2, cut, name, &bd, &bdl);
			if (s == 0 && bd + bdl > b2 + cut)
				bad++;
		}
		CHECK(bad == 0, "%d truncations gave data past the end", bad);
	}
	free(fw);
	free(b2);
}

static void
made(void)
{
	struct wcn_fw_info fi;
	const uint8_t *d;
	size_t dl;
	const char *two[] = { "alias", "bus=snoc,qmi-board-id=ff" };
	const char *one[] = { "other" };
	char name[WCN_BD_NAME_MAX];
	uint8_t f[4] = { 0x40, 0x01, 0x1d };

	/* Wrong magic. */
	magic("QCA-ATH10X");
	CHECK(wcn_fw_parse(mk, mkl, &fi) == EINVAL, "wrong fw magic taken");
	magic("QCA-ATH10K-BOARX");
	CHECK(wcn_bd_find(mk, mkl, "x", &d, &dl) == EINVAL,
	    "wrong board magic taken");

	/* A length past the end. */
	magic("QCA-ATH10K");
	put32(WCN_FW_IE_FEATURES);
	put32(400);
	put32(0);
	CHECK(wcn_fw_parse(mk, mkl, &fi) == EINVAL, "long ie taken");

	/* Wrong size of a 32-bit element. */
	magic("QCA-ATH10K");
	putie(WCN_FW_IE_WMI_OP_VERSION, f, 3);
	CHECK(wcn_fw_parse(mk, mkl, &fi) == EINVAL, "3-byte wmi op taken");

	/* A version string, and the features of this firmware's 3 bytes. */
	magic("QCA-ATH10K");
	putie(WCN_FW_IE_FW_VERSION, "1.2.3", 5);
	putie(WCN_FW_IE_FEATURES, f, 3);
	CHECK(wcn_fw_parse(mk, mkl, &fi) == 0 &&
	    strcmp(fi.version, "1.2.3") == 0 && fi.features == 0x1d0140,
	    "version '%s' features %llx", fi.version,
	    (unsigned long long)fi.features);

	/* A board with two names; the second one is the one asked for. */
	magic("QCA-ATH10K-BOARD");
	board(one, 1, "first");
	board(two, 2, "second");
	CHECK(wcn_bd_find(mk, mkl, "bus=snoc,qmi-board-id=ff", &d, &dl) ==
	    0 && dl == 6 && memcmp(d, "second", 6) == 0, "alias lookup");
	CHECK(wcn_bd_find(mk, mkl, "alias", &d, &dl) == 0 && dl == 6,
	    "first alias lookup");
	/* A name that is a prefix of another is not that name. */
	CHECK(wcn_bd_find(mk, mkl, "othe", &d, &dl) == ENOENT,
	    "prefix matched");
	CHECK(wcn_bd_find(mk, mkl, "nothing", &d, &dl) == ENOENT,
	    "missing name found");

	/* Names: no variant - 0 is skipped; with one - in the name. */
	CHECK(wcn_bd_name(name, sizeof(name), 0, 0xff, 0x30214, NULL) < 0,
	    "name 0 without a variant");
	CHECK(wcn_bd_name(name, sizeof(name), 0, 0xff, 0x30214, "xiaomi") >
	    0 && strcmp(name, "bus=snoc,qmi-board-id=ff,qmi-chip-id=30214,"
	    "variant=xiaomi") == 0, "name 0 '%s'", name);
	CHECK(wcn_bd_name(name, 8, 1, 0xff, 0x30214, NULL) < 0,
	    "truncated name accepted");

	/* bdwlan names: the files the tablet's modem image has. */
	CHECK(wcn_bdf_name(name, sizeof(name), 0xff) > 0 &&
	    strcmp(name, "bdwlan.bin") == 0, "0xff '%s'", name);
	CHECK(wcn_bdf_name(name, sizeof(name), 0x04) > 0 &&
	    strcmp(name, "bdwlan.b04") == 0, "0x04 '%s'", name);
	CHECK(wcn_bdf_name(name, sizeof(name), 0x37) > 0 &&
	    strcmp(name, "bdwlan.b37") == 0, "0x37 '%s'", name);
	CHECK(wcn_bdf_name(name, sizeof(name), 0x102) > 0 &&
	    strcmp(name, "bdwlan.102") == 0, "0x102 '%s'", name);
	CHECK(wcn_bdf_name(name, 10, 0x04) < 0, "truncated bdwlan name");
}

int
main(int argc, char **argv)
{

	made();
	if (argc > 1)
		real(argv[1]);
	else
		printf("(no DIR: the tablet's files not checked)\n");
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
