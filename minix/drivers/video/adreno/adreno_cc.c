/*
 * The clock blocks around the GPU: the GCC (shared with the rest of the
 * SoC; qcom_soc knows its tables and reads it) and the GPU's own clock
 * controller, the GPUCC (sdm660_gpucc.h, generated from Linux's
 * gpucc-sdm660.c by port/clover/gen-sdm660-tables.py).  Registers and
 * sequences: port/clover/hw-gpu.md, §2.
 *
 * What gates what, and so the order of everything here:
 *   - the GCC is always readable;
 *   - the GPUCC is read only while the GPU's AHB clock (the GPU node's
 *     "iface", GCC_GPU_CFG_AHB_CLK) runs - Linux keeps that clock critical
 *     and never says the GPUCC needs it, so it is assumed to;
 *   - the GPU and its SMMU are read only while their clocks run and the
 *     GPUCC's domains are powered (cc_gpu_clocked(), cc_smmu_clocked()).
 *
 * Bringing things up is Linux's gdsc.c and clk-branch.c order, at the
 * crystal's 19.2 MHz only: no PLL is programmed, no voltage asked for.
 * Every write is remembered and undone by cc_power_down(), which then
 * checks that what it can see is as it was found.  All writes go through
 * ad_wr(), so a host model sees each one as the hardware would.
 */

#include "adreno.h"
#include "adreno_regs.h"
#include "qcom_soc.h"
#include "sdm660_gpucc.h"

#include <string.h>

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

#define XO_KHZ		19200u
#define GPLL0_KHZ	600000u		/* [I] hw-gpu.md §2: 50 MHz = it/12 */

/* What cc_power_up() did, for cc_power_down() to undo. */
static struct {
	unsigned gcc[AD_MAXCLK];	/* GCC branches it enabled */
	unsigned ngcc;
	unsigned gpb[AD_MAXCLK];	/* GPUCC branches it enabled */
	unsigned ngpb;
	struct { uint32_t cmd, cfg; } rcg[AD_MAXCLK];	/* roots it moved */
	unsigned nrcg;
	int gdsc[4];			/* GPUCC domains it powered, in order */
	unsigned ngdsc;
	int cxc_set;			/* FORCE_MEM_CORE_ON it set */
	uint32_t cxc;
} undo;

/* Before, for the comparison after. */
static struct {
	int gcc_on[AD_MAXCLK * 3];
	unsigned gcc_id[AD_MAXCLK * 3];
	unsigned ngcc;
	int gpb_on[NELEM(sdm660_gpucc_branch)];
	int gdsc_on[NELEM(sdm660_gpucc_gdsc)];
	uint32_t rcg_cfg[NELEM(sdm660_gpucc_rcg)];
} found;

/*===========================================================================*
 *    tables                                                                  *
 *===========================================================================*/
static int
gp_branch(unsigned id)
{
	unsigned i;

	for (i = 0; i < NELEM(sdm660_gpucc_branch); i++)
		if (sdm660_gpucc_branch[i].id == id)
			return (int)i;
	return -1;
}

static int
gp_gdsc(int id)
{
	unsigned i;

	for (i = 0; i < NELEM(sdm660_gpucc_gdsc); i++)
		if ((int)sdm660_gpucc_gdsc[i].id == id)
			return (int)i;
	return -1;
}

static int
gp_rcg(uint32_t cmd)
{
	unsigned i;

	for (i = 0; i < NELEM(sdm660_gpucc_rcg); i++)
		if (sdm660_gpucc_rcg[i].cmd == cmd)
			return (int)i;
	return -1;
}

/*
 * A short name for the log: "GPUCC_GFX3D_CLK" -> "gfx3d", "GPU_GX_GDSC" ->
 * "gx", "GPU_PLL0_PLL" -> "pll0", "GFX3D_CLK_SRC" -> "gfx3d" - which is
 * also how the parent maps name the PLLs (sdm660_gpucc_rcg[].src).
 */
