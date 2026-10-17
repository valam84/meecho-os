/*
 * What this machine has, asked of the device tree.
 *
 * The same shape as dwmac_find.c and sdmmc_host.c, and for the same
 * reason: this port has no board identifiers, it has the tree the loader
 * passed, and RS grants the registers and the interrupt out of that same
 * tree by way of the devicetree "..." lines in system.conf.
 *
 * The controller is a "snps,dwc3" node: a Synopsys DWC3 core with an xHCI
 * inside, which is the same part on the CB2's RK3566 and on the Mi Pad 4's
 * SDM660.  What differs is everything the SoC puts around it - power
 * domains, clocks, resets, the PHY - and that is the glue's, chosen here by
 * the compatible string of the core's node or of its parent (the wrapper
 * node: "rockchip,rk3568-dwc3", "qcom,dwc3").  The core's own properties
 * are read here, the way Linux's dwc3 core driver reads them: dr_mode, the
 * PHY interface and the quirks that touch the PHY configuration.
 *
 * A core whose tree says "peripheral" is not a host, whatever the silicon
 * could do: that is the tree's statement about the board, and a tree that
 * wants a host says "host" or "otg".
 */

#include <minix/drivers.h>
#include <minix/fdt.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

#define MAX_CTRLS		4
#define MAXDEPTH		16

/* The glues this driver knows, by the wrapper's (or the core's) string. */
static const struct xhci_glue *const glues[] = {
	&xhci_rk_glue,
	&xhci_qcom_glue,
};

struct ctrl {
	struct xhci_devinfo info;
	const struct xhci_glue *glue;
	char glue_compat[48];		/* what the tree said, when no glue */
};

struct search {
	struct ctrl ctrl[MAX_CTRLS];
	unsigned nctrls;
	struct fdt_node stack[MAXDEPTH];	/* the nodes above this one */
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

static uint32_t
prop_u32(const struct fdt_node *node, const char *name, uint32_t def)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, name, &len)) == NULL || len < 4)
		return def;
	return (uint32_t)fdt_read_cells(p, 1);
}

static void
prop_str(const struct fdt_node *node, const char *name, char *buf,
	size_t size)
{
	const char *s;
	unsigned len;

	buf[0] = '\0';
	if ((s = fdt_getprop(node, name, &len)) != NULL && len > 0 &&
	    s[len - 1] == '\0')
		strlcpy(buf, s, size);
}

static int
has_prop(const struct fdt_node *node, const char *name)
{
	unsigned len;

	return fdt_getprop(node, name, &len) != NULL;
}

/* The glue for this core: its own compatible strings, then its parent's. */
static const struct xhci_glue *
glue_of(const struct fdt_node *node, const struct fdt_node *parent)
{
	unsigned i;

	for (i = 0; i < sizeof(glues) / sizeof(glues[0]); i++)
		if (fdt_node_is_compatible(node, glues[i]->compatible) ||
		    (parent != NULL &&
		    fdt_node_is_compatible(parent, glues[i]->compatible)))
			return glues[i];
	return NULL;
}

static void
read_ctrl(const struct fdt_node *node, const char *name,
	struct xhci_devinfo *c)
{
	const void *p;
	char phy_type[16];
	u64_t base, size;
	unsigned len, i;

