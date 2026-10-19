/*
 * The qcom_cpufreq stand (port/clover/hw-cpu.md, step 2): the service's own
 * files - cf_find.c, cf_volt.c, cf_rail.c, cf_clk.c, cf_run.c, unchanged -
 * with libsys's device tree reader on the tablet's tree plus
 * sdm660-xiaomi-clover-cpu.dtsi, against the model (model.c).
 *
 *	cftest [-v[v[v]]] TREE.dtb volt-ref.txt
 *
 * Four parts:
 *   volt      cf_volt() against port/clover/cpu-volt.py (volt-ref.txt) on
 *             every fuse set - the tablet's, zero, random ones in every bin
 *             - and on the tablet against the table of hw-cpu.md, section 7
 *   targets   cf_parse_target(): which strings name which points
 *   find      cf_find() on the tree: the regions it maps and where
 *   runs      the stages against the model, one scenario at a time; then
 *             gold=max and silver=max on every random fuse set
 *
 * A scenario fails on any violation the model records (model.h), on a
 * result other than the one it expects, or on the clusters' rates, the
 * PLL's Ls or a log line other than it expects.  Every failure is a line
 * that starts with "FAIL"; mutate.sh counts on that.
 */

#include "qcom_cpufreq.h"
#include "model.h"

#include <stdarg.h>

static unsigned fails;
static void *dtb;

