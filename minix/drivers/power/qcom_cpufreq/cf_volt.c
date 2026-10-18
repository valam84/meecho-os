/*
 * Each corner's open-loop voltage for this chip, from its fuses
 * (port/clover/hw-cpu.md, sections 4, 5 and 7).
 *
 * The arithmetic is the vendor kernel's (Xiaomi clover-o-oss,
 * cprh-kbss-regulator.c cprh_kbss_init_regulator() and cpr3-util.c), with
 * the tables of its sdm660-regulator.dtsi: numbers and the order of the
 * steps, not its text.  port/clover/cpu-volt.py does the same steps and is
 * the reference; the stand (port/test/qcom_cpufreq) checks this file
 * against it on the tablet's fuses, on zero fuses and on random ones.
 *
 * What comes out is each corner's open-loop voltage - the vendor tree
 * makes it the corner's ceiling too - and its floor.  Closed-loop CPR,
 * which would go lower, is not run on this machine: without it the
 * open-loop voltage is the one a corner needs.
 */

#include "qcom_cpufreq.h"

#define STEP_UV		4000	/* qcom,voltage-step */
#define FUSE_STEP_UV	10000	/* CPRH_KBSS_FUSE_STEP_VOLT */
#define FUSE_BITS	6	/* sign and magnitude */
#define APM_HYST_UV	20000	/* qcom,apm-hysteresis-voltage */

/* A field: bits lo..hi of 64-bit fuse row "row". */
struct seg {
	uint8_t row, lo, hi;
};

struct bin_freq {
	unsigned bin;
	uint32_t khz[CF_MAX_CORNERS];
};

struct table {
	const char *fc_name[CF_FUSE_CORNERS];
	int32_t ref_uv[CF_FUSE_CORNERS];	/* fuse corner reference */
	struct seg fuse[CF_FUSE_CORNERS];	/* its open-loop field */
	unsigned fmax[CF_FUSE_CORNERS];		/* its last corner, 1-based */
	int32_t adjust_uv[CF_FUSE_CORNERS];	/* the tree's adjustment */
	unsigned n;				/* corners */
	struct bin_freq freq[3];		/* per speed bin */
	uint16_t ceiling_mv[CF_MAX_CORNERS], floor_mv[CF_MAX_CORNERS];
	uint16_t range_mv[CF_MAX_CORNERS];	/* floor-to-ceiling max */
};

static const struct table tables[CF_NCLUSTER] = {
	[CF_SILVER] = {		/* apc0: CPRh controller 0 */
		.fc_name = { "LowSVS", "SVS", "SVS+", "NOM", "TURBO_L1" },
		.ref_uv = { 644000, 724000, 788000, 868000, 1068000 },
		.fuse = { { 67, 34, 39 }, { 67, 28, 33 }, { 71, 3, 8 },
		    { 67, 22, 27 }, { 67, 16, 21 } },
		.fmax = { 2, 3, 4, 5, 8 },
		.adjust_uv = { -4000, 4000, 7000, 19000, -8000 },
		.n = 8,
		.freq = {
			{ 0, { 300000, 633600, 902400, 1113600, 1401600,
			    1536000, 1747200, 1843200 } },
			{ 1, { 300000, 633600, 902400, 1113600, 1401600,
			    1536000, 1747200, 1843200 } },
			{ 3, { 300000, 633600, 902400, 1113600, 1401600,
			    1536000, 1612800, 1843200 } },
		},
		.ceiling_mv = { 724, 724, 724, 788, 868, 1068, 1068, 1068 },
		.floor_mv = { 588, 588, 596, 652, 712, 744, 784, 844 },
		.range_mv = { 32, 32, 32, 40, 44, 40, 40, 40 },
	},
	[CF_GOLD] = {		/* apc1: CPRh controller 1 */
		.fc_name = { "SVS", "SVS+", "NOM", "TURBO", "TURBO_L2" },
		.ref_uv = { 724000, 788000, 868000, 988000, 1068000 },
		.fuse = { { 69, 17, 22 }, { 69, 23, 28 }, { 69, 11, 16 },
		    { 69, 5, 10 }, { 70, 42, 47 } },
		.fmax = { 2, 3, 4, 6, 7 },
		.adjust_uv = { 16000, 27000, 39000, 39000, 20000 },
		.n = 7,
		.freq = {
			{ 0, { 300000, 1113600, 1401600, 1747200, 1958400,
			    2150400, 2457600 } },
			{ 1, { 300000, 1113600, 1401600, 1747200, 1958400,
			    2150400, 2208000 } },
			{ 3, { 300000, 1113600, 1401600, 1747200, 1804800,
			    2150400, 2208000 } },
		},
		.ceiling_mv = { 724, 724, 788, 868, 988, 988, 1068 },
		.floor_mv = { 588, 596, 652, 712, 744, 784, 844 },
		.range_mv = { 40, 40, 40, 40, 66, 66, 40 },
	},
};

static const struct seg speed_bin = { 38, 29, 31 };
static const struct seg cpr_rev = { 71, 28, 30 };

/* cpr3_read_fuse_param() for a field of one segment. */
static unsigned
param(const uint64_t *rows, const struct seg *s)
{
	unsigned n = s->hi - s->lo + 1u;

	return (unsigned)((rows[s->row] >> s->lo) & ((1ull << n) - 1));
}

/* Up to the regulator's step, towards plus infinity, for any sign. */
static int64_t
round_up(int64_t v)
{
	int64_t q = v / STEP_UV;

	if (v % STEP_UV > 0)
		q++;
	return q * STEP_UV;
}

