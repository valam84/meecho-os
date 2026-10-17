/*
 * Хостовый стенд службы adreno: настоящие adreno_find.c, adreno_run.c,
 * adreno_cc.c, adreno_hw.c, adreno_tz.c и qcom_soc.c, настоящая читалка
 * дерева libsys, настоящее дерево планшета (clover) — и модель SoC вокруг
 * GPU (model.c) вместо железа.  Служба под стенд не правится.
 *
 *	adrtest [-v] CLOVER_DTB OTHER_DTB
 *
 * OTHER_DTB — дерево машины без Adreno (CB2): служба должна сказать, что
 * узла нет, и уйти, ничего не тронув.
 *
 * Что проверяется в каждом сценарии:
 *   - ни одного обращения к блоку без такта или питания (иначе модель
 *     «вешает шину», и сценарий падает с причиной);
 *   - стадия probe не пишет ничего;
 *   - после любой стадии всё, что модель видит, — как было до неё;
 *   - строки Q, которых ждёт круг на планшете (hw-gpu.md, §12).
 */

#include "adreno.h"
#include "adreno_mdt.h"
#include "adreno_regs.h"
#include "model.h"

#include <minix/qcomscm.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct log ad_log = { "adreno", LEVEL_INFO, NULL };
int ad_route = 1;		/* adreno.c's, route=: the stand sets it */

static int failures, checks;

static void
check(int ok, const char *what, ...)
{
	va_list ap;

	checks++;
	if (ok)
		return;
	failures++;
	printf("  FAIL: ");
	va_start(ap, what);
	vprintf(what, ap);
	va_end(ap);
	printf("\n");
}

static void *
load(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *buf;

	if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
		fprintf(stderr, "adrtest: cannot read %s\n", path);
		exit(2);
	}
	rewind(f);
	buf = malloc((size_t)n);
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "adrtest: cannot read %s\n", path);
		exit(2);
	}
	fclose(f);
	return buf;
}

static void *
load_n(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *buf;

	*len = 0;
	if (f == NULL)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	if ((buf = malloc((size_t)n)) == NULL ||
	    fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(buf);
		return NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
}

static void
save(const char *dir, const char *name, const void *p, size_t n)
{
	char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "wb")) == NULL || fwrite(p, 1, n, f) != n) {
		fprintf(stderr, "adrtest: cannot write %s\n", path);
		exit(2);
	}
	fclose(f);
}

static void
put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static void
put_ph(uint8_t *p, uint32_t type, uint32_t off, uint32_t paddr, uint32_t filesz,
	uint32_t memsz, uint32_t flags)
{
	put32(p + 0, type);
	put32(p + 4, off);
	put32(p + 8, paddr);
	put32(p + 12, paddr);
	put32(p + 16, filesz);
	put32(p + 20, memsz);
	put32(p + 24, flags);
	put32(p + 28, type == 1 ? 0x1000 : 0);
}

/*
 * A zap shader of the tablet's shape, made up: a split image - the .mdt is
 * the headers and the hash, the code is a512_zap.b02 - with one loadable,
 * relocatable segment at 0x5000.  Unlike the tablet's, its segment ends
 * unaligned (0x1331 bytes) and is shorter than its memory size (0x1800):
 * the copy's byte ends and the zeroing are then seen too.
 */
#define S_HDR		0x94
#define S_HASH		0x988
#define S_SEG		0x1331
#define S_MEM		0x1800

static uint8_t syn_mdt[S_HDR + S_HASH], syn_seg[S_SEG], syn_image[S_MEM];

static void
make_zap(const char *dir, int with_seg)
{
	uint8_t *h = syn_mdt;
	unsigned i;

	memset(syn_mdt, 0, sizeof(syn_mdt));
	memcpy(h, "\177ELF", 4);
	h[4] = 1;			/* ELFCLASS32 */
	h[5] = 1;			/* little-endian */
	h[6] = 1;
	put16(h + 16, 2);		/* ET_EXEC */
	put16(h + 18, 164);		/* QDSP6 */
	put32(h + 20, 1);
	put32(h + 24, 0x5000);
	put32(h + 28, 52);		/* e_phoff */
	put32(h + 36, 0x73);
	put16(h + 40, 52);
	put16(h + 42, 32);
	put16(h + 44, 3);
	put_ph(h + 52, 0, 0, 0, S_HDR, 0, 0x07000000);
	put_ph(h + 84, 0, 0x1000, 0x7000, S_HASH, 0x1000, 0x02200000);
	put_ph(h + 116, 1, 0x3000, 0x5000, S_SEG, S_MEM, 0x08000007);
	for (i = 0; i < S_HASH; i++)
		h[S_HDR + i] = (uint8_t)(i * 7 + 3);
	for (i = 0; i < S_SEG; i++)
		syn_seg[i] = (uint8_t)(i * 13 + 1);
	memset(syn_image, 0, sizeof(syn_image));
	memcpy(syn_image, syn_seg, S_SEG);

	save(dir, "a512_zap.mdt", syn_mdt, sizeof(syn_mdt));
	save(dir, "a512_zap.b00", syn_mdt, S_HDR);
	save(dir, "a512_zap.b01", syn_mdt + S_HDR, S_HASH);
	if (with_seg)
		save(dir, "a512_zap.b02", syn_seg, S_SEG);
	else {
		char path[256];

		snprintf(path, sizeof(path), "%s/a512_zap.b02", dir);
		(void)unlink(path);
	}
}

/* One run of a stage in a scenario: the run's result, or -1000 if hung. */
static int
run(const void *dtb, const struct scenario *s, int stage, int tz)
{
	volatile int r = -1000;

	model_reset(s);
	if (setjmp(model_hang) == 0)
		r = adreno_run(dtb, stage, tz);
	return r;
}

