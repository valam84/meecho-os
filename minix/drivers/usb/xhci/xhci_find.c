/*
 * What this machine has, asked of the device tree.
 *
 * The same shape as dwmac_find.c and sdmmc_host.c, and for the same
 * reason: this port has no board identifiers, it has the tree the loader
 * passed, and RS grants the registers and the interrupt out of that same
 * tree by way of the devicetree "..." lines in system.conf.
 *
 * One thing here is harder than in the drivers before it, and it is worth
 * saying why.  A controller is a node that says "snps,dwc3", and this SoC
 * has two of them; what tells them apart is not their names but what they
 * are wired to.  Both are on the same USB2 PHY, on its two different
 * ports, and the wiring is written as pointers: the host-capable one names
 * its PHY port in "phys", and the OTG-capable one names the whole PHY in
 * "extcon" because on it the PHY also does the role signalling.  So the
 * walk collects both kinds of node - controllers and PHYs, with the
 * phandles of the PHYs' port children - and afterwards follows the pointer
 * the tree actually wrote.  Guessing "the first PHY" would work on this
 * board and stop working on the next.
 *
 * The blocks around them are found the same way: the clock-and-reset
 * controller through the "resets" phandle, the PHY's logical register file
 * through "rockchip,usbgrf", the PMU's clock controller through the PHY's
 * "clocks".  Only the power management unit is looked for by compatible
 * string, because the tree points at its child (the power controller,
 * which has no registers of its own) rather than at the block.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

#define MAX_CTRLS		4
#define MAX_PHYS		4
#define MAX_PHY_PORTS		2

/*
 * The controllers this driver knows.  "snps,dwc3" is the core itself; the
 * Rockchip wrapper node above it carries the clocks and is matched only to
 * be skipped, since it has no registers.
 */
static const char *const ctrl_compatibles[] = {
	"snps,dwc3",
};

static const char *const phy_compatibles[] = {
	"rockchip,rk3568-usb2phy",
	"rockchip,rk3566-usb2phy",
};

static const char *const pmu_compatibles[] = {
	"rockchip,rk3568-pmu",
	"rockchip,rk3566-pmu",
};

struct ctrl {
	phys_bytes base;
	size_t size;
	int irq;
	uint32_t reset_phandle;
	unsigned reset_id[XHCI_MAX_RESETS];
	unsigned nresets;
	uint32_t pd_phandle;
	int pd_domain;
	uint32_t phy_phandle[MAX_PHY_PORTS + 1];
	unsigned nphy_phandles;
};

struct phy {
	phys_bytes base;
	size_t size;
	uint32_t phandle;		/* the PHY node itself */
	uint32_t usbgrf_phandle;
	uint32_t clock_phandle;		/* the PMU clock controller */
	uint32_t port_phandle[MAX_PHY_PORTS];	/* [0] otg, [1] host */
};

struct search {
	struct ctrl ctrl[MAX_CTRLS];
	unsigned nctrls;

	struct phy phy[MAX_PHYS];
	unsigned nphys;
	int in_phy;			/* index+1 of the PHY being walked */
	int phy_depth;

	phys_bytes pmu_base;
	size_t pmu_size;

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

static int
is_one_of(const struct fdt_node *node, const char *const *list, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (fdt_node_is_compatible(node, list[i]))
			return 1;
	return 0;
}

static void
read_ctrl(const struct fdt_node *node, struct ctrl *c)
{
	const void *p;
	u64_t base, size;
	unsigned len, i;

	memset(c, 0, sizeof(*c));
	c->irq = -1;
	c->pd_domain = -1;

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		c->base = (phys_bytes)base;
		c->size = (size_t)size;
	}

	/*
	 * The first interrupt.  This part raises three - the controller, a
	 * wake-up line and a low-power-idle line - and only the first is
	 * the one an operating system has to serve.
	 */
	c->irq = fdt_node_gic_irq(node, 0);

	/* "resets" is <controller, line> pairs; both halves are used. */
	if ((p = fdt_getprop(node, "resets", &len)) != NULL && len >= 8) {
		c->reset_phandle = (uint32_t)fdt_read_cells(p, 1);
		for (i = 0; i + 8 <= len && c->nresets < XHCI_MAX_RESETS;
		    i += 8)
			c->reset_id[c->nresets++] = (unsigned)
			    fdt_read_cells((const char *)p + i + 4, 1);
	}