static void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
fail(const char *fmt, ...)
{
	va_list ap;

	fails++;
	printf("  FAIL: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/*===========================================================================*
 *    volt                                                                   *
 *===========================================================================*/
static void
volt_set(const struct fuse_set *f)
{
	struct cf_corners c;
	unsigned i;
	int cl, r;

	for (cl = 0; cl < CF_NCLUSTER; cl++) {
		r = cf_volt(cl, f->row, &c);
		if (!f->has[cl]) {
			if (r == OK)
				fail("volt %s cluster %d: corners for a bin the "
				    "tree has none for", f->name, cl);
			continue;
		}
		if (r != OK) {
			fail("volt %s cluster %d: %d", f->name, cl, r);
			continue;
		}
		if (c.n != f->n[cl] || c.speed_bin != f->bin[cl]) {
			fail("volt %s cluster %d: %u corners, bin %u (cpu-volt.py: "
			    "%u, %u)", f->name, cl, c.n, c.speed_bin, f->n[cl],
			    f->bin[cl]);
			continue;
		}
		for (i = 0; i < c.n; i++)
			if (c.khz[i] != f->khz[cl][i] ||
			    c.ol_uv[i] != f->ol[cl][i] ||
			    c.floor_uv[i] != f->fl[cl][i]) {
				fail("volt %s cluster %d corner %u: %u kHz %d/%d "
				    "uV, cpu-volt.py %u kHz %d/%d uV", f->name, cl,
				    i + 1, c.khz[i], c.ol_uv[i], c.floor_uv[i],
				    f->khz[cl][i], f->ol[cl][i], f->fl[cl][i]);
				break;
			}
	}
}

/* hw-cpu.md, section 7, as written there - not computed by anyone here. */
static void
volt_table7(void)
{
	static const int32_t ol[2][8] = {
		{ 612, 612, 648, 696, 800, 836, 896, 920 },
		{ 680, 680, 748, 828, 888, 940, 960 } };
	static const int32_t fl[2][8] = {
		{ 588, 588, 616, 656, 756, 796, 856, 880 },
		{ 640, 640, 708, 788, 852, 876, 920 } };
	static const unsigned raw[2][5] = {
		{ 0x23, 0x28, 0x2a, 0x29, 0x2e }, { 0x26, 0x27, 0x28, 0x29, 0x2d } };
	static const int32_t fused[2][5] = {
		{ 614, 644, 688, 778, 928 }, { 664, 718, 788, 898, 938 } };
	const struct fuse_set *f = model_fuse_set("tablet");
	struct cf_corners c;
	unsigned i;
	int cl;

	for (cl = 0; cl < CF_NCLUSTER; cl++) {
		if (cf_volt(cl, f->row, &c) != OK || c.speed_bin != 1 ||
		    c.cpr_rev != 2) {
			fail("section 7: cluster %d: bin %u revision %u", cl,
			    c.speed_bin, c.cpr_rev);
			continue;
		}
		for (i = 0; i < c.n; i++)
			if (c.ol_uv[i] != ol[cl][i] * 1000 ||
			    c.floor_uv[i] != fl[cl][i] * 1000)
				fail("section 7: cluster %d corner %u: %d/%d mV, "
				    "the table says %d/%d", cl, i + 1,
				    c.ol_uv[i] / 1000, c.floor_uv[i] / 1000,
				    ol[cl][i], fl[cl][i]);
		for (i = 0; i < CF_FUSE_CORNERS; i++)
			if (c.raw[i] != raw[cl][i] ||
			    c.fused_uv[i] != fused[cl][i] * 1000)
				fail("section 7: cluster %d fuse corner %u: raw "
				    "0x%02x %d mV, the table says 0x%02x %d", cl, i,
				    c.raw[i], c.fused_uv[i] / 1000, raw[cl][i],
				    fused[cl][i]);
	}
}

/* cpu-volt.py --selftest: zero fuses, worked by hand. */
static void
volt_selftest(void)
{
	static const int32_t ol[8] = { 640, 640, 724, 788, 868, 940, 1024,
	    1060 };
	static const int32_t fl[8] = { 608, 608, 692, 748, 824, 900, 984,
	    1020 };
	uint64_t rows[CF_FUSE_ROWS];
	struct cf_corners c;
	unsigned i;

	memset(rows, 0, sizeof(rows));
	if (cf_volt(CF_SILVER, rows, &c) != OK) {
		fail("selftest: zero fuses refused");
		return;
	}
	for (i = 0; i < 8; i++)
		if (c.ol_uv[i] != ol[i] * 1000 || c.floor_uv[i] != fl[i] * 1000)
			fail("selftest: corner %u %d/%d mV, by hand %d/%d", i + 1,
			    c.ol_uv[i] / 1000, c.floor_uv[i] / 1000, ol[i], fl[i]);
	rows[67] = 0x21ull << 34;		/* LowSVS: -1 step */
	if (cf_volt(CF_SILVER, rows, &c) != OK || c.fused_uv[0] != 634000)
		fail("selftest: fuse 0x21 gives %d uV, not 634000",
		    c.fused_uv[0]);
	rows[67] = 0x01ull << 34;		/* +1 step */
	if (cf_volt(CF_SILVER, rows, &c) != OK || c.fused_uv[0] != 654000)
		fail("selftest: fuse 0x01 gives %d uV, not 654000",
		    c.fused_uv[0]);
}

static void
volt(void)
{
	unsigned i, n0 = fails;

	volt_table7();
	volt_selftest();
	for (i = 0; i < model_fuse_sets(); i++)
		volt_set(model_fuse_set_at(i));
	printf("%s volt: %u fuse sets against cpu-volt.py, section 7, "
	    "selftest\n", fails == n0 ? "ok  " : "FAIL", model_fuse_sets());
}

/*===========================================================================*
 *    targets                                                                *
 *===========================================================================*/
static void
targets(void)
{
	static const struct { int cl; const char *s; uint32_t khz; } t[] = {
		{ CF_GOLD, "1747", 1747200 },
		{ CF_GOLD, "1747.2", 1747200 },
		{ CF_GOLD, "1747.3", 0 },
		{ CF_GOLD, "1113", 1113600 },
		{ CF_GOLD, "1401.6", 1401600 },
		{ CF_GOLD, "2208", 2208000 },	/* parsed; the rail decides */
		{ CF_GOLD, "2457", 0 },		/* bin 0's, not this chip's */
		{ CF_GOLD, "300", 0 },		/* GPLL0's, not a PLL's */
		{ CF_GOLD, "1536", 0 },		/* silver's */
		{ CF_SILVER, "1536", 1536000 },
		{ CF_SILVER, "902", 902400 },	/* L 47 */
		{ CF_SILVER, "633", 0 },	/* L 33: below PLL_MIN_LVAL */
		{ CF_SILVER, "1747", 1747200 },
		{ CF_SILVER, "aux", CF_TARGET_AUX },
		{ CF_SILVER, "max", CF_TARGET_MAX },
		{ CF_SILVER, "", 0 },
		{ CF_SILVER, "1536x", 0 },
		{ CF_SILVER, "1536.", 0 },
		{ CF_SILVER, "-1536", 0 },
		{ CF_SILVER, "01536", 1536000 },
		{ CF_SILVER, "153600", 0 },
	};
	const struct fuse_set *f = model_fuse_set("tablet");
	struct cf_corners c[2];
	unsigned i, n0 = fails;
	uint32_t khz;
	int r;

	(void)cf_volt(CF_SILVER, f->row, &c[CF_SILVER]);
	(void)cf_volt(CF_GOLD, f->row, &c[CF_GOLD]);
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		khz = 0;
		r = cf_parse_target(&c[t[i].cl], t[i].s, &khz);
		if (t[i].khz == 0 ? r == OK : (r != OK || khz != t[i].khz))
			fail("target %s=\"%s\": %d, %u kHz (want %u)",
			    t[i].cl == CF_GOLD ? "gold" : "silver", t[i].s, r,
			    khz, t[i].khz);
	}
	printf("%s targets: %zu strings\n", fails == n0 ? "ok  " : "FAIL",
	    sizeof(t) / sizeof(t[0]));
}

/*===========================================================================*
 *    find                                                                   *
 *===========================================================================*/
static struct cf cf;

static void
find(void)
{
	static const struct scenario s = { .name = "find" };
	unsigned n0 = fails;

	model_reset(&s);
	if (cf_find(dtb, &cf) != OK)
		fail("find: cf_find() refused the tree");
	else {
		if (cf.cl[CF_SILVER].glb.pa != 0x17911000 ||
		    cf.cl[CF_SILVER].pll.pa != 0x17916000 ||
		    cf.cl[CF_GOLD].glb.pa != 0x17811000 ||
		    cf.cl[CF_GOLD].pll.pa != 0x17816000)
			fail("find: the clusters' blocks at 0x%llx 0x%llx 0x%llx "
			    "0x%llx", (unsigned long long)cf.cl[0].glb.pa,
			    (unsigned long long)cf.cl[0].pll.pa,
			    (unsigned long long)cf.cl[1].glb.pa,
			    (unsigned long long)cf.cl[1].pll.pa);
		if (cf.fuse.pa != 0x784000 || cf.gcc.pa != 0x148000)
			fail("find: fuses at 0x%llx, GCC page at 0x%llx",
			    (unsigned long long)cf.fuse.pa,
			    (unsigned long long)cf.gcc.pa);
		if (cf.cl[CF_SILVER].nrail != 2 ||
		    cf.cl[CF_SILVER].rail[0].base != 0x1400 ||
		    cf.cl[CF_GOLD].nrail != 3 ||
		    cf.cl[CF_GOLD].rail[0].base != 0x1700 ||
		    cf.cl[CF_GOLD].rail[1].base != 0x1a00 ||
		    cf.cl[CF_GOLD].rail[0].sid != 1)
			fail("find: the rails are not S1 / S2+S3 of SID 1");
		if (cf.cl[CF_SILVER].rail[1].sid != 3 ||
		    cf.cl[CF_SILVER].rail[1].base != 0x2000 ||
		    cf.cl[CF_GOLD].rail[2].sid != 3 ||
		    cf.cl[CF_GOLD].rail[2].base != 0x2000)
			fail("find: VDD_MX (PM660L S5, SID 3) is not a rail of "
			    "both clusters");
	}
	if (model_violation() != NULL)
		fail("find: %s", model_violation());
	printf("%s find: the tree's node, the fuses, the GCC, the arbiter\n",
	    fails == n0 ? "ok  " : "FAIL");
}

/*===========================================================================*
 *    runs                                                                   *
 *===========================================================================*/
static int
run(const struct scenario *s, int quiet)
{
	const char *t[CF_NCLUSTER];
	unsigned n0 = fails, c;
	uint32_t want;
	int r;

	model_reset(s);
	if ((r = cf_find(dtb, &cf)) != OK) {
		fail("%s: cf_find() %d", s->name, r);
		return 1;
	}
	cf.steps = s->steps < 0 ? 0 : 1;
	cf.hold = 0;
	t[CF_SILVER] = s->silver;
	t[CF_GOLD] = s->gold;
	r = cf_run(&cf, s->stage, t);
	model_end_check();

	if (model_violation() != NULL)
		fail("%s: %s", s->name, model_violation());
	if (s->fail ? r == OK : r != OK)
		fail("%s: cf_run() %d, wanted %s", s->name, r,
		    s->fail ? "a refusal" : "OK");
	for (c = 0; c < 2; c++) {
		want = s->want_khz[c];
		if (want == 0)
			want = c == M_SILVER ? (s->cfg[0] == 0x401 ? 600000 :
			    (s->l[0] ? s->l[0] : 0x48) * 19200) :
			    (s->cfg[1] == 0x501 ? (s->l[1] ? s->l[1] : 0x20) *
			    19200 : 600000);
		if (model_khz((int)c) != want)
			fail("%s: %s ends at %u kHz, not %u", s->name,
			    c == M_SILVER ? "silver" : "gold", model_khz((int)c),
			    want);
		if (s->want_l[c] != NULL &&
		    strcmp(model_l_history((int)c), s->want_l[c]) != 0)
			fail("%s: %s's PLL locked at \"%s\", not \"%s\"",
			    s->name, c == M_SILVER ? "silver" : "gold",
			    model_l_history((int)c), s->want_l[c]);
	}
	if (s->no_writes && model_writes() != 0)
		fail("%s: %u register write(s); wanted none", s->name,
		    model_writes());
	if (s->want_line != NULL && !model_saw(s->want_line))
		fail("%s: no line \"%s\"", s->name, s->want_line);
	if (s->want_line2 != NULL && !model_saw(s->want_line2))
		fail("%s: no line \"%s\"", s->name, s->want_line2);
	if (fails != n0 && model_verbose)
		model_log_dump();
	if (!quiet || fails != n0)
		printf("%s %s (%d)\n", fails == n0 ? "ok  " : "FAIL", s->name, r);
	return fails != n0;
}

#define GOLD_ON_PLL	.cfg = { 0, 0x501 }

static const struct scenario scenarios[] = {
	/* Reading: nothing written, everything said. */
	{ .name = "probe: the tablet as found", .stage = CF_PROBE,
	  .no_writes = 1, .want_l = { "", "" },
	  .want_line = "gold: the rail holds up to 1747.2 MHz",
	  .want_line2 = "silver: the rail holds up to 1536.0 MHz" },
	{ .name = "probe: speed bin and corners", .stage = CF_PROBE,
	  .no_writes = 1, .want_line = "speed bin 1, CPR fusing revision 2",
	  .want_line2 = "gold corner 4: 1747.2 MHz, open loop 828 mV" },
	{ .name = "probe: aux is GPLL0 at 600 MHz", .stage = CF_PROBE,
	  .no_writes = 1, .want_line = "aux (GPLL0): 600.0 MHz" },

	/* Step 3 and 4 of the plan, as they will be run. */
	{ .name = "gold to 1747.2 by steps", .stage = CF_SET, .gold = "1747",
	  .want_khz = { 0, 1747200 }, .want_l = { "", "3a 49 5b" },
	  .want_line = "gold: to 1113.6 MHz (L 0x3a)" },
	{ .name = "gold to 1747.2 straight", .stage = CF_SET, .gold = "1747.2",
	  .steps = -1, .want_khz = { 0, 1747200 }, .want_l = { "", "5b" } },
	{ .name = "silver to 1536 by steps", .stage = CF_SET,
	  .silver = "1536", .want_khz = { 1536000, 0 },
	  .want_l = { "49 50", "" } },
	{ .name = "both to max", .stage = CF_SET, .gold = "max",
	  .silver = "max", .want_khz = { 1536000, 1747200 },
	  .want_l = { "49 50", "3a 49 5b" } },
	{ .name = "gold back to aux", .stage = CF_SET, .gold = "aux",
	  GOLD_ON_PLL, .l = { 0, 0x5b }, .want_khz = { 0, 600000 },
	  .want_l = { "", "" } },
	{ .name = "silver to aux", .stage = CF_SET, .silver = "aux",
	  .want_khz = { 600000, 0 }, .want_l = { "", "" } },
	{ .name = "gold already there", .stage = CF_SET, .gold = "1747",
	  GOLD_ON_PLL, .l = { 0, 0x5b }, .no_writes = 1 },
	{ .name = "gold down to 1113.6", .stage = CF_SET, .gold = "1113",
	  GOLD_ON_PLL, .l = { 0, 0x5b }, .want_khz = { 0, 1113600 },
	  .want_l = { "", "3a" } },
	{ .name = "aux at 300 MHz", .stage = CF_SET, .gold = "1747",
	  .aux_cfg = 0x103, .want_khz = { 0, 1747200 },
	  .want_line = "aux (GPLL0): 300.0 MHz" },

	/* What the rails do not hold: refused before any write. */
	{ .name = "gold 1958.4 needs APM", .stage = CF_SET, .gold = "1958",
	  .fail = 1, .no_writes = 1 },
	{ .name = "silver 1747.2 needs APM", .stage = CF_SET,
	  .silver = "1747", .mv = { 900, 0, 0 }, .fail = 1, .no_writes = 1 },
	{ .name = "a point at exactly 872 mV", .fuses = "apm872",
	  .stage = CF_SET, .silver = "1843", .mv = { 900, 0, 0 }, .fail = 1,
	  .no_writes = 1 },
	{ .name = "gold rail exactly 828 mV", .stage = CF_SET, .gold = "1747",
	  .mv = { 0, 828, 828 }, .want_khz = { 0, 1747200 } },
	{ .name = "gold rail 824 mV", .stage = CF_SET, .gold = "1747",
	  .mv = { 0, 824, 824 }, .fail = 1, .no_writes = 1 },
	{ .name = "gold S3 below S2", .stage = CF_SET, .gold = "1747",
	  .mv = { 0, 868, 824 }, .fail = 1, .no_writes = 1 },
	{ .name = "gold S2 below S3", .stage = CF_SET, .gold = "1747",
	  .mv = { 0, 824, 868 }, .fail = 1, .no_writes = 1 },
	{ .name = "silver's rail is S1, not gold's", .stage = CF_SET,
	  .silver = "1536", .mv = { 832, 900, 900 }, .fail = 1,
	  .no_writes = 1 },
	{ .name = "gold's rail is S2+S3, not silver's", .stage = CF_SET,
	  .gold = "1747", .mv = { 900, 824, 824 }, .fail = 1, .no_writes = 1 },
	{ .name = "gold=max at a low rail", .stage = CF_SET, .gold = "max",
	  .mv = { 0, 760, 760 }, .want_khz = { 0, 1401600 },
	  .want_l = { "", "3a 49" } },

	/* VDD_MX holds the arrays: a point it does not hold is refused. */
	{ .name = "MX below gold's 1747.2", .stage = CF_SET, .gold = "1747",
	  .mx_mv = 820, .fail = 1, .no_writes = 1 },
	{ .name = "MX below silver's 1536", .stage = CF_SET,
	  .silver = "1536", .mx_mv = 832, .fail = 1, .no_writes = 1 },
	{ .name = "MX caps max", .stage = CF_SET, .gold = "max",
	  .silver = "max", .mx_mv = 800, .want_khz = { 1401600, 1401600 },
	  .want_l = { "49", "3a 49" } },
	{ .name = "MX drops under gold's run", .stage = CF_SET,
	  .gold = "1747", .drop_cl = M_GOLD, .drop_after = 1, .drop_reg = 3,
	  .drop_mv = 800, .fail = 1, .want_khz = { 0, 1401600 },
	  .want_l = { "", "3a 49" } },

	/* The rail moves under the run. */
	{ .name = "gold's rail drops after 1113.6", .stage = CF_SET,
	  .gold = "1747", .drop_cl = M_GOLD, .drop_after = 1, .drop_reg = 1,
	  .drop_mv = 720, .fail = 1, .want_khz = { 0, 1113600 },
	  .want_l = { "", "3a" } },
	{ .name = "gold's rail drops while its PLL locks", .stage = CF_SET,
	  .gold = "1747", .drop_cl = M_GOLD, .drop_l = 0x49, .drop_reg = 2,
	  .drop_mv = 720, .fail = 1, .want_khz = { 0, 600000 },
	  .want_l = { "", "3a 49" } },
	{ .name = "silver's rail drops below where it runs", .stage = CF_SET,
	  .silver = "1536", .drop_cl = M_SILVER, .drop_after = 1,
	  .drop_reg = 0, .drop_mv = 790, .fail = 1,
	  .want_khz = { 600000, 0 }, .want_l = { "49", "" },
	  .want_line = "the rail no longer holds 1401.6 MHz" },

	/* The hardware does not do what it should. */
	{ .name = "gold's PLL never locks", .stage = CF_SET, .gold = "1747",
	  .pll_dead = { 0, 1 }, .fail = 1, .want_l = { "", "" } },
	{ .name = "gold's PLL locks only up to L 0x49", .stage = CF_SET,
	  .gold = "1747", .vco_max = { 0, 0x49 }, .fail = 1,
	  .want_khz = { 0, 600000 }, .want_l = { "", "3a 49" } },
	{ .name = "silver's PLL never locks", .stage = CF_SET,
	  .silver = "1536", .pll_dead = { 1, 0 }, .fail = 1,
	  .want_khz = { 600000, 0 } },
	{ .name = "gold's mux never finishes", .stage = CF_SET,
	  .gold = "1747", .mux_stuck = { 0, 1 }, .fail = 1 },
	{ .name = "gold's L does not take", .stage = CF_SET, .gold = "1747",
	  .l_ro = { 0, 1 }, .fail = 1 },
	{ .name = "aux's root off", .stage = CF_SET, .silver = "1536",
	  GOLD_ON_PLL, .l = { 0, 0x5b }, .aux_off = 1, .fail = 1,
	  .no_writes = 1 },
	{ .name = "aux's root off, gold on its PLL", .stage = CF_SET,
	  .gold = "1113", GOLD_ON_PLL, .l = { 0, 0x5b }, .aux_off = 1,
	  .fail = 1, .no_writes = 1 },
	{ .name = "silver's mux divides", .stage = CF_SET, .silver = "1536",
	  .cfg = { 0x503, 0 }, .want_khz = { 691200, 0 }, .fail = 1,
	  .no_writes = 1 },
	{ .name = "silver on the PLL's main output", .stage = CF_SET,
	  .silver = "1536", .cfg = { 0x301, 0 }, .fail = 1, .no_writes = 1 },
	{ .name = "S1 is not an FTS426", .stage = CF_SET, .silver = "1536",
	  .s1_subtype = 0x08, .fail = 1, .no_writes = 1 },
	{ .name = "S3 disabled", .stage = CF_SET, .gold = "1747",
	  .s3_disabled = 1, .fail = 1, .no_writes = 1 },
	{ .name = "a flaky arbiter: read again", .stage = CF_SET,
	  .gold = "1747", .silver = "1536", .spmi_flaky = 13,
	  .want_khz = { 1536000, 1747200 } },
	{ .name = "the arbiter does not answer", .stage = CF_SET,
	  .gold = "1747", .spmi_dead = 1, .fail = 1, .no_writes = 1 },
	{ .name = "a speed bin with no table", .fuses = "bin2",
	  .stage = CF_PROBE, .fail = 1, .no_writes = 1 },

	/* Asked for nonsense: refused before any write. */
	{ .name = "stage=set without a target", .stage = CF_SET, .fail = 1,
	  .no_writes = 1 },
	{ .name = "gold=1536", .stage = CF_SET, .gold = "1536", .fail = 1,
	  .no_writes = 1 },
	{ .name = "a bad silver target stops gold too", .stage = CF_SET,
	  .gold = "1747", .silver = "633", .fail = 1, .no_writes = 1 },
};

/*
 * gold=max, then silver=max, on every random fuse set and on the two made
 * to sit on an edge: apm872 (a point at 872 mV, which max must pass over;
 * its gold 1747.2 MHz is at 868 mV, which gold's 868 mV rail holds) and
 * edge868.  What max must reach is the reference's highest point a PLL
 * makes and the boot loader's rails hold.
 */
static void
random_runs(void)
{
	struct scenario s;
	const struct fuse_set *f;
	char name[96];
	unsigned i, k, n = 0, n0 = fails, held;
	int cl, top;

	for (i = 0; i < model_fuse_sets(); i++) {
		f = model_fuse_set_at(i);
		if (strncmp(f->name, "random", 6) != 0 &&
		    strcmp(f->name, "apm872") != 0 &&
		    strcmp(f->name, "edge868") != 0)
			continue;
		for (cl = 0; cl < 2; cl++) {
			memset(&s, 0, sizeof(s));
			snprintf(name, sizeof(name), "%s %s=max", f->name,
			    cl == M_GOLD ? "gold" : "silver");
			s.name = name;
			s.fuses = f->name;
			s.stage = CF_SET;
			if (cl == M_GOLD)
				s.gold = "max";
			else
				s.silver = "max";
			/* The reference's highest point a PLL makes and the
			 * boot loader's rails hold. */
			held = cl == M_SILVER ? 872 : 868;
			for (k = 0, top = -1; k < f->n[cl]; k++)
				if (f->khz[cl][k] % 19200 == 0 &&
				    f->khz[cl][k] / 19200 >= 43 &&
				    f->ol[cl][k] < 872000 &&
				    f->ol[cl][k] <= (int32_t)held * 1000)
					top = (int)k;
			if (top < 0) {
				s.fail = 1;
				s.no_writes = 1;
			} else
				s.want_khz[cl] = f->khz[cl][top];
			n++;
			(void)run(&s, 1);
		}
	}
	printf("%s random: %u runs of gold=max and silver=max\n",
	    fails == n0 ? "ok  " : "FAIL", n);
}

/*===========================================================================*
 *    the governor                                                           *
 *===========================================================================*/
/* Which core is in which cluster, numbered as the kernel numbers them. */
static void
gov_cpus(void)
{
	static const struct { uint64_t boot; int ok; const char *want; } t[] = {
		{ 0x0, 1, "SGGGGSSS" },		/* the tree: 100..103, 0..3 */
		{ 0x100, 1, "GGGGSSSS" },
		{ 0x2, 1, "SGGGSSGS" },	/* 0x2 and 0x100 swapped */
		{ 0x7, 0, NULL },		/* not in the tree */
	};
	int map[CF_MAX_CPUS];
	char got[CF_MAX_CPUS + 1];
	unsigned i, k, n, n0 = fails;
	int r;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		n = 0;
		r = cf_gov_cpus(dtb, t[i].boot, map, &n);
		if (!t[i].ok) {
			if (r == OK)
				fail("cpus: boot 0x%llx accepted",
				    (unsigned long long)t[i].boot);
			continue;
		}
		for (k = 0; k < n && k < CF_MAX_CPUS; k++)
			got[k] = map[k] == CF_GOLD ? 'G' : map[k] == CF_SILVER ?
			    'S' : '?';
		got[k] = '\0';
		if (r != OK || n != 8 || strcmp(got, t[i].want) != 0)
			fail("cpus: boot 0x%llx: %d, %u cores \"%s\", not \"%s\"",
			    (unsigned long long)t[i].boot, r, n, got, t[i].want);
	}
	printf("%s gov cpus: the tree's cores, the boot core first\n",
	    fails == n0 ? "ok  " : "FAIL");
}