static void
expect_clean(const struct scenario *s, const char *stage, int r, int want)
{
	char why[128];

	check(r != -1000, "%s/%s: the bus hung: %s", s->name, stage,
	    model_violation());
	if (r == -1000)
		return;
	check(r == want, "%s/%s: returned %d, wanted %d", s->name, stage, r,
	    want);
	check(model_same_as_start(why, sizeof(why)), "%s/%s: not as found: %s",
	    s->name, stage, why);
}

static void
saw(const struct scenario *s, const char *stage, const char *needle)
{
	check(model_saw(needle), "%s/%s: no line with \"%s\"", s->name, stage,
	    needle);
}

static void
not_saw(const struct scenario *s, const char *stage, const char *needle)
{
	check(!model_saw(needle), "%s/%s: a line with \"%s\"", s->name, stage,
	    needle);
}

/*
 * Stage zap (hw-gpu.md, §17): the zap shader to TrustZone, in Linux's
 * order, and what tzmodel.c checks of it; then the starts after the first.
 * A made-up image always; the tablet's own, when ZAP_DIR= names where
 * it was copied off its vendor partition (it may not live in the tree).
 */
static void
zap_checks(const void *dtb, const struct scenario *tablet)
{
	struct scenario s;
	char dir[] = "/tmp/adrzap.XXXXXX";
	const char *real = getenv("ZAP_DIR");
	const char *why;
	struct mdt m;
	char path[256];
	void *mdt, *seg;
	size_t mlen, slen;
	uint8_t *image;
	int r;

	printf("zap\n");
	if (mkdtemp(dir) == NULL) {
		fprintf(stderr, "adrtest: mkdtemp\n");
		exit(2);
	}
	make_zap(dir, 1);
	strlcpy(ad_fwdir, dir, sizeof(ad_fwdir));
	tz_expect(syn_mdt, sizeof(syn_mdt), syn_image, sizeof(syn_image),
	    0x2000);

	/* The made-up image's headers, as adreno_mdt.c reads them. */
	check(mdt_parse(syn_mdt, sizeof(syn_mdt), &m, &why) == 0,
	    "mdt: the made-up image refused: %s", why);
	check(m.nph == 3 && m.hash == 1 && m.ehdr_size == S_HDR &&
	    m.hash_size == S_HASH && m.hash_in_file && m.hash_off == S_HDR &&
	    m.nload == 1 && m.load[0] == 2 && m.relocatable && m.split &&
	    m.min_addr == 0x5000 && m.max_addr == 0x7000,
	    "mdt: the made-up image misread");

	/* The first start: loaded and authenticated. */
	s = *tablet;
	s.name = "zap-load";
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, OK);
	saw(&s, "zap", "Q18 zap: a512_zap.mdt 2588 bytes, 3 headers, hash 1 "
	    "(2440 bytes), load 1, relocatable, split; image 5000..7000, 2000 "
	    "bytes, state none");
	saw(&s, "zap", "Q18 zap: init_image(13, 2588 bytes at ");
	saw(&s, "zap", "Q18 zap: mem_setup(13, f0b00000, 2000): ok");
	saw(&s, "zap", "Q18 zap: segment 2 at +0: 4913 bytes + 1231 zero, read "
	    "back the same");
	saw(&s, "zap", "Q18 zap: auth_and_reset(13): ok - authenticated; "
	    "secvid 00000001");
	check(strcmp(tz_trail(), "init mem auth") == 0, "zap: calls \"%s\"",
	    tz_trail());
	check(tz_state() == QCOMSCM_PAS_AUTHED, "zap: state %d", tz_state());
	check(tz_refusal()[0] == '\0', "zap: refused: %s", tz_refusal());
	saw(&s, "zap", "Q17 smmu put back");

	/* Every start after: resumed, the region never touched. */
	s.name = "zap-resume";
	s.zap_state = QCOMSCM_PAS_AUTHED;
	strlcpy(ad_fwdir, "/nonexistent", sizeof(ad_fwdir));
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, OK);
	saw(&s, "zap", "Q18 zap: authenticated since boot; set_remote_state("
	    "resume, 13): ok");
	not_saw(&s, "zap", "init_image");
	check(strcmp(tz_trail(), "remote") == 0, "zap-resume: calls \"%s\"",
	    tz_trail());
	strlcpy(ad_fwdir, dir, sizeof(ad_fwdir));

	/* A load that named the region and did not finish: hands off. */
	s.name = "zap-given";
	s.zap_state = QCOMSCM_PAS_GIVEN;
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, EBUSY);
	saw(&s, "zap", "the region is TrustZone's until a reboot");
	check(tz_calls() == 0, "zap-given: %u calls", tz_calls());

	/* TrustZone refuses the image: said, and the region left to it. */
	s.name = "zap-refused";
	s.zap_state = QCOMSCM_PAS_NONE;
	s.zap_auth_fail = 1;
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, EIO);
	saw(&s, "zap", "Q18 zap: auth_and_reset(13): refused -2");
	check(tz_state() == QCOMSCM_PAS_GIVEN, "zap-refused: state %d",
	    tz_state());
	s.zap_auth_fail = 0;

	/* A segment's file missing: nothing told to TrustZone at all. */
	s.name = "zap-nofile";
	make_zap(dir, 0);
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, ENOENT);
	saw(&s, "zap", "a512_zap.b02: 2; refusing");
	check(tz_calls() == 0, "zap-nofile: %u calls", tz_calls());
	make_zap(dir, 1);

	/* No qcom,scm: as QEMU. */
	s.name = "zap-notz";
	s.tz = 0;
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, ENODEV);
	saw(&s, "zap", "Q18 zap: sys_qcomscm: 19 (no qcom,scm in the tree)");
	s.tz = 1;

	/* The tablet's own image (§15: readelf of the .mdt and the .elf). */
	if (real == NULL) {
		printf("  (ZAP_DIR= not set: the tablet's image not tried)\n");
		return;
	}
	snprintf(path, sizeof(path), "%s/a512_zap.mdt", real);
	mdt = load_n(path, &mlen);
	snprintf(path, sizeof(path), "%s/a512_zap.b02", real);
	seg = load_n(path, &slen);
	check(mdt != NULL && seg != NULL, "zap-real: no %s/a512_zap.mdt or .b02",
	    real);
	if (mdt == NULL || seg == NULL)
		return;
	check(mdt_parse(mdt, mlen, &m, &why) == 0, "zap-real: refused: %s",
	    why);
	check(mlen == 6684 && m.nph == 3 && m.ph[0].type == 0 &&
	    m.ph[0].filesz == 0x94 && m.ph[0].flags == 0x07000000 &&
	    m.ph[1].offset == 0x1000 && m.ph[1].paddr == 0x7000 &&
	    m.ph[1].filesz == 0x1988 && m.ph[1].flags == 0x02200000 &&
	    m.ph[2].type == 1 && m.ph[2].offset == 0x3000 &&
	    m.ph[2].paddr == 0x5000 && m.ph[2].filesz == 0x1334 &&
	    m.ph[2].memsz == 0x1334 && m.ph[2].flags == 0x08000007,
	    "zap-real: headers not as readelf gave them");
	check(m.hash == 1 && m.hash_in_file && m.hash_off == 0x94 &&
	    m.relocatable && m.split && m.min_addr == 0x5000 &&
	    m.max_addr == 0x7000 && slen == 0x1334,
	    "zap-real: misread");
	if ((image = calloc(1, 0x1334)) == NULL)
		abort();
	memcpy(image, seg, slen);
	tz_expect(mdt, mlen, image, 0x1334, 0x2000);
	strlcpy(ad_fwdir, real, sizeof(ad_fwdir));
	s.name = "zap-real";
	s.zap_state = QCOMSCM_PAS_NONE;
	r = run(dtb, &s, AD_ZAP, 0);
	expect_clean(&s, "zap", r, OK);
	saw(&s, "zap", "Q18 zap: a512_zap.mdt 6684 bytes, 3 headers, hash 1 "
	    "(6536 bytes), load 1, relocatable, split; image 5000..7000, 2000 "
	    "bytes, state none");
	saw(&s, "zap", "Q18 zap: init_image(13, 6684 bytes at ");
	saw(&s, "zap", "Q18 zap: segment 2 at +0: 4916 bytes + 0 zero, read "
	    "back the same");
	saw(&s, "zap", "authenticated");
	check(strcmp(tz_trail(), "init mem auth") == 0, "zap-real: calls "
	    "\"%s\"", tz_trail());
	strlcpy(ad_fwdir, dir, sizeof(ad_fwdir));
}

