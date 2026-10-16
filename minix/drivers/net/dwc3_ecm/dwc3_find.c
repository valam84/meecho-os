/*
 * The controller in the device tree (dwc3_ecm.h).
 *
 * The Qualcomm binding this reads (sdm630.dtsi, clover's tree):
 *
 *   usb@a8f8800 "qcom,dwc3"        the wrapper: reg is its QSCRATCH block;
 *                                  clocks/clock-names, power-domains,
 *                                  resets, qcom,select-utmi-as-pipe-clk
 *     usb@a800000 "snps,dwc3"      the core: reg, interrupts, dr_mode,
 *                                  maximum-speed, phys/phy-names
 *   phy@c012000 (by phandle)       the PHY: reg, clocks, resets, the three
 *                                  supplies, nvmem-cells
 *   hstx-trim@240 (by phandle)     a cell of the QFPROM: reg is the byte's
 *                                  offset in its parent, bits <first count>
 *
 * Three walks, because a phandle can only be followed by walking again:
 * the wrapper and the core, then the nodes they point at, then the parents
 * of the fuse cell and of the supplies.  Only enabled nodes count ("status"
 * absent, "okay" or "ok"): the SoC has a second wrapper, disabled on this
 * tablet.
 */

#include "dwc3_ecm.h"

#include <string.h>

#define MAXDEPTH	16

static const struct dwc3_glue_ops *const glues[] = { &dwc3_qcom_glue };
static const struct dwc3_phy_ops *const phys[] = { &qusb2_phy };