	/*
	 * "power-domains" is <controller, domain>.  The controller here is
	 * the power controller node, which carries no registers of its own
	 * - they belong to its parent - so only the domain number is kept
	 * and the block itself is found by compatible string below.
	 */
	if ((p = fdt_getprop(node, "power-domains", &len)) != NULL &&
	    len >= 8) {
		c->pd_phandle = (uint32_t)fdt_read_cells(p, 1);
		c->pd_domain =
		    (int)fdt_read_cells((const char *)p + 4, 1);
	}

	/*
	 * How this controller names its PHY.  Two spellings, and a
	 * controller may use either: "phys" points at a port of a PHY,
	 * "extcon" at a whole PHY.  Both are collected and resolved later
	 * against what the PHY nodes turn out to be.
	 */
	if ((p = fdt_getprop(node, "phys", &len)) != NULL) {
		for (i = 0; (i + 1) * 4 <= len &&
		    c->nphy_phandles < MAX_PHY_PORTS + 1; i++)
			c->phy_phandle[c->nphy_phandles++] = (uint32_t)
			    fdt_read_cells((const char *)p + i * 4, 1);
	}
	if (c->nphy_phandles < MAX_PHY_PORTS + 1) {
		uint32_t extcon = prop_u32(node, "extcon", 0);

		if (extcon != 0)
			c->phy_phandle[c->nphy_phandles++] = extcon;
	}
}

static int
collect(void *cookie, int depth, const char *name, const struct fdt_node *node)
{
	struct search *s = cookie;
	unsigned len;

	if (depth == 0)
		return 0;

	/*
	 * Inside a PHY node: its ports are children, and the port is what a
	 * controller points at.  The tree names them by function, which is
	 * the only place that distinction is written down.
	 */
	if (s->in_phy != 0) {
		if (depth <= s->phy_depth) {
			s->in_phy = 0;
		} else {
			struct phy *ph = &s->phy[s->in_phy - 1];
			uint32_t ph_handle = prop_u32(node, "phandle", 0);

			if (ph_handle != 0 && strncmp(name, "otg-port", 8) == 0)
				ph->port_phandle[0] = ph_handle;
			else if (ph_handle != 0 &&
			    strncmp(name, "host-port", 9) == 0)
				ph->port_phandle[1] = ph_handle;
			return 0;
		}
	}

	if (is_one_of(node, ctrl_compatibles,
	    sizeof(ctrl_compatibles) / sizeof(ctrl_compatibles[0]))) {
		if (!node_enabled(node)) {
			log_debug(&xhci_log, "%s: disabled in the tree\n",
			    name);
			return 0;
		}
		if (s->nctrls < MAX_CTRLS) {
			read_ctrl(node, &s->ctrl[s->nctrls]);
			if (s->ctrl[s->nctrls].base != 0)
				s->nctrls++;
		}
		return 0;
	}

	if (is_one_of(node, phy_compatibles,
	    sizeof(phy_compatibles) / sizeof(phy_compatibles[0]))) {
		u64_t base, size;
		struct phy *ph;

		if (!node_enabled(node))
			return 0;
		if (s->nphys >= MAX_PHYS)
			return 0;
		if (fdt_node_reg(node, 0, &base, &size) != 0)
			return 0;

		ph = &s->phy[s->nphys++];
		memset(ph, 0, sizeof(*ph));
		ph->base = (phys_bytes)base;
		ph->size = (size_t)size;
		ph->phandle = prop_u32(node, "phandle", 0);
		ph->usbgrf_phandle = prop_u32(node, "rockchip,usbgrf", 0);
		ph->clock_phandle = prop_u32(node, "clocks", 0);

		s->in_phy = (int)s->nphys;
		s->phy_depth = depth;
		return 0;
	}

	if (s->pmu_base == 0 && is_one_of(node, pmu_compatibles,
	    sizeof(pmu_compatibles) / sizeof(pmu_compatibles[0]))) {
		u64_t base, size;

		if (fdt_node_reg(node, 0, &base, &size) == 0) {
			s->pmu_base = (phys_bytes)base;
			s->pmu_size = (size_t)size;
		}
		return 0;
	}

	(void)len;
	return 0;
}

