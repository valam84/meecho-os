/*
 * The GPU in the device tree (adreno.h).
 *
 * The binding this reads (sdm630.dtsi, sdm660.dtsi and clover's tree;
 * port/clover/hw-gpu.md, §1):
 *
 *   gpu@5000000 "qcom,adreno-512.0", "qcom,adreno"
 *       reg, interrupts; clocks/clock-names (GCC and GPUCC numbers);
 *       power-domains <&rpmpd VDDMX>; iommus <&kgsl_smmu SID>
 *     zap-shader             firmware-name, memory-region (a no-map range)
 *     opp-table / opp-*      opp-hz
 *   iommu@5040000 "qcom,adreno-smmu" (by phandle)
 *       reg, interrupts (#global-interrupts of them global), clocks,
 *       power-domains <&gpucc GPU_GX_GDSC>
 *   clock-controller@5065000 "qcom,gpucc-sdm660" (by phandle)
 *   clock-controller@100000 "qcom,gcc-sdm660" (by phandle; qcom_soc's)
 *
 * Three walks, because a phandle is followed by walking again: every clock
 * provider's #clock-cells, the GPU and its children, then the nodes it
 * points at.  Only an enabled GPU counts.
 * The chip id is the compatible's number, as Linux makes it
 * (adreno_device.c): "512.0" is core 5, major 1, minor 2, patch 0.
 */

#include "adreno.h"

#include <stdlib.h>
#include <string.h>

static int
enabled(const struct fdt_node *node)
{
	const char *s;
	unsigned len;

	if ((s = fdt_getprop(node, "status", &len)) == NULL)
		return 1;
	return strcmp(s, "okay") == 0 || strcmp(s, "ok") == 0;
}

static uint32_t
phandle_of(const struct fdt_node *node)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, "phandle", &len)) == NULL &&
	    (p = fdt_getprop(node, "linux,phandle", &len)) == NULL)
		return 0;
	return len == 4 ? (uint32_t)fdt_read_cells(p, 1) : 0;
}

static void
copy_str(char *dst, size_t size, const char *s, unsigned len)
{
	dst[0] = '\0';
	if (s != NULL && len > 0 && s[len - 1] == '\0')
		strlcpy(dst, s, size);
}

/* The n'th string of a string-list property, or NULL. */
static const char *
nth_string(const struct fdt_node *node, const char *prop, unsigned n)
{
	const char *s, *p;
	unsigned len;

	if ((s = fdt_getprop(node, prop, &len)) == NULL)
		return NULL;
	for (p = s; p < s + len; p += strlen(p) + 1)
		if (n-- == 0)
			return p;
	return NULL;
}

/*
 * Every clock provider's #clock-cells, by phandle: an entry of "clocks" is
 * the provider's phandle and that many cells - one for the GCC and the
 * GPUCC, none for the fixed crystal (xo_board) that heads the GPUCC's own
 * list.  Walk 0 fills it in.
 */
#define AD_MAXPROV	64
static struct {
	uint32_t phandle;
	unsigned cells;
} prov[AD_MAXPROV];
static unsigned nprov;

static int
walk_providers(void *UNUSED(cookie), int UNUSED(depth),
	const char *UNUSED(name), const struct fdt_node *node)
{
	const void *p;
	unsigned len;
	uint32_t ph;

	if ((p = fdt_getprop(node, "#clock-cells", &len)) == NULL || len != 4 ||
	    (ph = phandle_of(node)) == 0 || nprov == AD_MAXPROV)
		return 0;
	prov[nprov].phandle = ph;
	prov[nprov++].cells = (unsigned)fdt_read_cells(p, 1);
	return 0;
}

static int
clock_cells(uint32_t ph)
{
	unsigned i;

	for (i = 0; i < nprov; i++)
		if (prov[i].phandle == ph)
			return (int)prov[i].cells;
	return -1;
}

/*
 * The entries of "clocks" with their clock-names: the provider's phandle
 * and, for a provider of one cell, the clock's number.  A provider this
 * does not know, or one of more cells than one, is refused.
 */