static const char *
short_name(const char *name, char *buf, size_t size)
{
	static const char *const prefix[] = { "GPUCC_", "GPU_" };
	static const char *const suffix[] = { "_CLK_SRC", "_CLK", "_GDSC",
	    "_BCR", "_PLL" };
	const char *p = name;
	size_t i, n, k;

	for (k = 0; k < NELEM(prefix); k++)
		if (strncmp(p, prefix[k], strlen(prefix[k])) == 0) {
			p += strlen(prefix[k]);
			break;
		}
	n = strlen(p);
	for (k = 0; k < NELEM(suffix); k++)
		if (n > strlen(suffix[k]) &&
		    strcmp(p + n - strlen(suffix[k]), suffix[k]) == 0) {
			n -= strlen(suffix[k]);
			break;
		}
	for (i = 0; i < n && i + 1 < size; i++)
		buf[i] = (char)(p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 'a' :
		    p[i]);
	buf[i] = '\0';
	return buf;
}

/*===========================================================================*
 *    looks                                                                   *
 *===========================================================================*/
int
cc_gcc_on(unsigned id, uint32_t *reg)
{
	return qsoc_clk_on(id, reg, NULL);
}

static int
cbcr_on(uint32_t v)
{
	return !(v & CBCR_OFF) || CBCR_NOC(v) == CBCR_NOC_ON;
}

/* The GPU's AHB clock: the GPU node's "iface", a GCC branch. */
static int
iface_on(void)
{
	unsigned i;

	for (i = 0; i < ad.dt.nclk; i++)
		if (strcmp(ad.dt.clk[i].name, "iface") == 0 &&
		    ad.dt.clk[i].prov == AD_GCC)
			return cc_gcc_on(ad.dt.clk[i].id, NULL) == 1;
	return 0;	/* none named: nothing is known to be safe */
}

static int
gdsc_on(unsigned i)
{
	uint32_t st = sdm660_gpucc_gdsc[i].status != 0 ?
	    sdm660_gpucc_gdsc[i].status : sdm660_gpucc_gdsc[i].gdscr;

	return (ad_rd(&ad.gpucc, st) & GDSC_PWR_ON) != 0;
}

/* A domain and every parent of it on. */
static int
gdsc_chain_on(int id)
{
	int i;

	while (id >= 0) {
		if ((i = gp_gdsc(id)) < 0 || !gdsc_on((unsigned)i))
			return 0;
		id = sdm660_gpucc_gdsc[i].parent;
	}
	return 1;
}

static uint32_t
pll_khz(unsigned base)
{
	uint32_t l = ad_rd(&ad.gpucc, base + PLL_L);
	uint64_t a = ad_rd(&ad.gpucc, base + PLL_ALPHA) |
	    (uint64_t)(ad_rd(&ad.gpucc, base + PLL_ALPHA_U) & 0xff) << 32;
	uint64_t khz = (uint64_t)XO_KHZ * l;

	if (ad_rd(&ad.gpucc, base + PLL_USER_CTL) & PLL_USER_ALPHA_EN)
		khz += ((uint64_t)XO_KHZ * a) >> 40;
	return (uint32_t)khz;
}

static uint32_t
src_khz(const char *src)
{
	unsigned i;

	if (src == NULL)
		return 0;
	if (strcmp(src, "xo") == 0)
		return XO_KHZ;
	if (strcmp(src, "gpll0") == 0)
		return GPLL0_KHZ;
	if (strcmp(src, "gpll0_div") == 0)
		return GPLL0_KHZ / 2;
	for (i = 0; i < NELEM(sdm660_gpucc_pll); i++) {
		char sn[24];

		if (strcmp(src, short_name(sdm660_gpucc_pll[i].name, sn,
		    sizeof(sn))) == 0)
			return pll_khz(sdm660_gpucc_pll[i].base);
	}
	return 0;
}