/* The decision alone, on gold's points of this chip. */
static void
gov_pick(void)
{
	static const struct {
		int policy;
		unsigned cur, load, low;	/* before */
		unsigned want, low_after;
	} t[] = {
		{ CF_GOV_PERFORMANCE, 0, 0, 0, 3, 0 },
		{ CF_GOV_POWERSAVE, 3, 100, 0, 0, 0 },
		{ CF_GOV_ONDEMAND, 0, 80, 0, 3, 0 },	/* up% straight up */
		{ CF_GOV_ONDEMAND, 0, 79, 0, 1, 0 },	/* 600*79 > 1113.6*60? no */
		{ CF_GOV_ONDEMAND, 0, 70, 0, 1, 0 },
		{ CF_GOV_ONDEMAND, 0, 60, 0, 0, 0 },	/* 600 holds 60% */
		{ CF_GOV_ONDEMAND, 3, 10, 0, 3, 1 },	/* down waits */
		{ CF_GOV_ONDEMAND, 3, 10, 1, 3, 2 },
		{ CF_GOV_ONDEMAND, 3, 10, 2, 0, 0 },	/* third: down */
		{ CF_GOV_ONDEMAND, 3, 40, 2, 2, 0 },	/* 1401.6*60 >= 1747.2*40 */
		{ CF_GOV_ONDEMAND, 3, 50, 2, 3, 0 },	/* no lower point holds it */
		{ CF_GOV_ONDEMAND, 2, 79, 2, 3, 0 },	/* up resets the run */
		{ CF_GOV_ONDEMAND, 1, 0, 2, 0, 0 },
	};
	static const uint32_t pts[4] = { 600000, 1113600, 1401600, 1747200 };
	struct cf_govpol p = { 0, 80, 60, 3 };
	struct cf_govcl g;
	unsigned i, w, n0 = fails;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		memset(&g, 0, sizeof(g));
		g.n = 4;
		memcpy(g.khz, pts, sizeof(pts));
		g.cur = t[i].cur;
		g.low = t[i].low;
		p.policy = t[i].policy;
		w = cf_gov_pick(&p, &g, t[i].load);
		if (w != t[i].want || g.low != t[i].low_after)
			fail("pick %u: policy %d at %u, load %u, run %u: point %u "
			    "run %u, not %u run %u", i, t[i].policy, t[i].cur,
			    t[i].load, t[i].low, w, g.low, t[i].want,
			    t[i].low_after);
	}
	printf("%s gov pick: %zu decisions\n", fails == n0 ? "ok  " : "FAIL",
	    sizeof(t) / sizeof(t[0]));
}