/*
 * What a5xx_hw_init() leaves in the registers of an A512 (Linux's
 * a5xx_gpu.c and a512_hwcg[], copied out again here, not from the
 * service): dword, value.  The read-modify-writes are checked against
 * the bits cpmodel.c put there first.
 */
#define PROT(reg, log2len)	(0x60000000u | (uint32_t)(log2len) << 24 | \
	(((uint32_t)(reg) << 2) & 0xfffff))
static const struct { uint32_t dw, val; } after_hw_init[] = {
	{ 0x0067, 0xaaa8aa00 }, { 0x0533, 0x00000182 },	/* CLOCK_CNTL, ISDB */
	{ 0x0068, 0x02222222 }, { 0x006c, 0x02222220 }, { 0x0070, 0x0000f3cf },
	{ 0x0074, 0x00000080 }, { 0x004a, 0x22222222 }, { 0x0052, 0x00002222 },
	{ 0x00b0, 0x77777777 }, { 0x00b8, 0x00007777 }, { 0x00a4, 0x11111111 },
	{ 0x00ac, 0x00001111 }, { 0x005d, 0x00222222 }, { 0x005e, 0x00444444 },
	{ 0x005f, 0x00000002 }, { 0x007c, 0x00222222 }, { 0x0082, 0x00022220 },
	{ 0x008a, 0x05522222 }, { 0x008b, 0x00505555 }, { 0x0086, 0x04040404 },
	{ 0x0080, 0x07444044 }, { 0x008c, 0x00000002 }, { 0x0081, 0x00010011 },
	{ 0x0063, 0x04222222 }, { 0x0060, 0x02222222 }, { 0x0091, 0x00002222 },
	{ 0x0062, 0x04104004 }, { 0x0065, 0x00004000 }, { 0x0061, 0x00000200 },
	{ 0x0092, 0x00002222 },
	{ 0x3049, 3 }, { 0x302a, 9 }, { 0x046f, 0xffffffff }, { 0x0093, 1 },
	{ 0x002f, 0x4000ffff }, { 0x0464, 1 }, { 0x046b, 6 }, { 0x0e96, 2 },
	{ 0x0e89, 0xffff0000 }, { 0x0e8a, 0x0001ffff },	/* UCHE_TRAP_BASE */
	{ 0x0e87, 0xffff0000 }, { 0x0e88, 0x0001ffff },	/* WRITE_THRU */
	{ 0x0e8b, 0x00100000 }, { 0x0e8d, 0x00143fff },	/* GMEM range */
	{ 0x0825, 0x40 }, { 0x0826, 0x400 }, { 0x0820, 0x80000060 },
	{ 0x081f, 0x40201b16 }, { 0x0d00, 0x80100000 },	/* PC_DBG_ECO */
	{ 0x0cc4, 0x00000a00 },			/* RB_DBG_ECO: + bit 9 */
	{ 0x0e81, 0x20000000 }, { 0x0833, 0x02000000 }, { 0x0094, 0xa6ffffff },
	{ 0x0095, 0x3f }, { 0x0f02, 0x80 }, { 0x0cc6, 2 }, { 0x0e8f, 1 },
	{ 0x0e60, 0x00800402 },			/* VPC: + bits 10, 23 */
	{ 0x0e04, 0x00000001 },			/* HLSQ: - bit 18 */
	{ 0x08a0, 7 },
	{ 0x0880, PROT(0x04, 2) }, { 0x0881, PROT(0x08, 3) },
	{ 0x0882, PROT(0x10, 4) }, { 0x0883, PROT(0x20, 5) },
	{ 0x0884, PROT(0x40, 6) }, { 0x0885, PROT(0x80, 6) },
	{ 0x0886, PROT(0xf800, 4) }, { 0x0887, PROT(0xf400, 1) },
	{ 0x0888, PROT(0x800, 6) }, { 0x0889, PROT(0x840, 3) },
	{ 0x088a, PROT(0x880, 5) }, { 0x088b, PROT(0xaa0, 0) },
	{ 0x088c, PROT(0xcc0, 0) }, { 0x088d, PROT(0xcf0, 1) },
	{ 0x088e, PROT(0xe68, 3) }, { 0x088f, PROT(0xe70, 4) },
	{ 0x0890, PROT(0xe80, 4) }, { 0x0891, PROT(0x10000, 15) },
	{ 0xf803, 0 }, { 0xf800, 0 }, { 0xf801, 0 }, { 0xf802, 0 },
	{ 0x0860, 1 }, { 0x0bc1, 1 }, { 0x0c81, 1 }, { 0x0cc5, 1 },
	{ 0x0d01, 1 }, { 0x0e05, 1 }, { 0x0e41, 1 }, { 0x0e61, 1 },
	{ 0x0e80, 1 }, { 0x0ec1, 1 }, { 0x0f01, 1 }, { 0xf810, 1 },
	{ 0x0802, 0x0800020c },			/* CP_RB_CNTL */
	{ 0x0038, 0 },				/* the mask, after cp_stop() */
};

