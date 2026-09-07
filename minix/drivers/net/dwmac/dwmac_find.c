/*
 * What this machine has, asked of the device tree.
 *
 * The same shape as sdmmc_host.c, and for the same reason: this port has no
 * board identifiers, it has the tree the loader passed, and RS grants the
 * registers and the interrupt line out of that same tree by way of the
 * devicetree "..." lines in system.conf.
 *
 * One thing is done differently here, and better.  sdmmc finds the block a
 * node points at - its reset controller - by looking for the compatible
 * string, because the reader could not follow a phandle.  That works while
 * there is one such block on the machine and stops at the second, and this
 * driver needs a GPIO bank, of which the SoC has five.  So the reader
 * learned to follow a phandle (fdt_phandle_reg), and the three blocks this
 * controller points at - its GRF, its reset controller and the bank that
 * resets its PHY - are found by following the pointer the tree actually
 * wrote, not by guessing which of the kind was meant.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dwmac.h"

/*
 * The controllers this driver knows.  The generic string is matched too, so
 * that another Rockchip SoC with the same block needs no new line here -
 * but the glue in dwmac_rk.c is written for the RK3568 register layout, and
 * a part that is not one of these would need that checked before it is
 * added.
 */
static const char *const compatibles[] = {
	"rockchip,rk3568-gmac",
	"rockchip,rk3566-gmac",
};

struct search {
	struct dwmac_devinfo *info;
	int skip;
	int found;			/* the controller has been recorded */
	int ctrl_depth;			/* and at what depth, to know its kids */
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
read_devinfo(const struct fdt_node *node, struct dwmac_devinfo *info)
{
	const void *p;
	u64_t base, size;
	unsigned len, i;
	const char *s;

	memset(info, 0, sizeof(*info));
	info->irq = -1;
	info->reset_pin = -1;
	info->phy_addr = -1;

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		info->base = (phys_bytes)base;
		info->size = (size_t)size;
	}

	/*
	 * The first interrupt, which the tree calls "macirq".  The two after
	 * it are wake-on-lan and energy-efficient-ethernet signalling, and
	 * on the board neither has fired once in the vendor system's uptime.
	 */
	info->irq = fdt_node_gic_irq(node, 0);

	info->tx_delay = prop_u32(node, "tx_delay", 0);
	info->rx_delay = prop_u32(node, "rx_delay", 0);

	if ((s = fdt_getprop(node, "clock_in_out", &len)) != NULL && len > 0)
		info->clock_from_phy = (strcmp(s, "input") == 0);

	/* The GRF, followed by its phandle. */
	if (fdt_phandle_reg(node->dtb, prop_u32(node, "rockchip,grf", 0), 0,
	    &base, &size) == 0) {
		info->grf_base = (phys_bytes)base;
		info->grf_size = (size_t)size;
	}

	/*
	 * "resets" is <controller, line> pairs: the controller as a phandle,
	 * the line as a number.  Both halves are used - unlike in sdmmc,
	 * which had to find the controller another way.
	 */
	if ((p = fdt_getprop(node, "resets", &len)) != NULL && len >= 8) {
		if (fdt_phandle_reg(node->dtb,
		    (uint32_t)fdt_read_cells(p, 1), 0, &base, &size) == 0) {
			info->cru_base = (phys_bytes)base;
			info->cru_size = (size_t)size;
		}
		for (i = 0; i + 8 <= len && info->nresets < DWMAC_MAX_RESETS;
		    i += 8)
			info->reset_id[info->nresets++] = (unsigned)
			    fdt_read_cells((const char *)p + i + 4, 1);
	}