/* The governor's start on the model: stage=gov through cf_run(). */
static int
gov_start(const struct scenario *s, int policy, const char *gcap,
	const char *scap)
{
	const char *t[CF_NCLUSTER];
	int r;

	model_reset(s);
	if ((r = cf_find(dtb, &cf)) != OK)
		return r;
	cf.pol = (struct cf_govpol){ policy, 80, 60, 3 };
	t[CF_SILVER] = scap;
	t[CF_GOLD] = gcap;
	return cf_run(&cf, CF_GOV, t);
}

static void
gov_want(const char *name, uint32_t silver, uint32_t gold)
{
	if (model_khz(M_SILVER) != silver || model_khz(M_GOLD) != gold)
		fail("%s: silver %u, gold %u kHz; wanted %u, %u", name,
		    model_khz(M_SILVER), model_khz(M_GOLD), silver, gold);
}

/* A load, 0..100: idle half the time, some work, sometimes all. */
static unsigned
rnd_load(void)
{
	static uint32_t x = 12345;
	unsigned v;

	x = x * 1103515245u + 12345u;
	v = (x >> 16) % 100;
	return v < 50 ? 0 : v < 85 ? v - 30 : 100;
}

/* Both clusters, one period with these loads. */
static void
gov_period(unsigned silver, unsigned gold)
{
	(void)cf_gov_step(&cf, &cf.cl[CF_SILVER], &cf.gov[CF_SILVER], &cf.pol,
	    silver, 10);
	(void)cf_gov_step(&cf, &cf.cl[CF_GOLD], &cf.gov[CF_GOLD], &cf.pol,
	    gold, 10);
}