static int
clock_list(const struct fdt_node *node, struct ad_clk *clk, unsigned *count)
{
	const uint8_t *p;
	const char *name;
	unsigned len, off, n;
	int cells;

	*count = 0;
	if ((p = fdt_getprop(node, "clocks", &len)) == NULL)
		return 0;
	for (off = 0, n = 0; off + 4 <= len; n++) {
		if (n == AD_MAXCLK)
			return -1;
		clk[n].phandle = (uint32_t)fdt_read_cells(p + off, 1);
		if ((cells = clock_cells(clk[n].phandle)) < 0 || cells > 1 ||
		    off + 4 + 4 * (unsigned)cells > len)
			return -1;
		clk[n].id = cells == 1 ?
		    (unsigned)fdt_read_cells(p + off + 4, 1) : 0;
		clk[n].prov = 0;
		name = nth_string(node, "clock-names", n);
		strlcpy(clk[n].name, name != NULL ? name : "?",
		    sizeof(clk[n].name));
		off += 4 + 4 * (unsigned)cells;
	}
	if (off != len)
		return -1;
	*count = n;
	return 0;
}

/* The first <phandle number> pair of a property, 0 if there is none. */
static int
pair(const struct fdt_node *node, const char *prop, uint32_t *ph,
	unsigned *num)
{
	const uint8_t *p;
	unsigned len;

	if ((p = fdt_getprop(node, prop, &len)) == NULL || len < 8)
		return 0;
	*ph = (uint32_t)fdt_read_cells(p, 1);
	*num = (unsigned)fdt_read_cells(p + 4, 1);
	return 1;
}

/* "qcom,adreno-512.0" -> 0x05010200; 0 when the string is not of that form. */
static uint32_t
chip_id(const char *compat)
{
	const char *p;
	char *end;
	unsigned long rev, patch = 0;

	if (strncmp(compat, "qcom,adreno-", 12) != 0)
		return 0;
	p = compat + 12;
	rev = strtoul(p, &end, 10);
	if (end == p || rev < 100 || rev > 999)
		return 0;
	if (*end == '.')
		patch = strtoul(end + 1, &end, 10);
	return (uint32_t)((rev / 100) << 24 | ((rev / 10) % 10) << 16 |
	    (rev % 10) << 8 | (patch & 0xff));
}

/*===========================================================================*
 *    walk 1: the GPU and its children                                        *
 *===========================================================================*/
struct w1 {
	struct adreno_dt *dt;
	int found, depth, in_opp, bad;
};

static int
walk_gpu(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct w1 *w = cookie;
	struct adreno_dt *dt = w->dt;
	const char *s;
	const uint8_t *p;
	unsigned len;
	u64_t hz;

	if (!w->found) {
		if (!fdt_node_is_compatible(node, "qcom,adreno") ||
		    !enabled(node))
			return 0;
		w->found = 1;
		w->depth = depth;
		strlcpy(dt->node, name, sizeof(dt->node));
		s = fdt_getprop(node, "compatible", &len);
		copy_str(dt->compat, sizeof(dt->compat), s, s != NULL ?
		    (unsigned)strnlen(s, len) + 1 : 0);
		dt->chip_id = chip_id(dt->compat);
		if (fdt_node_reg(node, 0, &dt->gpu_pa, &dt->gpu_sz) != 0)
			w->bad = 1;
		dt->irq = fdt_node_gic_irq(node, 0);
		if (clock_list(node, dt->clk, &dt->nclk) != 0)
			w->bad = 1;
		(void)pair(node, "power-domains", &dt->pd_phandle, &dt->pd_id);
		if (!pair(node, "iommus", &dt->smmu_ph, &dt->sid))
			dt->smmu_ph = 0;
		return 0;
	}

	if (depth <= w->depth)
		return 1;		/* past the GPU's children: done */

	if (depth == w->depth + 1) {
		w->in_opp = strncmp(name, "opp-table", 9) == 0;
		if (strncmp(name, "zap-shader", 10) == 0) {
			s = fdt_getprop(node, "firmware-name", &len);
			copy_str(dt->zap_fw, sizeof(dt->zap_fw), s, len);
			if ((p = fdt_getprop(node, "memory-region", &len)) !=
			    NULL && len >= 4)
				dt->zap_ph = (uint32_t)fdt_read_cells(p, 1);
		}
	} else if (depth == w->depth + 2 && w->in_opp) {
		if ((p = fdt_getprop(node, "opp-hz", &len)) != NULL &&
		    len == 8) {
			hz = fdt_read_cells(p, 2);
			dt->nopp++;
			if (hz / 1000000 > dt->opp_max_mhz)
				dt->opp_max_mhz = (unsigned)(hz / 1000000);
		}
	}
	return 0;
}