static const char *const supply_prop[3] = {
	"vdd-supply", "vdda-pll-supply", "vdda-phy-dpdm-supply"
};

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
copy_str(char *dst, size_t size, const struct fdt_node *node, const char *prop)
{
	const char *s;
	unsigned len;

	dst[0] = '\0';
	if ((s = fdt_getprop(node, prop, &len)) != NULL && len > 0 &&
	    s[len - 1] == '\0')
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
 * <provider id> pairs of "clocks", "resets" or "power-domains", all from
 * one provider: dt->gcc_ref (set by the first).  Returns the number of
 * pairs, or -1 when a pair names another provider.
 */
static int
pairs(struct dwc3_dt *dt, const struct fdt_node *node, const char *prop,
	uint32_t *ids, unsigned max)
{
	const uint8_t *p;
	unsigned len, n, i;
	uint32_t prov;

	if ((p = fdt_getprop(node, prop, &len)) == NULL)
		return 0;
	n = len / 8;
	if (len % 8 != 0 || n > max)
		return -1;
	for (i = 0; i < n; i++) {
		prov = (uint32_t)fdt_read_cells(p + 8 * i, 1);
		if (dt->gcc_ref == 0)
			dt->gcc_ref = prov;
		if (prov != dt->gcc_ref)
			return -1;
		ids[i] = (uint32_t)fdt_read_cells(p + 8 * i + 4, 1);
	}
	return (int)n;
}

static int
clocks(struct dwc3_dt *dt, const struct fdt_node *node, struct dwc3_clk *clk,
	unsigned max, unsigned *count)
{
	uint32_t ids[DWC3_MAXCLK];
	const char *name;
	int n, i;

	if ((n = pairs(dt, node, "clocks", ids, max)) < 0)
		return -1;
	for (i = 0; i < n; i++) {
		clk[i].id = ids[i];
		name = nth_string(node, "clock-names", (unsigned)i);
		strlcpy(clk[i].name, name != NULL ? name : "?",
		    sizeof(clk[i].name));
	}
	*count = (unsigned)n;
	return 0;
}

/*===========================================================================*
 *    walk 1: the wrapper and the core                                        *
 *===========================================================================*/
struct w1 {
	struct dwc3_dt *dt;
	int found, glue_depth, core, bad;
};

static int
walk1(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct w1 *w = cookie;
	struct dwc3_dt *dt = w->dt;
	uint32_t ids[2];
	const uint8_t *p;
	const char *s;
	unsigned len, i, n;
	int r;

	if (w->found) {
		if (depth <= w->glue_depth)
			return 1;		/* left the wrapper */
		if (depth != w->glue_depth + 1 || w->core ||
		    !fdt_node_is_compatible(node, "snps,dwc3") || !enabled(node))
			return 0;
		w->core = 1;
		if (fdt_node_reg(node, 0, &dt->core_pa, &dt->core_sz) != 0)
			w->bad = 1;
		dt->irq = fdt_node_gic_irq(node, 0);
		copy_str(dt->dr_mode, sizeof(dt->dr_mode), node, "dr_mode");
		copy_str(dt->max_speed, sizeof(dt->max_speed), node,
		    "maximum-speed");
		/* phys: one cell each (#phy-cells = 0), by phy-names. */
		if ((p = fdt_getprop(node, "phys", &len)) != NULL) {
			for (i = 0; i < len / 4; i++)
				if ((s = nth_string(node, "phy-names", i)) !=
				    NULL && strcmp(s, "usb2-phy") == 0)
					dt->phy_phandle =
					    (uint32_t)fdt_read_cells(p + 4 * i, 1);
		}
		return 0;
	}

	for (i = 0; i < sizeof(glues) / sizeof(glues[0]); i++)
		if (fdt_node_is_compatible(node, glues[i]->compatible))
			break;
	if (i == sizeof(glues) / sizeof(glues[0]) || !enabled(node))
		return 0;
	w->found = 1;
	w->glue_depth = depth;
	strlcpy(dt->glue_node, name, sizeof(dt->glue_node));
	dt->glue = glues[i];
	strlcpy(dt->glue_compat, glues[i]->compatible, sizeof(dt->glue_compat));
	if (fdt_node_reg(node, 0, &dt->qscratch_pa, &dt->qscratch_sz) != 0 ||
	    clocks(dt, node, dt->clk, DWC3_MAXCLK, &dt->nclk) != 0)
		w->bad = 1;
	if ((r = pairs(dt, node, "power-domains", ids, 1)) < 0)
		w->bad = 1;
	else if (r == 1) {
		dt->have_pd = 1;
		dt->pd = ids[0];
	}
	if ((r = pairs(dt, node, "resets", dt->rst, 2)) < 0)
		w->bad = 1;
	else
		dt->nrst = (unsigned)r;
	dt->utmi_as_pipe = fdt_getprop(node, "qcom,select-utmi-as-pipe-clk",
	    &n) != NULL;
	return 0;
}

/*===========================================================================*
 *    walk 2: what they point at                                              *
 *===========================================================================*/
struct w2 {
	struct dwc3_dt *dt;
	int phy, gcc, bad;
};

static int
cells_one(const struct fdt_node *node, const char *prop)
{
	const void *p;
	unsigned len;

	return (p = fdt_getprop(node, prop, &len)) != NULL && len == 4 &&
	    fdt_read_cells(p, 1) == 1;
}

static int
walk2(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct w2 *w = cookie;
	struct dwc3_dt *dt = w->dt;
	uint32_t ph = phandle_of(node), id[1];
	const uint8_t *p;
	unsigned i, len;
	int r;

	if (ph == 0)
		return 0;
	if (ph == dt->gcc_ref) {
		/* One number each, or the pairs were read wrongly. */
		w->gcc = cells_one(node, "#clock-cells") &&
		    cells_one(node, "#reset-cells") &&
		    (!dt->have_pd || cells_one(node, "#power-domain-cells"));
	}
	if (ph != dt->phy_phandle)
		return 0;
	for (i = 0; i < sizeof(phys) / sizeof(phys[0]); i++)
		if (fdt_node_is_compatible(node, phys[i]->compatible))
			break;
	if (i == sizeof(phys) / sizeof(phys[0]) || !enabled(node)) {
		w->bad = 1;
		return 0;
	}
	w->phy = 1;
	dt->phy = phys[i];
	strlcpy(dt->phy_compat, phys[i]->compatible, sizeof(dt->phy_compat));
	if (fdt_node_reg(node, 0, &dt->phy_pa, &dt->phy_sz) != 0 ||
	    clocks(dt, node, dt->phy_clk, 4, &dt->nphyclk) != 0)
		w->bad = 1;
	if ((r = pairs(dt, node, "resets", id, 1)) < 0)
		w->bad = 1;
	else if (r == 1) {
		dt->have_phy_rst = 1;
		dt->phy_rst = id[0];
	}
	for (i = 0; i < 3; i++)
		if ((p = fdt_getprop(node, supply_prop[i], &len)) != NULL &&
		    len == 4)
			dt->supply_phandle[i] = (uint32_t)fdt_read_cells(p, 1);
	if ((p = fdt_getprop(node, "nvmem-cells", &len)) != NULL && len == 4)
		dt->trim_phandle = (uint32_t)fdt_read_cells(p, 1);
	return 0;
}

/*===========================================================================*
 *    walk 3: the fuse cell and the supplies, with their parents              *
 *===========================================================================*/
struct w3 {
	struct dwc3_dt *dt;
	struct fdt_node stack[MAXDEPTH];
	int trim, bad;
};

/*
 * "l1" below "qcom,rpm-pm660l-regulators" is "l1 (pm660l)".  (Services
 * link against libminc, which has no strstr().)
 */
static void
supply_name(char *buf, size_t size, const char *name,
	const struct fdt_node *parent)
{
	static const char pre[] = "qcom,rpm-", suf[] = "-regulators";
	const char *c = nth_string(parent, "compatible", 0);
	size_t len, mid;

	if (c != NULL && (len = strlen(c)) > sizeof(pre) - 1 + sizeof(suf) - 1 &&
	    strncmp(c, pre, sizeof(pre) - 1) == 0 &&
	    strcmp(c + len - (sizeof(suf) - 1), suf) == 0) {
		mid = len - (sizeof(pre) - 1) - (sizeof(suf) - 1);
		snprintf(buf, size, "%s (%.*s)", name, (int)mid,
		    c + sizeof(pre) - 1);
	} else
		strlcpy(buf, name, size);
}

static int
walk3(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct w3 *w = cookie;
	struct dwc3_dt *dt = w->dt;
	const struct fdt_node *parent;
	uint32_t ph;
	const void *b;
	unsigned len, i;
	u64_t sz;

	if (depth >= MAXDEPTH)
		return 0;
	w->stack[depth] = *node;
	if (depth == 0 || (ph = phandle_of(node)) == 0)
		return 0;
	parent = &w->stack[depth - 1];
	for (i = 0; i < 3; i++)
		if (ph == dt->supply_phandle[i])
			supply_name(dt->supply[i], sizeof(dt->supply[i]), name,
			    parent);
	if (ph != dt->trim_phandle)
		return 0;
	/* The cell's reg is an offset into its parent, the QFPROM. */
	if (fdt_node_reg(node, 0, &dt->trim_off, &sz) != 0 || sz < 1 ||
	    fdt_node_reg(parent, 0, &dt->fuse_base, &dt->fuse_size) != 0 ||
	    dt->trim_off >= dt->fuse_size) {
		w->bad = 1;
		return 0;
	}
	dt->trim_bit = 0;
	dt->trim_nbits = 8;
	if ((b = fdt_getprop(node, "bits", &len)) != NULL && len == 8) {
		dt->trim_bit = (unsigned)fdt_read_cells(b, 1);
		dt->trim_nbits = (unsigned)fdt_read_cells((const uint8_t *)b +
		    4, 1);
	}
	if (dt->trim_bit + dt->trim_nbits > 8)
		w->bad = 1;
	else
		w->trim = 1;
	return 0;
}

int
dwc3_find(const void *dtb, struct dwc3_dt *dt)
{
	struct w1 w1;
	struct w2 w2;
	static struct w3 w3;
	unsigned i;

	memset(dt, 0, sizeof(*dt));
	dt->irq = -1;

	memset(&w1, 0, sizeof(w1));
	w1.dt = dt;
	fdt_walk(dtb, walk1, &w1);
	if (!w1.found)
		return ENXIO;
	if (w1.bad || !w1.core || dt->phy_phandle == 0 || dt->gcc_ref == 0) {
		log_warn(&dwc3_log, "%s (%s): %s\n", dt->glue_node,
		    dt->glue_compat, !w1.core ? "no enabled snps,dwc3 below it" :
		    dt->phy_phandle == 0 ? "the core names no usb2-phy" :
		    "its reg, clocks, resets or power domain do not read");
		return EINVAL;
	}

	memset(&w2, 0, sizeof(w2));
	w2.dt = dt;
	fdt_walk(dtb, walk2, &w2);
	if (!w2.phy || w2.bad) {
		log_warn(&dwc3_log, "the usb2-phy (phandle %u) is not an enabled "
		    "PHY this driver knows, or does not read\n",
		    dt->phy_phandle);
		return EINVAL;
	}
	if (!w2.gcc) {
		log_warn(&dwc3_log, "clocks, resets and power domain name "
		    "phandle %u, which is not a provider of one number each\n",
		    dt->gcc_ref);
		return EINVAL;
	}

	memset(&w3, 0, sizeof(w3));
	w3.dt = dt;
	fdt_walk(dtb, walk3, &w3);
	if (dt->trim_phandle != 0 && (!w3.trim || w3.bad)) {
		log_warn(&dwc3_log, "the PHY's fuse cell (phandle %u) does not "
		    "read; no trim\n", dt->trim_phandle);
		dt->trim_phandle = 0;
	}
	for (i = 0; i < 3; i++)
		if (dt->supply_phandle[i] != 0 && dt->supply[i][0] == '\0')
			snprintf(dt->supply[i], sizeof(dt->supply[i]),
			    "phandle %u", dt->supply_phandle[i]);
	return OK;
}