static void
gov_runs(void)
{
	static const struct scenario tablet = { .name = "gov" };
	static const struct scenario lowmx = { .name = "gov", .mx_mv = 800 };
	unsigned i, n0 = fails;
	int r;

	/* ondemand starts at the top, every point held. */
	r = gov_start(&tablet, CF_GOV_ONDEMAND, NULL, NULL);
	if (r != OK)
		fail("gov ondemand: start %d", r);
	gov_want("gov ondemand start", 1536000, 1747200);
	if (cf.gov[CF_SILVER].n != 5 || cf.gov[CF_GOLD].n != 4)
		fail("gov: %u silver and %u gold points, not 5 and 4 (aux, "
		    "902.4..1536; aux, 1113.6..1747.2)", cf.gov[CF_SILVER].n,
		    cf.gov[CF_GOLD].n);

	/* Idle: down after three periods, to aux, the PLLs off. */
	gov_period(0, 0);
	gov_period(0, 0);
	gov_want("gov idle, two periods", 1536000, 1747200);
	gov_period(0, 0);
	gov_want("gov idle, three periods", 600000, 600000);
	if (model_pll_mode(M_SILVER) != 0 || model_pll_mode(M_GOLD) != 0)
		fail("gov idle: the PLLs not parked (modes %u %u)",
		    model_pll_mode(M_SILVER), model_pll_mode(M_GOLD));

	/* Some work: up at once, as far as it needs. */
	gov_period(70, 30);
	gov_want("gov 70% on silver at 600", 902400, 600000);
	gov_period(100, 95);
	gov_want("gov busy", 1536000, 1747200);
	/* Back and forth a thousand times: never a violation. */
	for (i = 0; i < 1000; i++) {
		gov_period(rnd_load(), rnd_load());
	}
	model_end_check();
	if (model_violation() != NULL)
		fail("gov ondemand: %s", model_violation());

	/* A flaky arbiter: the same, never frozen, never a violation. */
	{
		static const struct scenario flaky = { .name = "gov",
		    .spmi_flaky = 13 };

		r = gov_start(&flaky, CF_GOV_ONDEMAND, NULL, NULL);
		if (r != OK)
			fail("gov flaky: start %d", r);
		for (i = 0; i < 1000; i++) {
			gov_period(rnd_load(), rnd_load());
			if (i % 10 == 0) {
				(void)cf_gov_check(&cf, &cf.cl[CF_SILVER],
				    &cf.gov[CF_SILVER]);
				(void)cf_gov_check(&cf, &cf.cl[CF_GOLD],
				    &cf.gov[CF_GOLD]);
			}
		}
		if (cf.gov[CF_SILVER].frozen || cf.gov[CF_GOLD].frozen)
			fail("gov flaky: frozen (silver %d, gold %d)",
			    cf.gov[CF_SILVER].frozen, cf.gov[CF_GOLD].frozen);
		if (cf.gov[CF_SILVER].switches < 100 ||
		    cf.gov[CF_GOLD].switches < 100)
			fail("gov flaky: %u and %u switches (silver %u points, at %u, off %d, fails %u)", cf.gov[CF_SILVER].switches, cf.gov[CF_GOLD].switches, cf.gov[CF_SILVER].n, cf.gov[CF_SILVER].cur, cf.gov[CF_SILVER].off_point, cf.gov[CF_SILVER].fails);
		model_end_check();
		if (model_violation() != NULL)
			fail("gov flaky: %s", model_violation());
	}

	/* MX comes down under a point that sits still. */
	r = gov_start(&tablet, CF_GOV_ONDEMAND, NULL, NULL);
	model_set_rail(3, 800);
	model_hold_now();
	for (i = 0; i < 3; i++) {
		(void)cf_gov_check(&cf, &cf.cl[CF_SILVER], &cf.gov[CF_SILVER]);
		(void)cf_gov_check(&cf, &cf.cl[CF_GOLD], &cf.gov[CF_GOLD]);
		gov_period(100, 100);
	}
	gov_want("gov MX at 800 mV", 1401600, 1401600);
	model_end_check();
	if (model_violation() != NULL)
		fail("gov MX drops: %s", model_violation());

	/* Started with MX low: the points are what it holds. */
	r = gov_start(&lowmx, CF_GOV_PERFORMANCE, NULL, NULL);
	gov_want("gov performance, MX 800 mV", 1401600, 1401600);

	/* powersave: aux, the PLLs off, whatever the load. */
	r = gov_start(&tablet, CF_GOV_POWERSAVE, NULL, NULL);
	gov_period(100, 100);
	gov_want("gov powersave", 600000, 600000);
	if (model_pll_mode(M_SILVER) != 0 || model_pll_mode(M_GOLD) != 0)
		fail("gov powersave: the PLLs not parked");

	/* Caps. */
	r = gov_start(&tablet, CF_GOV_PERFORMANCE, "1401.6", "aux");
	if (r != OK)
		fail("gov caps: start %d", r);
	gov_want("gov performance, caps", 600000, 1401600);
	r = gov_start(&tablet, CF_GOV_ONDEMAND, "2208", NULL);
	gov_want("gov cap above the rails", 1536000, 1747200);
	if (gov_start(&tablet, CF_GOV_ONDEMAND, "633", NULL) == OK)
		fail("gov: cap 633 taken");

	/* The arbiter dead: nothing governed, nothing written. */
	{
		static const struct scenario dead = { .name = "gov",
		    .spmi_dead = 1 };

		if (gov_start(&dead, CF_GOV_ONDEMAND, NULL, NULL) == OK)
			fail("gov: started with a dead arbiter");
		if (model_writes() != 0)
			fail("gov: %u writes with a dead arbiter",
			    model_writes());
	}
	model_end_check();
	if (model_violation() != NULL)
		fail("gov: %s", model_violation());
	printf("%s gov runs: start, idle, load, MX under a point, powersave, "
	    "caps\n", fails == n0 ? "ok  " : "FAIL");
}