/* Microcode of the right shape, made up: the PFP's words 1 and 3 those of
 * linux-firmware's (§14) - a patched one, with WHERE_AM_I. */
#define MC_LEN		0x400
static uint8_t syn_pm4[MC_LEN], syn_pfp[MC_LEN];

static void
make_ucode(const char *dir)
{
	unsigned i;

	for (i = 0; i < MC_LEN; i++) {
		syn_pm4[i] = (uint8_t)(i * 5 + 7);
		syn_pfp[i] = (uint8_t)(i * 11 + 3);
	}
	put32(syn_pfp + 4, 0x005ff08a);
	put32(syn_pfp + 12, 0x00087001);
	save(dir, "a530_pm4.fw", syn_pm4, sizeof(syn_pm4));
	save(dir, "a530_pfp.fw", syn_pfp, sizeof(syn_pfp));
	cp_expect(syn_pm4, sizeof(syn_pm4), syn_pfp, sizeof(syn_pfp));
}

static void
regs_after(const struct scenario *s)
{
	unsigned i, bad = 0;

	for (i = 0; i < sizeof(after_hw_init) / sizeof(after_hw_init[0]); i++)
		if (model_gpu_reg(after_hw_init[i].dw) != after_hw_init[i].val) {
			if (bad++ < 4)
				check(0, "%s: register %04x is %08x, Linux "
				    "leaves %08x", s->name, after_hw_init[i].dw,
				    model_gpu_reg(after_hw_init[i].dw),
				    after_hw_init[i].val);
		}
	check(bad == 0, "%s: %u registers not as Linux leaves them", s->name,
	    bad);
}

static void
cp_lines(const struct scenario *s, const char *pm4, const char *pfp)
{
	saw(s, "cp", pm4);
	saw(s, "cp", pfp);
	saw(s, "cp", "where-am-i; ring 2020000, memptrs 2030000");
	saw(s, "cp", "Q19 cp: hw_init, 186 writes; cp_protect_cntl 7");
	/* The CP busy for a while after its read pointer was read (the
	 * model's hysteresis, the tablet's 800001c0): idle after it. */
	saw(s, "cp", "Q20 cp: me_init done, idle in 20 us (read in 0, busy "
	    "800001c0): rptr c wptr c shadow "
	    "c, rbbm 00000000 int 00000000");
	saw(s, "cp", "Q21 cp: set_secure_mode 0, idle in 20 us; secvid "
	    "00000000");
	saw(s, "cp", "Q22 line 332 came: int 00100000, fence 1 in memory, "
	    "scratch2 1, idle");
	saw(s, "cp", "Q22 the GPU ran its first command");
	saw(s, "cp", "Q23 cp: interrupts masked, vbif ports halted (f)");
	saw(s, "cp", "Q11 undone: cx off gx off; all as found");
}

/*
 * Stage cp (hw-gpu.md, §17): the command processor, its ring and its
 * first command against cpmodel.c, the zap shader at Linux's point.
 */