/* Every GCC clock the GPU, its SMMU or the GPUCC take, each once. */
static unsigned
gcc_clocks(const struct ad_clk **list, unsigned max)
{
	const struct ad_clk *all[3] = { ad.dt.clk, ad.dt.smmu_clk,
	    ad.dt.gpucc_in };
	unsigned cnt[3] = { ad.dt.nclk, ad.dt.smmu_nclk, ad.dt.gpucc_nin };
	unsigned g, i, j, n = 0;

	for (g = 0; g < 3; g++)
		for (i = 0; i < cnt[g]; i++) {
			if (all[g][i].prov != AD_GCC)
				continue;
			for (j = 0; j < n; j++)
				if (list[j]->id == all[g][i].id)
					break;
			if (j == n && n < max)
				list[n++] = &all[g][i];
		}
	return n;
}

int
cc_report(void)
{
	const struct ad_clk *gl[AD_MAXCLK * 3];
	char line[200], part[64], sn[24];
	uint32_t v, cmd, cfg;
	unsigned i, n;
	size_t len;
	int on;

	/* Q1: the GCC, always readable. */
	n = gcc_clocks(gl, NELEM(gl));
	len = (size_t)snprintf(line, sizeof(line), "Q1 gcc");
	found.ngcc = n;
	for (i = 0; i < n; i++) {
		on = cc_gcc_on(gl[i]->id, &v);
		found.gcc_id[i] = gl[i]->id;
		found.gcc_on[i] = on;
		snprintf(part, sizeof(part), " %s %s(%x)", gl[i]->name,
		    on == 1 ? "on" : on == 0 ? "off" : "?", v);
		if (len < sizeof(line))
			len += strlcpy(line + len, part, sizeof(line) - len);
	}
	log_info(&ad_log, "%s\n", line);

	if (!iface_on()) {
		log_warn(&ad_log, "Q2 gpucc not read: the GPU's iface clock "
		    "is off or unnamed - its registers could stall the bus; "
		    "refusing\n");
		return EIO;
	}

	/* Q2: domains, branches, resets of the GPUCC. */
	len = (size_t)snprintf(line, sizeof(line), "Q2 gpucc");
	for (i = 0; i < NELEM(sdm660_gpucc_gdsc); i++) {
		found.gdsc_on[i] = gdsc_on(i);
		snprintf(part, sizeof(part), " %s %s(%x)",
		    short_name(sdm660_gpucc_gdsc[i].name, sn, sizeof(sn)),
		    found.gdsc_on[i] ? "on" : "off",
		    ad_rd(&ad.gpucc, sdm660_gpucc_gdsc[i].gdscr));
		if (len < sizeof(line))
			len += strlcpy(line + len, part, sizeof(line) - len);
	}
	for (i = 0; i < NELEM(sdm660_gpucc_branch); i++) {
		v = ad_rd(&ad.gpucc, sdm660_gpucc_branch[i].cbcr);
		found.gpb_on[i] = cbcr_on(v);
		snprintf(part, sizeof(part), " %s %s%s",
		    short_name(sdm660_gpucc_branch[i].name, sn, sizeof(sn)),
		    found.gpb_on[i] ? "on" : "off",
		    sdm660_gpucc_branch[i].hwcg != 0 &&
		    (v & (1u << sdm660_gpucc_branch[i].hwcg)) ? "+hw" : "");
		if (len < sizeof(line))
			len += strlcpy(line + len, part, sizeof(line) - len);
	}
	for (i = 0; i < NELEM(sdm660_gpucc_bcr); i++)
		if (ad_rd(&ad.gpucc, sdm660_gpucc_bcr[i].bcr) & BCR_ASSERT) {
			snprintf(part, sizeof(part), " %s-reset",
			    short_name(sdm660_gpucc_bcr[i].name, sn,
			    sizeof(sn)));
			if (len < sizeof(line))
				len += strlcpy(line + len, part,
				    sizeof(line) - len);
		}
	log_info(&ad_log, "%s\n", line);

	/* Q3: PLLs and roots. */
	len = (size_t)snprintf(line, sizeof(line), "Q3");
	for (i = 0; i < NELEM(sdm660_gpucc_pll); i++) {
		unsigned b = sdm660_gpucc_pll[i].base;
		uint32_t mode = ad_rd(&ad.gpucc, b + PLL_MODE);
		uint32_t khz = pll_khz(b);

		snprintf(part, sizeof(part), " %s %s%s L %x %u.%u MHz",
		    short_name(sdm660_gpucc_pll[i].name, sn, sizeof(sn)),
		    (mode & PLL_MODE_LOCK_DET) ? "lock" : "nolock",
		    (mode & PLL_MODE_OUTCTRL) ? "+out" : "",
		    ad_rd(&ad.gpucc, b + PLL_L), khz / 1000, (khz % 1000) / 100);
		if (len < sizeof(line))
			len += strlcpy(line + len, part, sizeof(line) - len);
	}
	log_info(&ad_log, "%s\n", line);
	len = (size_t)snprintf(line, sizeof(line), "Q3");
	for (i = 0; i < NELEM(sdm660_gpucc_rcg); i++) {
		unsigned hid, src;
		uint32_t khz;

		cmd = ad_rd(&ad.gpucc, sdm660_gpucc_rcg[i].cmd);
		cfg = ad_rd(&ad.gpucc, sdm660_gpucc_rcg[i].cmd + 4);
		found.rcg_cfg[i] = cfg;
		hid = RCG_CFG_HID(cfg);
		src = RCG_CFG_SRC(cfg);
		khz = src_khz(sdm660_gpucc_rcg[i].src[src]);
		if (hid != 0)
			khz = (uint32_t)((uint64_t)khz * 2 / (hid + 1));
		snprintf(part, sizeof(part), " %s %s/%u%s %u.%u%s",
		    short_name(sdm660_gpucc_rcg[i].name, sn, sizeof(sn)),
		    sdm660_gpucc_rcg[i].src[src] != NULL ?
		    sdm660_gpucc_rcg[i].src[src] : "src?",
		    hid == 0 ? 1 : (hid + 1) / 2,
		    hid != 0 && !(hid & 1) ? ".5" : "", khz / 1000,
		    (khz % 1000) / 100,
		    (cmd & RCG_CMD_ROOT_OFF) ? " off" : "");
		if (len < sizeof(line))
			len += strlcpy(line + len, part, sizeof(line) - len);
	}
	log_info(&ad_log, "%s\n", line);
	return OK;
}