/*===========================================================================*
 *    walk 2: what the GPU points at                                          *
 *===========================================================================*/
struct w2 {
	struct adreno_dt *dt;
	int gpucc, smmu, zap, bad;
	uint32_t smmu_pd_ph;		/* the SMMU's power-domains pair */
	unsigned smmu_pd_id;
};

static int
is_clock_provider(const struct adreno_dt *dt, uint32_t ph)
{
	unsigned i;

	for (i = 0; i < dt->nclk; i++)
		if (dt->clk[i].phandle == ph)
			return 1;
	return 0;
}

static int
walk_targets(void *cookie, int UNUSED(depth), const char *name,
	const struct fdt_node *node)
{
	struct w2 *w = cookie;
	struct adreno_dt *dt = w->dt;
	const uint8_t *p;
	const char *s;
	uint32_t ph = phandle_of(node);
	unsigned len, i;
	int irq;

	if (ph == 0)
		return 0;

	if (is_clock_provider(dt, ph)) {
		if (fdt_node_is_compatible(node, "qcom,gcc-sdm660") ||
		    fdt_node_is_compatible(node, "qcom,gcc-sdm630")) {
			dt->gcc_ph = ph;
			if (fdt_node_reg(node, 0, &dt->gcc_pa, &dt->gcc_sz) != 0)
				w->bad = 1;
		} else if (fdt_node_is_compatible(node, "qcom,gpucc-sdm660") ||
		    fdt_node_is_compatible(node, "qcom,gpucc-sdm630")) {
			dt->gpucc_ph = ph;
			s = fdt_getprop(node, "compatible", &len);
			copy_str(dt->gpucc_compat, sizeof(dt->gpucc_compat), s,
			    s != NULL ? (unsigned)strnlen(s, len) + 1 : 0);
			if (fdt_node_reg(node, 0, &dt->gpucc_pa,
			    &dt->gpucc_sz) != 0 ||
			    clock_list(node, dt->gpucc_in, &dt->gpucc_nin) != 0)
				w->bad = 1;
			w->gpucc = 1;
		}
	}

	if (ph == dt->smmu_ph &&
	    fdt_node_is_compatible(node, "qcom,adreno-smmu")) {
		strlcpy(dt->smmu_node, name, sizeof(dt->smmu_node));
		if (fdt_node_reg(node, 0, &dt->smmu_pa, &dt->smmu_sz) != 0)
			w->bad = 1;
		for (i = 0; i < AD_MAXIRQ &&
		    (irq = fdt_node_gic_irq(node, i)) >= 0; i++)
			dt->smmu_irq[i] = irq;
		dt->smmu_nirq = i;
		if ((p = fdt_getprop(node, "#global-interrupts", &len)) !=
		    NULL && len == 4)
			dt->smmu_nglobal = (unsigned)fdt_read_cells(p, 1);
		if (clock_list(node, dt->smmu_clk, &dt->smmu_nclk) != 0)
			w->bad = 1;
		(void)pair(node, "power-domains", &w->smmu_pd_ph,
		    &w->smmu_pd_id);
		w->smmu = 1;
	}

	if (dt->zap_ph != 0 && ph == dt->zap_ph) {
		if (fdt_node_reg(node, 0, &dt->zap_pa, &dt->zap_sz) == 0)
			w->zap = 1;
	}
	return 0;
}