static void
cp_checks(const void *dtb, const struct scenario *tablet)
{
	struct scenario s;
	char dir[] = "/tmp/adrcp.XXXXXX", path[300];
	const char *real = getenv("ZAP_DIR"), *fw = getenv("FW_DIR");
	void *b[4];
	size_t n[4];
	int r;

	printf("cp\n");
	check(pm4_pkt7(CP_WHERE_AM_I, 2) == 0x70620002 &&
	    pm4_pkt7(CP_EVENT_WRITE, 4) == 0x70460004 &&
	    pm4_pkt7(CP_SET_PROTECTED_MODE, 1) == 0x70df0001 &&
	    pm4_pkt7(0x3f, 3) == 0x70bf8003 &&
	    pm4_pkt7(CP_ME_INIT, 8) == 0x70c80008 &&
	    pm4_pkt4(0x0b7a, 1) == 0x480b7a01,
	    "pm4: headers not as hw-gpu.md §6 has them");
	if (mkdtemp(dir) == NULL) {
		fprintf(stderr, "adrtest: mkdtemp\n");
		exit(2);
	}
	make_zap(dir, 1);
	make_ucode(dir);
	strlcpy(ad_fwdir, dir, sizeof(ad_fwdir));
	tz_expect(syn_mdt, sizeof(syn_mdt), syn_image, sizeof(syn_image),
	    0x2000);

	/* The first start of this boot: the zap shader loaded after ME_INIT. */
	s = *tablet;
	s.name = "cp-load";
	s.zap_state = QCOMSCM_PAS_NONE;
	r = run(dtb, &s, AD_CP, 0);
	expect_clean(&s, "cp", r, OK);
	cp_lines(&s, "Q19 cp: pm4 1020 bytes at 2000000", "pfp 1020 at "
	    "2010000 (005ff08a 00087001)");
	saw(&s, "cp", "Q18 zap: auth_and_reset(13): ok - authenticated");
	check(strcmp(tz_trail(), "init mem auth") == 0, "cp-load: calls "
	    "\"%s\"", tz_trail());
	check(cp_model_packets() == 7, "cp-load: %u packets",
	    cp_model_packets());
	regs_after(&s);

	/* Every start after: resumed. */
	s.name = "cp-resume";
	s.zap_state = QCOMSCM_PAS_AUTHED;
	r = run(dtb, &s, AD_CP, 0);
	expect_clean(&s, "cp", r, OK);
	saw(&s, "cp", "set_remote_state(resume, 13): ok");
	saw(&s, "cp", "Q22 the GPU ran its first command");
	check(strcmp(tz_trail(), "remote") == 0, "cp-resume: calls \"%s\"",
	    tz_trail());

	/* No zap shader: never out of secure mode, and everything back. */
	s.name = "cp-nozap";
	s.zap_state = QCOMSCM_PAS_NONE;
	snprintf(path, sizeof(path), "%s/a512_zap.mdt", dir);
	(void)unlink(path);
	r = run(dtb, &s, AD_CP, 0);
	expect_clean(&s, "cp", r, ENOENT);
	saw(&s, "cp", "Q20 cp: me_init done");
	not_saw(&s, "cp", "Q21");
	saw(&s, "cp", "Q23 cp: interrupts masked, vbif ports halted (f)");
	make_zap(dir, 1);

	/* No microcode: nothing started. */
	s.name = "cp-noucode";
	snprintf(path, sizeof(path), "%s/a530_pfp.fw", dir);
	(void)unlink(path);
	r = run(dtb, &s, AD_CP, 0);
	expect_clean(&s, "cp", r, ENOENT);
	saw(&s, "cp", "a530_pfp.fw: 2; refusing");
	check(!cp_model_started(), "cp-noucode: the CP started");
	make_ucode(dir);

	/* route=0: the GPU would reach no memory - refused. */
	s.name = "cp-noroute";
	ad_route = 0;
	r = run(dtb, &s, AD_CP, 0);
	ad_route = 1;
	expect_clean(&s, "cp", r, EINVAL);
	check(!cp_model_started(), "cp-noroute: the CP started");

	/* The tablet's own files: zap shader and linux-firmware's microcode. */
	if (real == NULL || fw == NULL) {
		printf("  (ZAP_DIR= or FW_DIR= not set: the real files not "
		    "tried)\n");
		return;
	}
	snprintf(path, sizeof(path), "%s/a512_zap.mdt", real);
	b[0] = load_n(path, &n[0]);
	snprintf(path, sizeof(path), "%s/a512_zap.b02", real);
	b[1] = load_n(path, &n[1]);
	snprintf(path, sizeof(path), "%s/a530_pm4.fw", fw);
	b[2] = load_n(path, &n[2]);
	snprintf(path, sizeof(path), "%s/a530_pfp.fw", fw);
	b[3] = load_n(path, &n[3]);
	check(b[0] && b[1] && b[2] && b[3], "cp-real: files missing");
	if (!(b[0] && b[1] && b[2] && b[3]))
		return;
	save(dir, "a512_zap.mdt", b[0], n[0]);
	save(dir, "a512_zap.b02", b[1], n[1]);
	save(dir, "a530_pm4.fw", b[2], n[2]);
	save(dir, "a530_pfp.fw", b[3], n[3]);
	tz_expect(b[0], n[0], b[1], n[1], 0x2000);
	cp_expect(b[2], n[2], b[3], n[3]);
	s.name = "cp-real";
	r = run(dtb, &s, AD_CP, 0);
	expect_clean(&s, "cp", r, OK);
	cp_lines(&s, "Q19 cp: pm4 19568 bytes at 2000000", "pfp 16140 at "
	    "2010000 (005ff08a 00087001)");
	regs_after(&s);
}