int
cc_gpu_clocked(const char **why)
{
	unsigned i;
	int b;

	*why = NULL;
	for (i = 0; i < ad.dt.nclk && *why == NULL; i++) {
		if (ad.dt.clk[i].prov == AD_GCC) {
			if (cc_gcc_on(ad.dt.clk[i].id, NULL) != 1)
				*why = ad.dt.clk[i].name;
		} else if ((b = gp_branch(ad.dt.clk[i].id)) < 0 ||
		    !cbcr_on(ad_rd(&ad.gpucc, sdm660_gpucc_branch[b].cbcr)))
			*why = ad.dt.clk[i].name;
	}
	/* The GPU's own domain is the RPM's (VDDMX); the GPUCC domain its
	 * SMMU names is the one around the core (hw-gpu.md, §1). */
	if (*why == NULL && !gdsc_chain_on(ad.dt.smmu_pd))
		*why = "its GPUCC power domain";
	return *why == NULL;
}

int
cc_smmu_clocked(const char **why)
{
	unsigned i;

	*why = NULL;
	for (i = 0; i < ad.dt.smmu_nclk && *why == NULL; i++)
		if (ad.dt.smmu_clk[i].prov != AD_GCC ||
		    cc_gcc_on(ad.dt.smmu_clk[i].id, NULL) != 1)
			*why = ad.dt.smmu_clk[i].name;
	if (*why == NULL && (ad.dt.smmu_pd < 0 ||
	    !gdsc_chain_on(ad.dt.smmu_pd)))
		*why = "its power domain";
	return *why == NULL;
}