	memset(c, 0, sizeof(*c));
	c->irq = -1;
	c->power_domain = -1;
	strlcpy(c->node, name, sizeof(c->node));

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		c->base = (phys_bytes)base;
		c->size = (size_t)size;
	}

	/*
	 * The first interrupt.  The RK3566 raises three - the controller, a
	 * wake-up line and a low-power-idle line - and only the first is
	 * the one an operating system has to serve.
	 */
	c->irq = fdt_node_gic_irq(node, 0);

	/* The core's own properties (Linux: dwc3_get_properties()). */
	prop_str(node, "dr_mode", c->dr_mode, sizeof(c->dr_mode));
	prop_str(node, "phy_type", phy_type, sizeof(phy_type));
	c->phy_type = strcmp(phy_type, "utmi_wide") == 0 ? XHCI_PHY_UTMI_WIDE :
	    strcmp(phy_type, "utmi") == 0 ? XHCI_PHY_UTMI : XHCI_PHY_UNSET;
	if (has_prop(node, "snps,dis_u2_susphy_quirk"))
		c->quirks |= XHCI_Q_DIS_U2_SUSPHY;
	if (has_prop(node, "snps,dis_enblslpm_quirk"))
		c->quirks |= XHCI_Q_DIS_ENBLSLPM;
	if (has_prop(node, "snps,dis-u2-freeclk-exists-quirk"))
		c->quirks |= XHCI_Q_DIS_U2_FREECLK;

	/* "resets" is <controller, line> pairs; both halves are used. */
	if ((p = fdt_getprop(node, "resets", &len)) != NULL && len >= 8) {
		c->reset_phandle = (uint32_t)fdt_read_cells(p, 1);
		for (i = 0; i + 8 <= len && c->nresets < XHCI_MAX_RESETS;
		    i += 8)
			c->reset_id[c->nresets++] = (unsigned)
			    fdt_read_cells((const char *)p + i + 4, 1);
	}

	/*
	 * "power-domains" is <controller, domain>.  Only the domain number
	 * is kept; which block holds it is the glue's business.
	 */
	if ((p = fdt_getprop(node, "power-domains", &len)) != NULL &&
	    len >= 8)
		c->power_domain = (int)fdt_read_cells((const char *)p + 4, 1);

	/*
	 * How this controller names its PHY.  Two spellings, and a
	 * controller may use either: "phys" points at a port of a PHY,
	 * "extcon" at a whole PHY.  Both are collected and resolved by the
	 * glue against what the PHY nodes turn out to be.
	 */
	if ((p = fdt_getprop(node, "phys", &len)) != NULL) {
		for (i = 0; (i + 1) * 4 <= len && c->nphy_phandles <
		    sizeof(c->phy_phandle) / sizeof(c->phy_phandle[0]); i++)
			c->phy_phandle[c->nphy_phandles++] = (uint32_t)
			    fdt_read_cells((const char *)p + i * 4, 1);
	}
	if (c->nphy_phandles <
	    sizeof(c->phy_phandle) / sizeof(c->phy_phandle[0])) {
		uint32_t extcon = prop_u32(node, "extcon", 0);

		if (extcon != 0)
			c->phy_phandle[c->nphy_phandles++] = extcon;
	}
}

static int
collect(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct search *s = cookie;
	const struct fdt_node *parent;
	struct ctrl *c;
	const char *compat;
	unsigned len;

	if (depth < 0 || depth >= MAXDEPTH)
		return 0;
	s->stack[depth] = *node;
	if (depth == 0 || !fdt_node_is_compatible(node, "snps,dwc3"))
		return 0;

	if (!node_enabled(node)) {
		log_debug(&xhci_log, "%s: disabled in the tree\n", name);
		return 0;
	}
	if (s->nctrls >= MAX_CTRLS)
		return 0;

	c = &s->ctrl[s->nctrls];
	read_ctrl(node, name, &c->info);
	if (c->info.base == 0)
		return 0;
	parent = &s->stack[depth - 1];
	c->glue = glue_of(node, parent);
	compat = fdt_getprop(parent, "compatible", &len);
	strlcpy(c->glue_compat, compat != NULL && len > 0 ? compat : "-",
	    sizeof(c->glue_compat));
	s->nctrls++;
	return 0;
}

int
xhci_find(struct xhci_devinfo *info, int skip)
{
	static struct search s;
	const struct ctrl *c;
	void *dtb;
	int r;

	memset(&s, 0, sizeof(s));
	memset(info, 0, sizeof(*info));
	info->irq = -1;
	info->power_domain = -1;

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&xhci_log, "this machine has no device tree\n");
		return ENXIO;
	}

	(void)fdt_walk(dtb, collect, &s);

	if (s.nctrls == 0) {
		log_warn(&xhci_log, "no USB controller this driver knows is "
		    "in this machine's device tree\n");
		free(dtb);
		return ENXIO;
	}
	if ((unsigned)skip >= s.nctrls) {
		log_warn(&xhci_log, "this machine has %u such controller(s), "
		    "not %d\n", s.nctrls, skip + 1);
		free(dtb);
		return ENXIO;
	}

	c = &s.ctrl[skip];
	*info = c->info;
	info->glue = c->glue;

	log_info(&xhci_log, "controller %d of %u at 0x%lx (%u bytes), "
	    "irq %d\n", skip, s.nctrls, (unsigned long)info->base,
	    (unsigned)info->size, info->irq);

	if (info->glue == NULL) {
		log_warn(&xhci_log, "%s: no glue this driver knows for its "
		    "wrapper (%s)\n", info->node, c->glue_compat);
		free(dtb);
		return ENXIO;
	}
	if (strcmp(info->dr_mode, "peripheral") == 0) {
		log_warn(&xhci_log, "%s: the tree says dr_mode \"peripheral\"; "
		    "not a host\n", info->node);
		free(dtb);
		return ENXIO;
	}

	r = info->glue->find(dtb, info);
	free(dtb);
	return r;
}