/*
 * Which PHY and which of its ports this controller is wired to, following
 * the pointers the tree wrote.  Answers -1 when nothing matches, and that
 * is a real answer: a controller whose PHY is not in the tree cannot be
 * brought up, and saying so beats bringing up the wrong one.
 */
static int
resolve_phy(struct search *s, const struct ctrl *c, unsigned *phy_idx,
	int *port)
{
	unsigned i, p, k;

	for (k = 0; k < c->nphy_phandles; k++) {
		uint32_t want = c->phy_phandle[k];

		for (i = 0; i < s->nphys; i++) {
			for (p = 0; p < MAX_PHY_PORTS; p++) {
				if (s->phy[i].port_phandle[p] == want &&
				    want != 0) {
					*phy_idx = i;
					*port = (int)p;
					return 0;
				}
			}
			if (s->phy[i].phandle == want && want != 0) {
				/*
				 * The whole PHY, which is how the OTG-capable
				 * controller names it: the role signalling
				 * belongs to the PHY as a whole, and the port
				 * it means is the OTG one.
				 */
				*phy_idx = i;
				*port = 0;
				return 0;
			}
		}
	}
	return -1;
}

int
xhci_find(struct xhci_devinfo *info, int skip)
{
	struct search s;
	const struct ctrl *c;
	struct phy *ph;
	unsigned phy_idx;
	u64_t base, size;
	int port;
	void *dtb;

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
	info->base = c->base;
	info->size = c->size;
	info->irq = c->irq;
	info->nresets = c->nresets;
	memcpy(info->reset_id, c->reset_id, sizeof(info->reset_id));
	info->power_domain = c->pd_domain;

	if (c->reset_phandle != 0 && fdt_phandle_reg(dtb, c->reset_phandle, 0,
	    &base, &size) == 0) {
		info->cru_base = (phys_bytes)base;
		info->cru_size = (size_t)size;
	}

	info->pmu_base = s.pmu_base;
	info->pmu_size = s.pmu_size;

	if (resolve_phy(&s, c, &phy_idx, &port) == 0) {
		ph = &s.phy[phy_idx];
		info->phy_base = ph->base;
		info->phy_size = ph->size;
		info->phy_port = port;
		info->phy_unit = (ph->base == RK3568_USB2PHY1_BASE) ? 1 : 0;

		if (ph->usbgrf_phandle != 0 && fdt_phandle_reg(dtb,
		    ph->usbgrf_phandle, 0, &base, &size) == 0) {
			info->usbgrf_base = (phys_bytes)base;
			info->usbgrf_size = (size_t)size;
		}
		if (ph->clock_phandle != 0 && fdt_phandle_reg(dtb,
		    ph->clock_phandle, 0, &base, &size) == 0) {
			info->pmucru_base = (phys_bytes)base;
			info->pmucru_size = (size_t)size;
		}
	}

	free(dtb);

	log_info(&xhci_log, "controller %d of %u at 0x%lx (%u bytes), "
	    "irq %d\n", skip, s.nctrls, (unsigned long)info->base,
	    (unsigned)info->size, info->irq);
	log_info(&xhci_log, "cru 0x%lx, %u reset line(s)%s%u, pmu 0x%lx "
	    "domain %d\n", (unsigned long)info->cru_base, info->nresets,
	    info->nresets ? ", first " : ", ",
	    info->nresets ? info->reset_id[0] : 0,
	    (unsigned long)info->pmu_base, info->power_domain);

	if (info->phy_base == 0) {
		log_warn(&xhci_log, "the tree names no USB2 PHY for this "
		    "controller\n");
		return ENXIO;
	}

	log_info(&xhci_log, "usb2phy%d at 0x%lx, %s port, usbgrf 0x%lx, "
	    "pmucru 0x%lx\n", info->phy_unit, (unsigned long)info->phy_base,
	    info->phy_port == 0 ? "otg" : "host",
	    (unsigned long)info->usbgrf_base,
	    (unsigned long)info->pmucru_base);

	if (info->usbgrf_base == 0) {
		log_warn(&xhci_log, "the PHY names no usbgrf; its suspend "
		    "and clock-output fields live there and nowhere else\n");
		return ENXIO;
	}

	return OK;
}