static int64_t
max64(int64_t a, int64_t b)
{
	return a > b ? a : b;
}

static int64_t
min64(int64_t a, int64_t b)
{
	return a < b ? a : b;
}

/* cpr3_interpolate(), in integers as the kernel does it. */
static int64_t
interpolate(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t x)
{
	if (x1 >= x2 || y1 > y2 || x1 > x || x > x2)
		return y2;
	return y2 - (x2 - x) * (y2 - y1) / (x2 - x1);
}

int
cf_volt(int cluster, const uint64_t rows[CF_FUSE_ROWS], struct cf_corners *c)
{
	const struct table *t;
	const uint32_t *khz = NULL;
	int64_t fv[CF_FUSE_CORNERS], ol[CF_MAX_CORNERS];
	int64_t flo[CF_MAX_CORNERS], cei[CF_MAX_CORNERS];
	unsigned fmax[CF_FUSE_CORNERS];
	unsigned i, j, lo, n, raw, steps;

	if (cluster != CF_SILVER && cluster != CF_GOLD)
		return EINVAL;
	t = &tables[cluster];
	memset(c, 0, sizeof(*c));
	c->speed_bin = param(rows, &speed_bin);
	c->cpr_rev = param(rows, &cpr_rev);
	for (i = 0; i < sizeof(t->freq) / sizeof(t->freq[0]); i++)
		if (t->freq[i].bin == c->speed_bin)
			khz = t->freq[i].khz;
	if (khz == NULL)
		return ENOENT;		/* no corners for this bin in the tree */
	n = c->n = t->n;
	for (i = 0; i < n; i++)
		c->khz[i] = khz[i];
	for (i = lo = 0; i < CF_FUSE_CORNERS; lo = t->fmax[i], i++)
		for (j = lo; j < t->fmax[i]; j++)
			c->fuse_corner[j] = i;

	/* cprh_kbss_calculate_open_loop_voltages() */
	for (i = 0; i < CF_FUSE_CORNERS; i++) {
		c->fc_name[i] = t->fc_name[i];
		raw = c->raw[i] = param(rows, &t->fuse[i]);
		steps = raw & ((1u << (FUSE_BITS - 1)) - 1);
		fv[i] = t->ref_uv[i] + ((raw & (1u << (FUSE_BITS - 1))) ?
		    -(int64_t)steps : (int64_t)steps) * FUSE_STEP_UV;
		c->fused_uv[i] = (int32_t)fv[i];
		fv[i] += t->adjust_uv[i];
	}
	for (i = 1; i < CF_FUSE_CORNERS; i++)
		fv[i] = max64(fv[i], fv[i - 1]);
	for (i = 0; i < CF_FUSE_CORNERS; i++)
		fmax[i] = t->fmax[i] - 1;
	for (j = 0; j <= fmax[0]; j++)
		ol[j] = fv[0];
	for (i = 1; i < CF_FUSE_CORNERS; i++)
		for (j = fmax[i - 1] + 1; j <= fmax[i]; j++)
			ol[j] = interpolate(khz[fmax[i - 1]] * 1000ll, fv[i - 1],
			    khz[fmax[i]] * 1000ll, fv[i], khz[j] * 1000ll);

	/* cpr3_limit_open_loop_voltages() */
	for (i = 0; i < n; i++) {
		flo[i] = t->floor_mv[i] * 1000ll;
		cei[i] = t->ceiling_mv[i] * 1000ll;
		ol[i] = min64(max64(round_up(ol[i]), flo[i]), cei[i]);
	}

	/*
	 * cprh_adjust_voltages_for_apm(): a corner whose floor and ceiling
	 * straddle the threshold keeps its open-loop voltage off the band
	 * just below it.  (The kernel also lowers such a corner's ceiling
	 * when it stays below; the next step replaces every ceiling with the
	 * open-loop voltage, so that has no effect here.)
	 */
	for (i = 0; i < n; i++) {
		if (round_up(CF_APM_UV) <= flo[i] ||
		    round_up(CF_APM_UV) > cei[i])
			continue;
		if (ol[i] >= round_up(CF_APM_UV)) {
			flo[i] = max64(flo[i],
			    round_up(CF_APM_UV) - round_up(APM_HYST_UV));
			ol[i] = max64(ol[i], flo[i]);
		}
	}

	/* cpr3_open_loop_voltage_as_ceiling() */
	for (i = 0; i < n; i++)
		cei[i] = ol[i];

	/* cpr3_limit_floor_voltages() */
	for (i = 0; i < n; i++) {
		flo[i] = max64(round_up(cei[i] - t->range_mv[i] * 1000ll),
		    flo[i]);
		ol[i] = max64(ol[i], flo[i]);
	}
	for (i = 1; i < n; i++)
		if (flo[i] < flo[i - 1]) {
			flo[i] = flo[i - 1];
			ol[i] = max64(ol[i], flo[i]);
		}

	for (i = 0; i < n; i++) {
		c->ol_uv[i] = (int32_t)ol[i];
		c->floor_uv[i] = (int32_t)flo[i];
	}
	return OK;
}

/* The corner a rate falls in: the lowest that reaches it; -1 above all. */
int
cf_corner_of(const struct cf_corners *c, uint32_t khz)
{
	unsigned i;

	for (i = 0; i < c->n; i++)
		if (khz <= c->khz[i])
			return (int)i;
	return -1;
}