/*===========================================================================*
 *    bringing things up                                                      *
 *===========================================================================*/
/* Poll (reg & mask) == want in the GPUCC, up to usec; the time taken, or -1. */
static int
gp_wait(uint32_t reg, uint32_t mask, uint32_t want, unsigned usec)
{
	unsigned t;

	for (t = 0; t <= usec; t++) {
		if ((ad_rd(&ad.gpucc, reg) & mask) == want)
			return (int)t;
		micro_delay(1);
	}
	return -1;
}

static void
gp_set(uint32_t reg, uint32_t bits)
{
	ad_wr(&ad.gpucc, reg, ad_rd(&ad.gpucc, reg) | bits);
}

static void
gp_clear(uint32_t reg, uint32_t bits)
{
	ad_wr(&ad.gpucc, reg, ad_rd(&ad.gpucc, reg) & ~bits);
}

/*
 * One domain up, its parent first.  Linux's gdsc_enable(): the block reset
 * pulsed (SW_RESET), the GMEM reset pulsed and the I/O clamp released
 * (CLAMP_IO, AON_RESET), the collapse released, PWR_ON waited for, the
 * core's memory kept on (cxcs; NO_RET_PERIPH: not the periphery's).
 */
static int
gdsc_up(int id, char *note, size_t size)
{
	char sn[24], part[48];
	uint32_t st;
	int i, t;

	if ((i = gp_gdsc(id)) < 0)
		return EINVAL;
	if (sdm660_gpucc_gdsc[i].parent >= 0 &&
	    gdsc_up(sdm660_gpucc_gdsc[i].parent, note, size) != OK)
		return EIO;
	short_name(sdm660_gpucc_gdsc[i].name, sn, sizeof(sn));
	if (gdsc_on((unsigned)i)) {
		snprintf(part, sizeof(part), " %s was on;", sn);
		strlcat(note, part, size);
		return OK;
	}

	if (sdm660_gpucc_gdsc[i].flags & SDM660_GDSC_SW_RESET) {
		gp_set(sdm660_gpucc_gdsc[i].bcr, BCR_ASSERT);
		(void)ad_rd(&ad.gpucc, sdm660_gpucc_gdsc[i].bcr);
		micro_delay(1);
		gp_clear(sdm660_gpucc_gdsc[i].bcr, BCR_ASSERT);
	}
	if (sdm660_gpucc_gdsc[i].flags & SDM660_GDSC_CLAMP_IO) {
		if (sdm660_gpucc_gdsc[i].flags & SDM660_GDSC_AON_RESET) {
			gp_set(sdm660_gpucc_gdsc[i].clamp, CLAMP_GMEM_RESET);
			micro_delay(1);
			gp_clear(sdm660_gpucc_gdsc[i].clamp, CLAMP_GMEM_RESET);
		}
		gp_clear(sdm660_gpucc_gdsc[i].clamp, CLAMP_IO);
	}
	ad_wr(&ad.gpucc, sdm660_gpucc_gdsc[i].gdscr,
	    (ad_rd(&ad.gpucc, sdm660_gpucc_gdsc[i].gdscr) & ~GDSC_INIT_MASK) |
	    GDSC_INIT_VAL);
	gp_clear(sdm660_gpucc_gdsc[i].gdscr, GDSC_SW_COLLAPSE);
	undo.gdsc[undo.ngdsc++] = id;	/* undone even if it never comes on */
	micro_delay(1);
	st = sdm660_gpucc_gdsc[i].status != 0 ? sdm660_gpucc_gdsc[i].status :
	    sdm660_gpucc_gdsc[i].gdscr;
	if ((t = gp_wait(st, GDSC_PWR_ON, GDSC_PWR_ON, 2000)) < 0) {
		snprintf(part, sizeof(part), " %s did not come on (%x);", sn,
		    ad_rd(&ad.gpucc, sdm660_gpucc_gdsc[i].gdscr));
		strlcat(note, part, size);
		return EIO;
	}
	if (sdm660_gpucc_gdsc[i].cxc != 0 && !(ad_rd(&ad.gpucc,
	    sdm660_gpucc_gdsc[i].cxc) & CBCR_FORCE_MEM_CORE_ON)) {
		gp_set(sdm660_gpucc_gdsc[i].cxc, CBCR_FORCE_MEM_CORE_ON);
		undo.cxc_set = 1;
		undo.cxc = sdm660_gpucc_gdsc[i].cxc;
	}
	micro_delay(1);
	snprintf(part, sizeof(part), " %s on in %d us;", sn, t + 1);
	strlcat(note, part, size);
	return OK;
}