int
main(int argc, char **argv)
{
	/* .smmu_tablet = 0: the made-up SMMU of the first stand. */
	static const struct scenario boot = { "boot", 1, 0, 0, 0, 0, 0, 0, 1,
	    .smmu_tablet = 0 };
	static const struct scenario allon = { "all-on", 1, 1, 1, 1, 1, 0, 0,
	    1, .smmu_tablet = 0 };
	static const struct scenario cxon = { "cx-on", 1, 0, 1, 0, 0, 0, 0, 0,
	    .smmu_tablet = 0 };
	static const struct scenario noiface = { "no-iface", 0, 0, 0, 0, 0, 0,
	    0, 0, .smmu_tablet = 0 };
	static const struct scenario gxdead = { "gx-dead", 1, 0, 0, 0, 0, 1, 0,
	    0, .smmu_tablet = 0 };
	static const struct scenario stuck = { "root-stuck", 1, 0, 0, 0, 0, 0,
	    1, 0, .smmu_tablet = 0 };
	static const struct scenario stale = { "stale-clocks", 1, 1, 1, 0, 1,
	    0, 0, 0, .smmu_tablet = 0 };
	/* The SMMU as the tablet answered (hw-gpu.md, §13, §15): S2CR writes
	 * dropped ("measured").  "tablet": the same SMMU taking them - the
	 * stages beyond smr, for when a way to route is found - and what may
	 * differ: writes dropped, a line not wired, bank 0 taken. */
	static const struct scenario measured = { "tablet-measured", 1, 0, 0,
	    0, 0, 0, 0, 1, .smmu_tablet = 1, .stale_tlb = 1, .s2cr_ro = 1 };
	static const struct scenario tablet = { "tablet", 1, 0, 0, 0, 0, 0, 0,
	    1, .smmu_tablet = 1, .stale_tlb = 1 };
	static const struct scenario smr_ro = { "smr-ro", 1, 0, 0, 0, 0, 0, 0,
	    1, .smmu_tablet = 1, .smr_ro = 1, .stale_tlb = 1 };
	static const struct scenario noline = { "no-line", 1, 0, 0, 0, 0, 0, 0,
	    1, .smmu_tablet = 1, .no_line = 1, .stale_tlb = 1 };
	static const struct scenario cbbusy = { "cb0-busy", 1, 0, 0, 0, 0, 0,
	    0, 1, .smmu_tablet = 1, .cb0_busy = 1 };
	static const struct scenario bypfree = { "bypass-free", 1, 0, 0, 0,
	    0, 0, 0, 1, .smmu_tablet = 1, .stale_tlb = 1, .bypass_free = 1 };
	static const char *const stages[] = { "probe", "cx", "gx", "gpu",
	    "smmu", "smr", "cb0", "fault" };
	const struct scenario *all[] = { &boot, &allon, &cxon };
	void *dtb, *other;
	unsigned i, st;
	int r, a = 1;

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		model_verbose = 1;
		a = 2;
	}
	if (argc - a != 2) {
		fprintf(stderr, "usage: adrtest [-v] CLOVER_DTB OTHER_DTB\n");
		return 2;
	}
	dtb = load(argv[a]);
	other = load(argv[a + 1]);

	/* The tree as the service reads it. */
	printf("tree\n");
	r = run(dtb, &boot, AD_PROBE, 0);
	expect_clean(&boot, "probe", r, OK);
	check(model_writes() == 0, "probe wrote %u times", model_writes());
	saw(&boot, "probe", "node gpu@5000000 qcom,adreno-512.0 chip "
	    "0x05010200 at 0x5000000+0x40000 irq 332");
	saw(&boot, "probe", "smmu iommu@5040000 sid 0 at 0x5040000+0x10000, "
	    "10 lines (2 global), domain 1; gpucc 0x5065000+0x9038");
	saw(&boot, "probe", "zap a512_zap.mdt at 0xf0b00000+0x2000");
	saw(&boot, "probe", "up to 588 MHz");
	saw(&boot, "probe", "Q1 gcc iface on");
	saw(&boot, "probe", " mem off");
	saw(&boot, "probe", "Q2 gpucc cx off");
	saw(&boot, "probe", " gx off");
	saw(&boot, "probe", " cxo on");
	saw(&boot, "probe", " gfx3d off");
	saw(&boot, "probe", "pll0 nolock L 29 800.0 MHz");
	saw(&boot, "probe", " gfx3d pll0/2 400.0");
	saw(&boot, "probe", "stage probe: nothing written");

	/* The guards themselves: asked to read with nothing powered. */
	r = run(dtb, &tablet, AD_PROBE, 0);
	if (setjmp(model_hang) == 0) {
		hw_gpu_report();
		hw_smmu_report();
		(void)smmu_smr_test();
		(void)smmu_cb0(1);
		smmu_undo();
		r = OK;
	} else
		r = -1000;
	check(r == OK, "guards: the bus hung: %s", model_violation());
	saw(&tablet, "guards", "Q5 gpu not read: rbbmtimer is off");
	saw(&tablet, "guards", "Q9 smmu not read: mem is off");
	saw(&tablet, "guards", "Q13 smmu not written: mem is off");
	saw(&tablet, "guards", "Q14 smmu not written: mem is off");
	check(model_writes() == 0, "guards: %u writes", model_writes());
	/* Every clock reads on, GX does not: the domain alone must stop it. */
	r = run(dtb, &stale, AD_PROBE, 0);
	if (setjmp(model_hang) == 0) {
		hw_gpu_report();
		hw_smmu_report();
		r = OK;
	} else
		r = -1000;
	check(r == OK, "guards/stale: the bus hung: %s", model_violation());
	saw(&stale, "guards", "Q5 gpu not read: its GPUCC power domain is off");
	saw(&stale, "guards", "Q9 smmu not read: its power domain is off");

	r = run(other, &boot, AD_SMMU, 0);
	check(r == ENXIO, "other tree: returned %d, wanted ENXIO", r);
	check(model_writes() == 0, "other tree: wrote %u times",
	    model_writes());
	saw(&boot, "other", "no qcom,adreno node");

	/* Every stage in the states the boot loader may leave. */
	for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		printf("%s\n", all[i]->name);
		for (st = AD_PROBE; st <= AD_SMMU; st++) {
			r = run(dtb, all[i], (int)st, 0);
			expect_clean(all[i], stages[st], r, OK);
			check(smmu_writes() == 0, "%s/%s: %u SMMU writes before "
			    "stage smr", all[i]->name, stages[st],
			    smmu_writes());
			if (st == AD_PROBE) {
				check(model_writes() == 0, "%s/probe: %u "
				    "writes", all[i]->name, model_writes());
				continue;
			}
			saw(all[i], stages[st], "Q4 power:");
			saw(all[i], stages[st], "Q11 undone:");
			saw(all[i], stages[st], "all as found");
			if (st == AD_CX) {
				not_saw(all[i], stages[st], "Q4 clocks");
				not_saw(all[i], stages[st], "gx on in");
			}
			if (st >= AD_GX)
				saw(all[i], stages[st], "Q4 clocks:");
			if (st >= AD_GPU) {
				saw(all[i], stages[st], "Q5 gpu hw 50120000 "
				    "00000000 00000000 vbif 20090000");
				saw(all[i], stages[st], "secvid 00000001");
				saw(all[i], stages[st], "Q7 gpu timer 192000 "
				    "ticks in 10 ms");
				saw(all[i], stages[st], "Q8 gpu scratch0 "
				    "12345678, wrote 5ad0c0de read 5ad0c0de, "
				    "restored 12345678: writes reach");
			} else
				not_saw(all[i], stages[st], "Q5");
			if (st >= AD_SMMU) {
				saw(all[i], stages[st], "Q9 smmu scr0 00000001"
				    " id 48000010 20000005 00000511");
				saw(all[i], stages[st], "5 cb, 16 smr, 4K "
				    "pages, ias 36 oas 36 ubs 48");
				saw(all[i], stages[st], "Q10 sid 0: smr 0 "
				    "s2cr 00000000 (translate cb 0) cbar "
				    "00010000");
				saw(all[i], stages[st], "Q10 cb0 +0x8000");
			} else
				not_saw(all[i], stages[st], "Q9");
		}
	}
	/* The SMMU written: the tablet as it answered, stage by stage. */
	printf("%s\n", tablet.name);
	r = run(dtb, &tablet, AD_SMMU, 0);
	expect_clean(&tablet, "smmu", r, OK);
	check(smmu_writes() == 0, "tablet/smmu: %u SMMU writes", smmu_writes());
	saw(&tablet, "smmu", "Q9 smmu scr0 00200626 id 48016603 20008005 "
	    "00007511 gfsr 00000000: 5 cb, 3 smr, 4K pages, ias 36 oas 36 "
	    "ubs 48");
	saw(&tablet, "smmu", "Q10 sme 1: smr 00000000 s2cr 000200ff");
	saw(&tablet, "smmu", "Q10 sid 0: no valid SMR routes it");
	not_saw(&tablet, "smmu", "Q13");

	r = run(dtb, &tablet, AD_SMR, 0);
	expect_clean(&tablet, "smr", r, OK);
	saw(&tablet, "smr", "Q13 smr 0 (00000000 000200ff): id bits 7fff mask "
	    "bits 7fff (numsidb 3)");
	saw(&tablet, "smr", "Q13 s2cr wrote 00020000 read 00020000 (taken); back "
	    "00000000 000200ff");
	saw(&tablet, "smr", "Q17 smmu put back: smr 0; all as found");
	not_saw(&tablet, "smr", "Q14");

	r = run(dtb, &tablet, AD_CB0, 0);
	expect_clean(&tablet, "cb0", r, OK);
	saw(&tablet, "cb0", "ias 48 oas 36, 4 levels; sctlr 00001167 tcr "
	    "00802010 tcr2 00038011");
	saw(&tablet, "cb0", "Q14 ats pr 1000000: pa ");
	saw(&tablet, "cb0", "Q14 ats uw a5a5a5a5a000: par 0000000000000009 - "
	    "fault, read-only");
	saw(&tablet, "cb0", "Q14 ats pr 1001000: par 0000000000000003 - fault, "
	    "not mapped");
	saw(&tablet, "cb0", "Q14 ats pr 1000000: par 0000000000000003 - fault, "
	    "unmapped");
	saw(&tablet, "cb0", "Q14 cb0: 7 of 7 translations as the tables say");
	not_saw(&tablet, "cb0", "NOT as mapped");
	saw(&tablet, "cb0", "Q15 sid 0 by smr 0 to cb0: smr 80000000 s2cr "
	    "00000300; 10 ms: fsr 0 gfsr 0; taken back");
	saw(&tablet, "cb0", "Q17 smmu put back: smr 0 cb0; all as found");
	not_saw(&tablet, "cb0", "Q16");

	r = run(dtb, &tablet, AD_FAULT, 0);
	expect_clean(&tablet, "fault", r, OK);
	saw(&tablet, "fault", "Q16 line 361 quiet before; raised tf: it came, "
	    "fsr 00000002");
	saw(&tablet, "fault", "Q16 cleared (fsr 00000000), quiet after: the "
	    "line works");
	saw(&tablet, "fault", "all as found");

	/* As measured: SMRs take writes, S2CRs do not - stop at Q13, every
	 * stage after it, nothing changed and nothing routed. */
	printf("%s\n", measured.name);
	r = run(dtb, &measured, AD_FAULT, 0);
	check(r == OK, "measured/fault: returned %d (%s)", r, model_violation());
	saw(&measured, "fault", "Q13 smr 0 (00000000 000200ff): id bits 7fff "
	    "mask bits 7fff (numsidb 3)");
	saw(&measured, "fault", "Q13 s2cr wrote 00020000 read 000200ff "
	    "(ignored: a hypervisor's); back 00000000 000200ff");
	saw(&measured, "fault", "Q13 the SMRs take writes");
	saw(&measured, "fault", "Q15 sid 0 by smr 0 to cb0: smr 80000000 s2cr "
	    "00000300");
	saw(&measured, "fault", "the line works");
	/* The S2CR cannot be put back (a fault-type write is ignored): it
	 * keeps naming bank 0, and bank 0 stays stage 1, off. */
	saw(&measured, "fault", "Q17 smmu put back: smr 0 (s2cr left 00000300) "
	    "cb0 off, kept stage 1; all as found");
	check(model_contig_live() == 0, "measured/fault: memory never freed");
	r = run(dtb, &measured, AD_SMR, 0);
	expect_clean(&measured, "smr", r, OK);
	/* route=0: the bank and its line, no stream routed - S2CR untouched,
	 * so everything back as it was, bank 0 stage 2 again. */
	ad_route = 0;
	r = run(dtb, &measured, AD_FAULT, 0);
	ad_route = 1;
	expect_clean(&measured, "fault route=0", r, OK);
	saw(&measured, "fault route=0", "Q15 route=0: sid 0 not routed");
	not_saw(&measured, "fault route=0", "Q15 writing s2cr");
	saw(&measured, "fault route=0", "the line works");
	saw(&measured, "fault route=0", "Q17 smmu put back: smr 0 cb0; all as "
	    "found");

	/* A busy SMR, and a free one whose S2CR would bypass the SMMU: the
	 * order of the writes is all that keeps the GPU's stream inside. */
	printf("%s\n", bypfree.name);
	r = run(dtb, &bypfree, AD_FAULT, 0);
	expect_clean(&bypfree, "fault", r, OK);
	saw(&bypfree, "fault", "Q15 sid 0 by smr 1 to cb0");
	saw(&bypfree, "fault", "the line works");
	saw(&bypfree, "fault", "Q17 smmu put back: smr 1 cb0; all as found");

	/* Writes that do not stick: stop before the bank, nothing changed. */
	printf("%s\n", smr_ro.name);
	r = run(dtb, &smr_ro, AD_FAULT, 0);
	expect_clean(&smr_ro, "fault", r, EIO);
	saw(&smr_ro, "fault", "the SMRs do NOT keep what is written; stopping");
	not_saw(&smr_ro, "fault", "Q14");

	/* The line never comes: said so, everything back. */
	printf("%s\n", noline.name);
	r = run(dtb, &noline, AD_FAULT, 0);
	expect_clean(&noline, "fault", r, EIO);
	saw(&noline, "fault", "raised tf: NOTHING came");
	saw(&noline, "fault", "the line does NOT work");
	saw(&noline, "fault", "all as found");

	/* Bank 0 somebody's: refused, nothing of it written. */
	printf("%s\n", cbbusy.name);
	r = run(dtb, &cbbusy, AD_FAULT, 0);
	expect_clean(&cbbusy, "fault", r, EBUSY);
	saw(&cbbusy, "fault", "Q14 cb0 is in use (sctlr 00000001");
	not_saw(&cbbusy, "fault", "Q15");

	/* The made-up SMMU routes SID 0 to bank 0 already: refused. */
	r = run(dtb, &boot, AD_CB0, 0);
	check(r != -1000, "boot/cb0: the bus hung: %s", model_violation());
	not_saw(&boot, "cb0", "Q15");
	saw(&boot, "cb0", "refusing");

	/* What each start state should make of the domains. */
	r = run(dtb, &boot, AD_GX, 0);
	saw(&boot, "gx", "Q4 power: cx on in");
	saw(&boot, "gx", "gx on in");
	saw(&boot, "gx", "Q4 clocks: mem on; mem_iface on; rbbmtimer xo on;"
	    " rbcpr xo on; core xo on;");
	r = run(dtb, &allon, AD_GX, 0);
	saw(&allon, "gx", "Q4 power: cx was on; gx was on;");
	saw(&allon, "gx", "Q4 clocks: all were on");
	check(model_writes() == 0, "all-on/gx: %u writes, wanted none",
	    model_writes());
	r = run(dtb, &cxon, AD_GX, 0);
	saw(&cxon, "gx", "Q4 power: cx was on; gx on in");

	/* The iface clock off: nothing past the GCC is touched. */
	printf("%s\n", noiface.name);
	for (st = AD_PROBE; st <= AD_SMMU; st++) {
		r = run(dtb, &noiface, (int)st, 0);
		expect_clean(&noiface, stages[st], r, EIO);
		check(model_writes() == 0, "no-iface/%s: %u writes",
		    stages[st], model_writes());
		saw(&noiface, stages[st], "Q2 gpucc not read");
	}

	/* No rail for GX: no GPU read, everything back. */
	printf("%s\n", gxdead.name);
	r = run(dtb, &gxdead, AD_SMMU, 0);
	expect_clean(&gxdead, "smmu", r, EIO);
	saw(&gxdead, "smmu", "gx did not come on");
	not_saw(&gxdead, "smmu", "Q5");
	not_saw(&gxdead, "smmu", "Q9");
	saw(&gxdead, "smmu", "all as found");

	/* gfx3d's root never takes the crystal: stop, undo. */
	printf("%s\n", stuck.name);
	r = run(dtb, &stuck, AD_SMMU, 0);
	expect_clean(&stuck, "smmu", r, EIO);
	saw(&stuck, "smmu", "core root stuck");
	not_saw(&stuck, "smmu", "Q5");
	saw(&stuck, "smmu", "all as found");

	/* TrustZone: answers, or no qcom,scm. */
	printf("tz\n");
	r = run(dtb, &boot, AD_PROBE, 1);
	expect_clean(&boot, "tz", r, OK);
	check(model_writes() == 0, "tz: %u writes", model_writes());
	saw(&boot, "tz", "Q12 tz smccc64 avail: init 1 mem 1 auth 1 supp 1 "
	    "remote 1; pas 13: supported");
	r = run(dtb, &cxon, AD_PROBE, 1);
	saw(&cxon, "tz", "Q12 tz: sys_qcomscm: 19 (no qcom,scm in the tree)");

	zap_checks(dtb, &tablet);
	cp_checks(dtb, &tablet);

	printf("%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
