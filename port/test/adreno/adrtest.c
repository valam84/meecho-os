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
#include "model.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct log ad_log = { "adreno", LEVEL_INFO, NULL };

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
	saw(&tablet, "smr", "Q13 s2cr wrote 00020000 read 00020000; back "
	    "00000000 000200ff: the table takes writes");
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
	    "00000000; 10 ms: fsr 0 gfsr 0; taken back");
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
	expect_clean(&measured, "fault", r, EIO);
	saw(&measured, "fault", "Q13 smr 0 (00000000 000200ff): id bits 7fff "
	    "mask bits 7fff (numsidb 3)");
	saw(&measured, "fault", "Q13 s2cr wrote 00020000 read 000200ff; back "
	    "00000000 000200ff: writes do NOT stick; stopping");
	saw(&measured, "fault", "Q17 smmu put back: smr 0; all as found");
	not_saw(&measured, "fault", "Q14");

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
	saw(&smr_ro, "fault", "writes do NOT stick; stopping");
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

	printf("%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