	/*
	 * "snps,reset-gpio" is <bank, pin, flags>, and the flags say active
	 * low the way every GPIO binding does: bit 0 set.  The delays that
	 * go with it are in microseconds and generous - a quarter of a
	 * second in all on this board.
	 */
	if ((p = fdt_getprop(node, "snps,reset-gpio", &len)) != NULL &&
	    len >= 12) {
		if (fdt_phandle_reg(node->dtb,
		    (uint32_t)fdt_read_cells(p, 1), 0, &base, &size) == 0) {
			info->gpio_base = (phys_bytes)base;
			info->gpio_size = (size_t)size;
			info->reset_pin =
			    (int)fdt_read_cells((const char *)p + 4, 1);
			info->reset_active_low =
			    (fdt_read_cells((const char *)p + 8, 1) & 1) != 0;
		}
	}
	if (fdt_getprop(node, "snps,reset-active-low", NULL) != NULL)
		info->reset_active_low = 1;

	if ((p = fdt_getprop(node, "snps,reset-delays-us", &len)) != NULL) {
		for (i = 0; i < 3 && (i + 1) * 4 <= len; i++)
			info->reset_delay_us[i] = (unsigned)
			    fdt_read_cells((const char *)p + i * 4, 1);
	}
}

/*
 * One walk, in two states.
 *
 * Before the controller is found the callback is looking for it.  After,
 * it is inside the controller's subtree, where the PHY's address on the
 * MDIO bus lives as the "reg" of a node called phy@N - the tree states it
 * there and nowhere else.  Nodes deeper than the controller are its
 * children; the first node that is not tells us the subtree is over, and
 * that is where the walk stops.
 */
static int
find_controller(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct search *s = cookie;
	const void *p;
	unsigned i, len;

	if (depth == 0)
		return 0;

	if (s->found) {
		if (depth <= s->ctrl_depth)
			return 1;		/* past the subtree: done */

		if (s->info->phy_addr < 0 && strncmp(name, "phy", 3) == 0 &&
		    (p = fdt_getprop(node, "reg", &len)) != NULL && len >= 4)
			s->info->phy_addr = (int)fdt_read_cells(p, 1);

		return 0;
	}

	for (i = 0; i < sizeof(compatibles) / sizeof(compatibles[0]); i++) {
		if (!fdt_node_is_compatible(node, compatibles[i]))
			continue;
		if (!node_enabled(node)) {
			log_debug(&dwmac_log, "%s: disabled in the tree\n",
			    name);
			continue;
		}

		read_devinfo(node, s->info);
		if (s->info->base == 0 || s->info->size == 0) {
			log_warn(&dwmac_log, "%s: no usable reg\n", name);
			continue;
		}

		if (s->skip > 0) {
			s->skip--;
			continue;
		}

		/* Recorded; the walk goes on into this node's children. */
		s->found = 1;
		s->ctrl_depth = depth;
		return 0;
	}
	return 0;
}

int
dwmac_find(struct dwmac_devinfo *info, int skip)
{
	struct search s;
	void *dtb;

	memset(&s, 0, sizeof(s));
	s.info = info;
	s.skip = skip;

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&dwmac_log, "this machine has no device tree\n");
		return ENXIO;
	}

	(void)fdt_walk(dtb, find_controller, &s);
	free(dtb);

	if (!s.found) {
		log_warn(&dwmac_log, "no network controller this driver "
		    "knows is in this machine's device tree\n");
		return ENXIO;
	}

	log_info(&dwmac_log, "controller at 0x%lx (%u bytes), irq %d\n",
	    (unsigned long)info->base, (unsigned)info->size, info->irq);
	log_info(&dwmac_log, "grf 0x%lx, cru 0x%lx, %u reset lines, "
	    "gpio 0x%lx pin %d%s\n", (unsigned long)info->grf_base,
	    (unsigned long)info->cru_base, info->nresets,
	    (unsigned long)info->gpio_base, info->reset_pin,
	    info->reset_active_low ? " (active low)" : "");
	log_info(&dwmac_log, "rgmii delays tx 0x%x rx 0x%x, clock %s, "
	    "phy at %d\n", info->tx_delay, info->rx_delay,
	    info->clock_from_phy ? "from the PHY" : "from the SoC",
	    info->phy_addr);

	return OK;
}