/* A GCC branch on through its own enable bit (qsoc_clk_gate()). */
static int
gcc_up(const struct ad_clk *c, char *note, size_t size)
{
	char part[48];
	uint32_t en;
	unsigned bit, t;
	int nocheck;

	if (cc_gcc_on(c->id, NULL) == 1)
		return OK;
	if (qsoc_clk_gate(c->id, &en, &bit, &nocheck) != OK)
		return EINVAL;
	ad_wr(&ad.gcc, en, ad_rd(&ad.gcc, en) | (1u << bit));
	undo.gcc[undo.ngcc++] = c->id;
	for (t = 0; t <= 200; t++) {
		if (nocheck || cc_gcc_on(c->id, NULL) == 1) {
			snprintf(part, sizeof(part), " %s on;", c->name);
			strlcat(note, part, size);
			if (nocheck)
				micro_delay(10);
			return OK;
		}
		micro_delay(1);
	}
	snprintf(part, sizeof(part), " %s did not come on;", c->name);
	strlcat(note, part, size);
	return EIO;
}

/*
 * A GPUCC branch on at the crystal: its root moved to source "xo",
 * divider 1 (Linux's F(19200000, P_GPU_XO, 1, 0, 0): half-divider field 1),
 * the update waited for, then the branch's enable bit.
 */
static int
gpucc_up(const struct ad_clk *c, char *note, size_t size)
{
	char part[48];
	uint32_t cfg;
	int b, r, s, t;

	if ((b = gp_branch(c->id)) < 0)
		return EINVAL;
	if (cbcr_on(ad_rd(&ad.gpucc, sdm660_gpucc_branch[b].cbcr)))
		return OK;
	if (sdm660_gpucc_branch[b].rcg != 0) {
		if ((r = gp_rcg(sdm660_gpucc_branch[b].rcg)) < 0)
			return EINVAL;
		for (s = 0; s < 8; s++)
			if (sdm660_gpucc_rcg[r].src[s] != NULL &&
			    strcmp(sdm660_gpucc_rcg[r].src[s], "xo") == 0)
				break;
		if (s == 8)
			return EINVAL;
		cfg = ad_rd(&ad.gpucc, sdm660_gpucc_rcg[r].cmd + 4);
		undo.rcg[undo.nrcg].cmd = sdm660_gpucc_rcg[r].cmd;
		undo.rcg[undo.nrcg++].cfg = cfg;
		ad_wr(&ad.gpucc, sdm660_gpucc_rcg[r].cmd + 4,
		    (cfg & ~RCG_CFG_FIELDS) | ((uint32_t)s << 8) | 1u);
		gp_set(sdm660_gpucc_rcg[r].cmd, RCG_CMD_UPDATE);
		if (gp_wait(sdm660_gpucc_rcg[r].cmd, RCG_CMD_UPDATE, 0,
		    500) < 0) {
			snprintf(part, sizeof(part), " %s root stuck;",
			    c->name);
			strlcat(note, part, size);
			return EIO;
		}
	}
	gp_set(sdm660_gpucc_branch[b].cbcr, CBCR_ENABLE);
	undo.gpb[undo.ngpb++] = c->id;
	for (t = 0; t <= 200; t++) {
		if (cbcr_on(ad_rd(&ad.gpucc, sdm660_gpucc_branch[b].cbcr))) {
			snprintf(part, sizeof(part), " %s xo on;", c->name);
			strlcat(note, part, size);
			return OK;
		}
		micro_delay(1);
	}
	snprintf(part, sizeof(part), " %s did not come on (%x);", c->name,
	    ad_rd(&ad.gpucc, sdm660_gpucc_branch[b].cbcr));
	strlcat(note, part, size);
	return EIO;
}

