/*
 * What this machine's GPU is and where, asked of the device tree.
 *
 * The node is found by the generic string "arm,mali-bifrost", which both
 * trees this board has met carry: the vendor's says only that, mainline
 * puts "rockchip,rk3568-mali" in front of it.  The SoC-specific part of
 * the driver is mali_rk.c, and it checks that the blocks it is about to
 * write are the ones the node points at.
 *
 * The node on the board (port/cb2-display/live.dtb, vendor tree with the
 * hdmi overlay, which also enables the GPU):
 *
 *   reg              0xfde60000, 16 KiB
 *   interrupts       SPI 39, 41, 40 named "gpu", "mmu", "job" - and the
 *                    same three again as "GPU", "MMU", "JOB", for kbase
 *   clocks           <scmi_clk 1> "gpu", <cru CLK_GPU (27)> "bus"
 *   power-domains    <power RK3568_PD_GPU (7)>
 *   mali-supply      vdd_gpu, DCDC_REG2 of the PMIC, always-on, boot-on,
 *                    0.9 V at boot
 *
 * So the names are matched without regard to case and the first of each
 * is taken.  The supply is followed to its node and only reported: this
 * driver has no I2C and switches no regulator (stage 11.8), and a supply
 * the tree does not keep on is a GPU that may not answer, which is worth
 * saying before the first register is read rather than after.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "mali.h"

static const char *const gpu_compatibles[] = {
	"arm,mali-bifrost",
};

static const char *const cru_compatibles[] = {
	"rockchip,rk3568-cru",
	"rockchip,rk3566-cru",
};

static const char *const pmu_compatibles[] = {
	"rockchip,rk3568-pmu",
	"rockchip,rk3566-pmu",
};

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

struct search {
	struct mali_info *info;
	int found;
};

static int
node_enabled(const struct fdt_node *node)
{
	const char *status;
	unsigned len;

	status = fdt_getprop(node, "status", &len);
	if (status == NULL || len == 0)
		return 1;
	return strcmp(status, "okay") == 0 || strcmp(status, "ok") == 0;
}

static int
compatible_with(const struct fdt_node *node, const char *const *list,
	unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (fdt_node_is_compatible(node, list[i]))
			return 1;
	return 0;
}

static void
take_reg(const struct fdt_node *node, struct mali_regs *r)
{
	u64_t base, size;

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		r->base = (phys_bytes)base;
		r->size = (size_t)size;
	}
}

/*
 * The position of a name in a string-list property ("interrupt-names",
 * "clock-names"), compared without regard to case, or -1.
 */
static int
name_index(const struct fdt_node *node, const char *prop, const char *want)
{
	const char *names;
	unsigned len, off;
	int idx;

	if ((names = fdt_getprop(node, prop, &len)) == NULL)
		return -1;
	for (off = 0, idx = 0; off < len; idx++) {
		if (strcasecmp(names + off, want) == 0)
			return idx;
		off += strlen(names + off) + 1;
	}
	return -1;
}

/*
 * One <provider, id> pair of "clocks".  Both providers on this SoC (the CRU
 * and the firmware's SCMI clocks) have #clock-cells = 1, so a specifier is
 * two cells; a provider with wider specifiers would shift every pair after
 * it, and the driver would then point at the wrong gate - so the "bus"
 * provider's address is checked against the CRU later, in mali_rk.c.
 */
static void
clock_pair(const struct fdt_node *node, int idx, uint32_t *phandle,
	uint32_t *id)
{
	const char *p;
	unsigned len;

	*phandle = *id = 0;
	if (idx < 0 || (p = fdt_getprop(node, "clocks", &len)) == NULL)
		return;
	if ((unsigned)(idx + 1) * 8 > len)
		return;
	*phandle = (uint32_t)fdt_read_cells(p + idx * 8, 1);
	*id = (uint32_t)fdt_read_cells(p + idx * 8 + 4, 1);
}

static void
read_gpu(const struct fdt_node *node, struct mali_info *info)
{
	static const char *const irq_names[MALI_NR_IRQS] = {
		"gpu", "job", "mmu"
	};
	const void *p;
	unsigned len, i;
	int idx;

	take_reg(node, &info->gpu);

	for (i = 0; i < MALI_NR_IRQS; i++) {
		idx = name_index(node, "interrupt-names", irq_names[i]);
		info->irq[i] = idx < 0 ? -1 : fdt_node_gic_irq(node,
		    (unsigned)idx);
	}

	clock_pair(node, name_index(node, "clock-names", "bus"),
	    &info->bus_clk_phandle, &info->bus_clk_id);
	clock_pair(node, name_index(node, "clock-names", "gpu"),
	    &info->gpu_clk_phandle, &info->gpu_clk_id);

	/* <controller, domain>: the controller has no reg of its own. */
	if ((p = fdt_getprop(node, "power-domains", &len)) != NULL &&
	    len >= 8)
		info->power_domain = (int)fdt_read_cells((const char *)p + 4,
		    1);

	if ((p = fdt_getprop(node, "mali-supply", &len)) != NULL && len >= 4)
		info->supply_phandle = (uint32_t)fdt_read_cells(p, 1);
}