static void *
slurp(const char *path)
{
	FILE *fp;
	long n;
	void *p;

	if ((fp = fopen(path, "rb")) == NULL || fseek(fp, 0, SEEK_END) != 0 ||
	    (n = ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET) != 0 ||
	    (p = malloc((size_t)n)) == NULL ||
	    fread(p, 1, (size_t)n, fp) != (size_t)n) {
		fprintf(stderr, "cftest: cannot read %s\n", path);
		exit(2);
	}
	fclose(fp);
	return p;
}

int
main(int argc, char **argv)
{
	unsigned i;

	while (argc > 1 && strncmp(argv[1], "-v", 2) == 0) {
		model_verbose = (int)strlen(argv[1]) - 1;
		argc--;
		argv++;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: cftest [-v] TREE.dtb volt-ref.txt\n");
		return 2;
	}
	dtb = slurp(argv[1]);
	if (model_load_fuses(argv[2]) != 0 || model_fuse_set("tablet") ==
	    NULL) {
		fprintf(stderr, "cftest: no fuse sets in %s\n", argv[2]);
		return 2;
	}
	volt();
	targets();
	find();
	for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++)
		(void)run(&scenarios[i], 0);
	random_runs();
	gov_cpus();
	gov_pick();
	gov_runs();
	printf("%s: %u failure%s\n", fails ? "FAIL" : "ok", fails,
	    fails == 1 ? "" : "s");
	return fails != 0;
}
