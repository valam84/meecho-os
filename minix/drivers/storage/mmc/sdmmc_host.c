/*
 * Which SD/MMC controller this machine has: asked of the device tree.
 *
 * The same shape as readclock's arch_readclock.c, and for the same reason.
 * The 32-bit ports pick a controller by board identifier - mmchost_mmchs.c
 * asks minix/board.h whether this is a BeagleBone Black or a BeagleBoard XM
 * - and a board identifier is a thing this port does not have. What it has
 * is the tree the loader passed, which names every controller, where its
 * registers are, which interrupt it raises and how wide the board wired its
 * bus. RS grants the registers and the line from the same tree, by way of
 * the devicetree "..." lines in system.conf.
 *
 * The list below is of parts, not of boards: one line per register set this
 * driver knows, matched on the compatible string. A second controller is a
 * second line and a second file, which is not hypothetical here - the target
 * board's SD socket is a Synopsys dw-mshc and wants one.
 *
 * The first match wins and the walk stops. A machine with two controllers
 * this driver could drive is served by two instances of the driver, and the
 * instance number says how many matches to skip - the same convention
 * virtio_blk uses for a second disk.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sdmmc.h"

struct host_driver {
	const char *compatible;
	int (*probe)(const struct fdt_node *node,
	    const struct sdmmc_devinfo *info, struct sdmmc_host *host);
};

static const struct host_driver drivers[] = {
	/* The RK3566/RK3568 eMMC: a DesignWare Cores part with SDHCI
	 * registers. Both strings appear on the same node; matching the
	 * generic one first means a future rk3588 needs no new line. */
	{ "rockchip,dwcmshc-sdhci",	sdhci_probe },
	{ "rockchip,rk3568-dwcmshc",	sdhci_probe },
};

struct search {
	struct sdmmc_host *host;
	int skip;
	const struct host_driver *found;
};

/* A node is usable unless it says otherwise. */
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

static void
read_devinfo(const struct fdt_node *node, struct sdmmc_devinfo *info)
{
	const void *p;
	u64_t base, size;
	unsigned len;

	memset(info, 0, sizeof(*info));
	info->irq = -1;
	info->bus_width = 1;

	if (fdt_node_reg(node, 0, &base, &size) == 0) {
		info->base = (phys_bytes)base;
		info->size = (size_t)size;
	}
	info->irq = fdt_node_gic_irq(node, 0);

	if ((p = fdt_getprop(node, "bus-width", &len)) != NULL && len >= 4)
		info->bus_width = (unsigned)fdt_read_cells(p, 1);
	if ((p = fdt_getprop(node, "max-frequency", &len)) != NULL && len >= 4)
		info->max_freq = (uint32_t)fdt_read_cells(p, 1);
	if (fdt_getprop(node, "non-removable", NULL) != NULL)
		info->non_removable = 1;
}

static int
find_host(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct search *s = cookie;
	struct sdmmc_devinfo info;
	unsigned i;

	if (depth == 0)
		return 0;

	for (i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
		if (!fdt_node_is_compatible(node, drivers[i].compatible))
			continue;
		if (!node_enabled(node)) {
			log_debug(&sdmmc_log, "%s: disabled in the tree\n",
			    name);
			continue;
		}

		read_devinfo(node, &info);
		if (info.base == 0 || info.size == 0) {
			log_warn(&sdmmc_log, "%s: no usable reg\n", name);
			continue;
		}

		if (s->skip > 0) {
			s->skip--;
			continue;
		}

		if (drivers[i].probe(node, &info, s->host) != OK)
			continue;

		s->host->name = drivers[i].compatible;
		log_info(&sdmmc_log, "%s: %s at 0x%lx (%u bytes), irq %d, "
		    "%u-bit bus, up to %u Hz%s\n", name,
		    drivers[i].compatible, (unsigned long)info.base,
		    (unsigned)info.size, info.irq, info.bus_width,
		    info.max_freq, info.non_removable ? ", soldered" : "");
		s->found = &drivers[i];
		return 1;	/* stops the walk */
	}
	return 0;
}

int
sdmmc_host_find(struct sdmmc_host *host)
{
	struct search s;
	long instance = 0;
	void *dtb;

	(void)env_parse("instance", "d", 0, &instance, 0, 3);

	memset(&s, 0, sizeof(s));
	s.host = host;
	s.skip = (int)instance;

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&sdmmc_log, "this machine has no device tree\n");
		return ENXIO;
	}

	(void)fdt_walk(dtb, find_host, &s);
	free(dtb);

	return (s.found != NULL) ? OK : ENXIO;
}