static int
collect(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct search *s = cookie;
	struct mali_info *info = s->info;

	if (depth == 0)
		return 0;

	if (!s->found && compatible_with(node, gpu_compatibles,
	    NELEM(gpu_compatibles))) {
		if (!node_enabled(node)) {
			log_info(&mali_log, "%s: disabled in the tree\n",
			    name);
			return 0;
		}
		read_gpu(node, info);
		if (info->gpu.base == 0 || info->gpu.size == 0) {
			log_warn(&mali_log, "%s: no usable reg\n", name);
			memset(&info->gpu, 0, sizeof(info->gpu));
			return 0;
		}
		s->found = 1;
	} else if (info->cru.base == 0 && compatible_with(node,
	    cru_compatibles, NELEM(cru_compatibles)))
		take_reg(node, &info->cru);
	else if (info->pmu.base == 0 && compatible_with(node,
	    pmu_compatibles, NELEM(pmu_compatibles)))
		take_reg(node, &info->pmu);

	return 0;
}

/* The second walk: the regulator node the supply points at. */
static int
find_supply(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct mali_info *info = cookie;
	const void *p;
	const char *s;
	unsigned len;

	if ((p = fdt_getprop(node, "phandle", &len)) == NULL || len < 4 ||
	    (uint32_t)fdt_read_cells(p, 1) != info->supply_phandle)
		return 0;

	if ((s = fdt_getprop(node, "regulator-name", &len)) != NULL &&
	    len > 0)
		strlcpy(info->supply_name, s, sizeof(info->supply_name));
	info->supply_always_on =
	    fdt_getprop(node, "regulator-always-on", NULL) != NULL;
	info->supply_boot_on =
	    fdt_getprop(node, "regulator-boot-on", NULL) != NULL;
	if ((p = fdt_getprop(node, "regulator-init-microvolt", &len)) !=
	    NULL && len >= 4)
		info->supply_init_uv = (unsigned)fdt_read_cells(p, 1);
	return 1;
}

int
mali_find(struct mali_info *info)
{
	struct search s;
	u64_t base, size;
	void *dtb;

	memset(info, 0, sizeof(*info));
	info->irq[MALI_IRQ_GPU] = info->irq[MALI_IRQ_JOB] =
	    info->irq[MALI_IRQ_MMU] = -1;
	info->power_domain = -1;
	memset(&s, 0, sizeof(s));
	s.info = info;

	if ((dtb = fdt_fetch()) == NULL) {
		log_info(&mali_log, "this machine has no device tree\n");
		return ENXIO;
	}

	(void)fdt_walk(dtb, collect, &s);

	if (!s.found) {
		log_info(&mali_log, "no Mali GPU in this machine's device "
		    "tree; nothing to do\n");
		free(dtb);
		return ENXIO;
	}

	if (info->bus_clk_phandle != 0 && fdt_phandle_reg(dtb,
	    info->bus_clk_phandle, 0, &base, &size) == 0)
		info->bus_clk_provider = (phys_bytes)base;

	if (info->supply_phandle != 0)
		(void)fdt_walk(dtb, find_supply, info);

	free(dtb);

	log_info(&mali_log, "gpu at 0x%lx (%u bytes), lines gpu %d job %d "
	    "mmu %d\n", (unsigned long)info->gpu.base,
	    (unsigned)info->gpu.size, info->irq[MALI_IRQ_GPU],
	    info->irq[MALI_IRQ_JOB], info->irq[MALI_IRQ_MMU]);
	log_info(&mali_log, "clock \"bus\" %u of 0x%lx, clock \"gpu\" %u "
	    "(firmware's), power domain %d, cru 0x%lx, pmu 0x%lx\n",
	    info->bus_clk_id, (unsigned long)info->bus_clk_provider,
	    info->gpu_clk_id, info->power_domain,
	    (unsigned long)info->cru.base, (unsigned long)info->pmu.base);

	if (info->supply_phandle == 0)
		log_info(&mali_log, "the tree names no supply\n");
	else if (info->supply_name[0] == '\0')
		log_warn(&mali_log, "the supply's node (phandle %u) was not "
		    "found\n", info->supply_phandle);
	else {
		log_info(&mali_log, "supply %s: %s%s, %u uV at boot\n",
		    info->supply_name,
		    info->supply_always_on ? "always on" : "switchable",
		    info->supply_boot_on ? ", on at boot" : "",
		    info->supply_init_uv);
		if (!info->supply_always_on && !info->supply_boot_on)
			log_warn(&mali_log, "the tree does not keep %s on, "
			    "and this driver cannot switch it; the GPU may "
			    "not answer\n", info->supply_name);
	}

	return OK;
}