int
cc_power_up(int stage)
{
	const struct ad_clk *gl[AD_MAXCLK * 3];
	char note[256];
	unsigned i, n;
	int r = OK, id;

	memset(&undo, 0, sizeof(undo));
	if (ad.dt.smmu_pd < 0) {
		log_warn(&ad_log, "Q4 the SMMU names no GPUCC domain; "
		    "refusing to power anything\n");
		return EINVAL;
	}

	/* Q4a: the domains - CX alone at stage cx, CX and GX after. */
	note[0] = '\0';
	id = ad.dt.smmu_pd;
	if (stage == AD_CX && gp_gdsc(id) >= 0)
		id = sdm660_gpucc_gdsc[gp_gdsc(id)].parent >= 0 ?
		    sdm660_gpucc_gdsc[gp_gdsc(id)].parent : id;
	r = gdsc_up(id, note, sizeof(note));
	log_info(&ad_log, "Q4 power:%s\n", note);
	if (r != OK || stage == AD_CX)
		return r;

	/*
	 * Q4b: the clocks - the GCC's the GPU and its SMMU name, then the
	 * GPUCC's at 19.2 MHz.  Not the GPUCC's own inputs (GPLL0's
	 * branches): the crystal needs neither.
	 */
	note[0] = '\0';
	n = 0;
	for (i = 0; i < ad.dt.nclk; i++)
		if (ad.dt.clk[i].prov == AD_GCC && n < NELEM(gl))
			gl[n++] = &ad.dt.clk[i];
	for (i = 0; i < ad.dt.smmu_nclk; i++)
		if (ad.dt.smmu_clk[i].prov == AD_GCC && n < NELEM(gl))
			gl[n++] = &ad.dt.smmu_clk[i];
	for (i = 0; i < n && r == OK; i++)
		r = gcc_up(gl[i], note, sizeof(note));	/* on already: no-op */
	for (i = 0; i < ad.dt.nclk && r == OK; i++)
		if (ad.dt.clk[i].prov == AD_GPUCC)
			r = gpucc_up(&ad.dt.clk[i], note, sizeof(note));
	log_info(&ad_log, "Q4 clocks:%s%s\n", note[0] != '\0' ? note :
	    " all were on", r == OK ? "" : " - stopping");
	return r;
}