static int
classify(const struct adreno_dt *dt, uint32_t ph)
{
	if (ph != 0 && ph == dt->gcc_ph)
		return AD_GCC;
	if (ph != 0 && ph == dt->gpucc_ph)
		return AD_GPUCC;
	return AD_OTHER;
}

int
adreno_find(const void *dtb, struct adreno_dt *dt)
{
	struct w1 w1;
	struct w2 w2;
	unsigned i;

	memset(dt, 0, sizeof(*dt));
	dt->irq = -1;
	dt->smmu_pd = -1;

	nprov = 0;
	fdt_walk(dtb, walk_providers, NULL);

	memset(&w1, 0, sizeof(w1));
	w1.dt = dt;
	fdt_walk(dtb, walk_gpu, &w1);
	if (!w1.found) {
		log_info(&ad_log, "no qcom,adreno node in the device tree; "
		    "not starting\n");
		return ENXIO;
	}
	if (w1.bad || dt->gpu_sz == 0 || dt->chip_id == 0) {
		log_warn(&ad_log, "%s: reg, clocks or compatible (%s) not "
		    "understood; refusing\n", dt->node, dt->compat);
		return EINVAL;
	}

	memset(&w2, 0, sizeof(w2));
	w2.dt = dt;
	fdt_walk(dtb, walk_targets, &w2);

	for (i = 0; i < dt->nclk; i++)
		dt->clk[i].prov = classify(dt, dt->clk[i].phandle);
	for (i = 0; i < dt->smmu_nclk; i++)
		dt->smmu_clk[i].prov = classify(dt, dt->smmu_clk[i].phandle);
	for (i = 0; i < dt->gpucc_nin; i++)
		dt->gpucc_in[i].prov = classify(dt, dt->gpucc_in[i].phandle);
	/* The SMMU's domain is the GPUCC's GX in this binding; another
	 * provider's is not one this driver can switch (-1). */
	if (w2.smmu_pd_ph != 0 && w2.smmu_pd_ph == dt->gpucc_ph)
		dt->smmu_pd = (int)w2.smmu_pd_id;

	if (w2.bad || dt->gcc_ph == 0 || !w2.gpucc || !w2.smmu) {
		log_warn(&ad_log, "%s: %s%s%s%s; refusing\n", dt->node,
		    dt->gcc_ph == 0 ? "no GCC among its clocks " : "",
		    !w2.gpucc ? "no qcom,gpucc-sdm660 among its clocks " : "",
		    !w2.smmu ? "no qcom,adreno-smmu behind iommus " : "",
		    w2.bad ? "a reg not understood" : "");
		return EINVAL;
	}
	for (i = 0; i < dt->nclk; i++)
		if (dt->clk[i].prov == AD_OTHER) {
			log_warn(&ad_log, "%s: clock %s is neither the GCC's "
			    "nor the GPUCC's; refusing\n", dt->node,
			    dt->clk[i].name);
			return EINVAL;
		}
	return OK;
}

void
adreno_find_report(const struct adreno_dt *dt)
{
	log_info(&ad_log, "node %s %s chip 0x%08x at 0x%llx+0x%llx "
	    "irq %d\n", dt->node, dt->compat, dt->chip_id,
	    (unsigned long long)dt->gpu_pa, (unsigned long long)dt->gpu_sz,
	    dt->irq);
	log_info(&ad_log, "smmu %s sid %u at 0x%llx+0x%llx, %u lines (%u "
	    "global), domain %d; gpucc 0x%llx+0x%llx\n", dt->smmu_node,
	    dt->sid, (unsigned long long)dt->smmu_pa,
	    (unsigned long long)dt->smmu_sz, dt->smmu_nirq, dt->smmu_nglobal,
	    dt->smmu_pd, (unsigned long long)dt->gpucc_pa,
	    (unsigned long long)dt->gpucc_sz);
	log_info(&ad_log, "zap %s at 0x%llx+0x%llx; %u operating points, up "
	    "to %u MHz\n", dt->zap_fw[0] != '\0' ? dt->zap_fw : "(none)",
	    (unsigned long long)dt->zap_pa, (unsigned long long)dt->zap_sz,
	    dt->nopp, dt->opp_max_mhz);
}
