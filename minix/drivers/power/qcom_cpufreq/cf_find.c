/*
 * The blocks, found in the device tree and mapped (port/clover/hw-cpu.md,
 * section 3).
 *
 *   meecho,sdm660-cpu-dvfs   the clusters' blocks, which the mainline tree
 *                            does not describe (sdm660-xiaomi-clover-cpu.dtsi
 *                            adds the node); of its regions only the four
 *                            this service writes are mapped: silver-glb,
 *                            silver-pll, gold-glb, gold-pll
 *   qcom,qfprom              the fuses: the page of the CPR rows (+0x4000)
 *   qcom,gcc-sdm660          the page of HMSS_GPLL0_CLK_SRC, the clusters'
 *                            "aux" source - read, never written
 *   qcom,spmi-pmic-arb       the arbiter: the rails' set points, through the
 *                            observer channels (cf_rail.c)
 *
 * Mapping a block touches none of it.  The reads below are ones the probe
 * (port/clover/cpuprobe) made on the tablet on 2026-10-18.
 */

#include "qcom_cpufreq.h"

#include <minix/fdt.h>
#include <minix/vm.h>

#include <sys/mman.h>

#define MAX_REGS		12

#define FUSE_ROWS_OFF		0x4000	/* qfprom + 0x4000 + 8 * row */
#define GCC_GPLL0_PAGE		0x48000
#define GCC_HMSS_GPLL0_CMD	0x05c	/* in that page: CMD_RCGR, CFG_RCGR */
#define GCC_HMSS_GPLL0_CFG	0x060
#define RCG_ROOT_OFF		(1u << 31)
#define GPLL0_KHZ		600000	/* gcc-sdm660.c: gpll0, 600 MHz */
#define GCC_SRC_GPLL0		1

#define PM660_SID_REGS		1	/* PM660's regulators (hw-rpm.md) */

struct look {
	const char *compat;
	int found;
	unsigned n;
	u64_t addr[MAX_REGS], size[MAX_REGS];
	char name[MAX_REGS][24];
	unsigned ee;
};

static int
look_cb(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct look *lk = cookie;
	const uint8_t *ee;
	const char *names, *p;
	unsigned len, i;

	if (!fdt_node_is_compatible(node, lk->compat))
		return 0;
	for (i = 0; i < MAX_REGS &&
	    fdt_node_reg(node, i, &lk->addr[i], &lk->size[i]) == 0; i++)
		lk->name[i][0] = '\0';
	lk->n = i;
	if ((names = fdt_getprop(node, "reg-names", &len)) != NULL)
		for (p = names, i = 0; p < names + len && i < lk->n;
		    p += strlen(p) + 1, i++)
			strlcpy(lk->name[i], p, sizeof(lk->name[i]));
	if ((ee = fdt_getprop(node, "qcom,ee", &len)) != NULL && len == 4)
		lk->ee = (unsigned)ee[3];
	lk->found = 1;
	return 1;
}

static int
look(const void *dtb, struct look *lk, const char *compat)
{
	memset(lk, 0, sizeof(*lk));
	lk->compat = compat;
	(void)fdt_walk(dtb, look_cb, lk);
	if (!lk->found || lk->n == 0) {
		log_warn(&cf_log, "no node \"%s\" in the tree\n", compat);
		return ENOENT;
	}
	return OK;
}

static int
named(const struct look *lk, const char *name)
{
	unsigned i;

	for (i = 0; i < lk->n; i++)
		if (strcmp(lk->name[i], name) == 0)
			return (int)i;
	log_warn(&cf_log, "%s has no region \"%s\"\n", lk->compat, name);
	return -1;
}

static int
map(struct cf_region *r, const char *name, u64_t pa, u64_t size)
{
	void *v;

	r->name = name;
	r->pa = pa;
	r->size = (size_t)size;
	v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)size);
	if (v == MAP_FAILED) {
		log_warn(&cf_log, "cannot map %s at 0x%llx+0x%llx: not "
		    "granted?\n", name, (unsigned long long)pa,
		    (unsigned long long)size);
		r->base = NULL;
		return EPERM;
	}
	r->base = v;
	return OK;
}

static int
map_named(struct cf_region *r, const struct look *lk, const char *name)
{
	int i;

	if ((i = named(lk, name)) < 0)
		return ENOENT;
	return map(r, name, lk->addr[i], lk->size[i]);
}