void
cc_power_down(void)
{
	char line[256], part[64], sn[24];
	unsigned i, k, differs = 0;
	int b, g, r, on;
	uint32_t cfg;

	/* GPUCC branches, then their roots back where they were. */
	for (i = undo.ngpb; i-- > 0; )
		if ((b = gp_branch(undo.gpb[i])) >= 0)
			gp_clear(sdm660_gpucc_branch[b].cbcr, CBCR_ENABLE);
	for (i = undo.nrcg; i-- > 0; ) {
		cfg = ad_rd(&ad.gpucc, undo.rcg[i].cmd + 4);
		ad_wr(&ad.gpucc, undo.rcg[i].cmd + 4,
		    (cfg & ~RCG_CFG_FIELDS) | (undo.rcg[i].cfg & RCG_CFG_FIELDS));
		gp_set(undo.rcg[i].cmd, RCG_CMD_UPDATE);
		(void)gp_wait(undo.rcg[i].cmd, RCG_CMD_UPDATE, 0, 500);
	}
	/* GCC branches: their bit back; another master may keep one on. */
	for (i = undo.ngcc; i-- > 0; ) {
		uint32_t en;
		unsigned bit;
		int nocheck;

		if (qsoc_clk_gate(undo.gcc[i], &en, &bit, &nocheck) == OK)
			ad_wr(&ad.gcc, en, ad_rd(&ad.gcc, en) & ~(1u << bit));
	}
	micro_delay(10);
	/* Domains, last up first down: memory, collapse, clamp. */
	for (k = undo.ngdsc; k-- > 0; ) {
		if ((g = gp_gdsc(undo.gdsc[k])) < 0)
			continue;
		if (undo.cxc_set && undo.cxc == sdm660_gpucc_gdsc[g].cxc)
			gp_clear(undo.cxc, CBCR_FORCE_MEM_CORE_ON);
		gp_set(sdm660_gpucc_gdsc[g].gdscr, GDSC_SW_COLLAPSE);
		(void)gp_wait(sdm660_gpucc_gdsc[g].status != 0 ?
		    sdm660_gpucc_gdsc[g].status : sdm660_gpucc_gdsc[g].gdscr,
		    GDSC_PWR_ON, 0, 2000);
		if (sdm660_gpucc_gdsc[g].flags & SDM660_GDSC_CLAMP_IO)
			gp_set(sdm660_gpucc_gdsc[g].clamp, CLAMP_IO);
	}

	/* Q11: what can be seen, against what was found. */
	snprintf(line, sizeof(line), "Q11 undone:");
	for (i = 0; i < NELEM(sdm660_gpucc_gdsc); i++) {
		on = gdsc_on(i);
		snprintf(part, sizeof(part), " %s %s",
		    short_name(sdm660_gpucc_gdsc[i].name, sn, sizeof(sn)),
		    on ? "on" : "off");
		strlcat(line, part, sizeof(line));
		if (on != found.gdsc_on[i]) {
			strlcat(line, "(was other)", sizeof(line));
			differs++;
		}
	}
	for (i = 0; i < NELEM(sdm660_gpucc_branch); i++)
		if (cbcr_on(ad_rd(&ad.gpucc, sdm660_gpucc_branch[i].cbcr)) !=
		    found.gpb_on[i]) {
			snprintf(part, sizeof(part), " %s differs",
			    short_name(sdm660_gpucc_branch[i].name, sn,
			    sizeof(sn)));
			strlcat(line, part, sizeof(line));
			differs++;
		}
	for (i = 0; i < NELEM(sdm660_gpucc_rcg); i++)
		if ((ad_rd(&ad.gpucc, sdm660_gpucc_rcg[i].cmd + 4) &
		    RCG_CFG_FIELDS) != (found.rcg_cfg[i] & RCG_CFG_FIELDS)) {
			snprintf(part, sizeof(part), " %s root differs",
			    short_name(sdm660_gpucc_rcg[i].name, sn,
			    sizeof(sn)));
			strlcat(line, part, sizeof(line));
			differs++;
		}
	for (i = 0; i < found.ngcc; i++)
		if ((r = cc_gcc_on(found.gcc_id[i], NULL)) != found.gcc_on[i]) {
			snprintf(part, sizeof(part), " gcc %u %s (was %s)",
			    found.gcc_id[i], r == 1 ? "on" : "off",
			    found.gcc_on[i] == 1 ? "on" : "off");
			strlcat(line, part, sizeof(line));
			differs++;
		}
	snprintf(part, sizeof(part), "; %s", differs == 0 ? "all as found" :
	    "NOT all as found");
	strlcat(line, part, sizeof(line));
	if (differs == 0)
		log_info(&ad_log, "%s\n", line);
	else
		log_warn(&ad_log, "%s\n", line);
}