int
cf_find(const void *dtb, struct cf *cf)
{
	static const char *const want[4] = { "core", "chnls", "obsrvr",
	    "cnfg" };
	struct look lk;
	struct cf_cluster *cl;
	struct cf_region m[4];
	unsigned k;
	int i, r;

	memset(cf, 0, sizeof(*cf));
	cl = &cf->cl[CF_SILVER];
	cl->id = CF_SILVER;
	cl->name = "silver";
	cl->rail[0] = (struct cf_rail){ "S1", PM660_SID_REGS, 0x1400 };
	cl->nrail = 1;
	cl = &cf->cl[CF_GOLD];
	cl->id = CF_GOLD;
	cl->name = "gold";
	cl->rail[0] = (struct cf_rail){ "S2", PM660_SID_REGS, 0x1700 };
	cl->rail[1] = (struct cf_rail){ "S3", PM660_SID_REGS, 0x1a00 };
	cl->nrail = 2;

	if ((r = look(dtb, &lk, "meecho,sdm660-cpu-dvfs")) != OK ||
	    (r = map_named(&cf->cl[CF_SILVER].glb, &lk, "silver-glb")) != OK ||
	    (r = map_named(&cf->cl[CF_SILVER].pll, &lk, "silver-pll")) != OK ||
	    (r = map_named(&cf->cl[CF_GOLD].glb, &lk, "gold-glb")) != OK ||
	    (r = map_named(&cf->cl[CF_GOLD].pll, &lk, "gold-pll")) != OK)
		return r;

	if ((r = look(dtb, &lk, "qcom,qfprom")) != OK)
		return r;
	if (lk.size[0] < FUSE_ROWS_OFF + 8 * CF_FUSE_ROWS) {
		log_warn(&cf_log, "qfprom is 0x%llx bytes: the CPR rows are "
		    "not in it\n", (unsigned long long)lk.size[0]);
		return ENXIO;
	}
	if ((r = map(&cf->fuse, "qfprom", lk.addr[0] + FUSE_ROWS_OFF,
	    0x1000)) != OK)
		return r;

	if ((r = look(dtb, &lk, "qcom,gcc-sdm660")) != OK)
		return r;
	if (lk.size[0] < GCC_GPLL0_PAGE + 0x1000) {
		log_warn(&cf_log, "the GCC is 0x%llx bytes: no "
		    "HMSS_GPLL0_CLK_SRC in it\n", (unsigned long long)lk.size[0]);
		return ENXIO;
	}
	if ((r = map(&cf->gcc, "gcc", lk.addr[0] + GCC_GPLL0_PAGE,
	    0x1000)) != OK)
		return r;

	if ((r = look(dtb, &lk, "qcom,spmi-pmic-arb")) != OK)
		return r;
	for (k = 0; k < 4; k++)
		if ((i = named(&lk, want[k])) < 0 ||
		    (r = map(&m[k], want[k], lk.addr[i], lk.size[i])) != OK)
			return i < 0 ? ENOENT : r;
	cf->spmi.core = m[0].base;
	cf->spmi.core_size = m[0].size;
	cf->spmi.chnls = m[1].base;
	cf->spmi.chnls_size = m[1].size;
	cf->spmi.obsrvr = m[2].base;
	cf->spmi.obsrvr_size = m[2].size;
	cf->spmi.cnfg = m[3].base;
	cf->spmi.cnfg_size = m[3].size;
	cf->spmi.ee = lk.ee;
	cf->spmi.delay_us = cf_delay_us;
	if ((r = spmi_init(&cf->spmi)) != OK_SPMI) {
		log_warn(&cf_log, "SPMI arbiter 0x%08x: not one spmi.c knows "
		    "(%d)\n", cf->spmi.version, r);
		return ENXIO;
	}
	return OK;
}

/* The CPR fuse rows, 38 and 65..71; the others stay zero. */
int
cf_fuse_rows(struct cf *cf, uint64_t rows[CF_FUSE_ROWS])
{
	unsigned r;

	memset(rows, 0, CF_FUSE_ROWS * sizeof(rows[0]));
	for (r = 0; r < CF_FUSE_ROWS; r++) {
		if (r != 38 && r < 65)
			continue;
		rows[r] = (uint64_t)cf_rd(&cf->fuse, 8 * r + 4) << 32 |
		    cf_rd(&cf->fuse, 8 * r);
	}
	return OK;
}

/*
 * The clusters' aux source: HMSS_GPLL0_CLK_SRC, GPLL0 through an RCG
 * (gcc-sdm660.c).  0 when its root is off or it is not on GPLL0 - then no
 * cluster may be put on it.
 */
int
cf_aux(struct cf *cf)
{
	uint32_t cmd, cfg, d;

	cmd = cf_rd(&cf->gcc, GCC_HMSS_GPLL0_CMD);
	cfg = cf_rd(&cf->gcc, GCC_HMSS_GPLL0_CFG);
	cf->aux_khz = 0;
	if ((cmd & RCG_ROOT_OFF) || ((cfg >> 8) & 7) != GCC_SRC_GPLL0) {
		log_warn(&cf_log, "aux (HMSS_GPLL0_CLK_SRC): cmd 0x%x cfg 0x%x - "
		    "not running from GPLL0\n", cmd, cfg);
		return ENXIO;
	}
	d = cfg & 0x1f;
	cf->aux_khz = d <= 1 ? GPLL0_KHZ : GPLL0_KHZ * 2 / (d + 1);
	return OK;
}
