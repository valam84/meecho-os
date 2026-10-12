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
 * second line and a second file - the target board has two, an SDHCI part
 * for its eMMC and a Synopsys dw-mshc for its SD socket.
 *
 * A machine with two controllers this driver could drive is served by two
 * instances of the driver, and the instance number says how many matches to
 * skip - the same convention virtio_blk uses for a second disk. Which match
 * is which follows one rule, so that it does not depend on the board:
 * soldered devices ("non-removable") first, then sockets, each group in tree
 * order. A machine's own disk is instance 0 whether or not it has a socket,
 * a card in it, or a driver for it - which is what keeps the root of the
 * target board on c0d0 now that its SD socket is driven too; in tree order
 * the socket comes first.
 *
 * A node that says it carries neither an SD card nor an MMC ("no-sd" and
 * "no-mmc" both) is an SDIO slot - on the target board, the Wi-Fi chip on
 * the second dw-mshc - and is not a block device; it is skipped and counts
 * for nothing.
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
	/* The Synopsys DesignWare MSHC: Rockchip's SD and SDIO sockets from
	 * the RK3288 on, and the generic binding every other SoC with the
	 * part uses. sdmmc_dwmshc.c tells the two apart itself. */
	{ "rockchip,rk3288-dw-mshc",	dwmshc_probe },
	{ "snps,dw-mshc",		dwmshc_probe },
};

struct search {
	struct sdmmc_host *host;
	int skip;
	int soldered;		/* this pass: soldered devices, or sockets */
	int refused;		/* the instance's node was refused */
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
	if (fdt_getprop(node, "cap-sd-highspeed", NULL) != NULL)
		info->cap_sd_hs = 1;
}

/*
 * The reset lines, every one the tree names, each followed to its holder.
 *
 * The first version took the second cell of each pair and found the holder
 * by looking for "rockchip,rk3568-cru" anywhere in the tree, on the grounds
 * that a machine has one clock-and-reset unit. That is true of this SoC and
 * made the host file a host file for it; sdmmc_clk.c follows the phandle
 * instead, and a line whose holder it does not know is counted and left
 * alone rather than pulled at some other controller's address.
 */
static void
read_resets(const struct fdt_node *node, const char *name,
	struct sdmmc_devinfo *info)
{
	struct sdmmc_line line;
	unsigned i, unknown = 0;
	int r;

	for (i = 0; info->nresets < SDMMC_MAX_RESETS; i++) {
		r = sdmmc_line_get(node, "resets", NULL, NULL, i, &line);
		if (r == ENOENT)
			break;
		if (r == OK)
			info->reset[info->nresets++] = line;
		else
			unknown++;
	}
	if (unknown > 0)
		log_warn(&sdmmc_log, "%s: %u reset lines held by a controller "
		    "this driver does not know; not pulled\n", name, unknown);
}

static int
find_host(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct search *s = cookie;
	struct sdmmc_devinfo info;
	const struct host_driver *drv = NULL;
	unsigned i;

	if (depth == 0)
		return 0;

	/*
	 * One node, one count - whatever the number of lines in the table it
	 * matches. The eMMC node of the target board carries two strings the
	 * table has, and the first version of this walk counted the node once
	 * per matching line: instance 1 skipped the eMMC under the first
	 * string and took it under the second. Two drivers on one controller
	 * is not a slow disk but a root file system whose controller is reset
	 * under it, which is how this was found.
	 */
	for (i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
		if (fdt_node_is_compatible(node, drivers[i].compatible)) {
			drv = &drivers[i];
			break;
		}
	}
	if (drv == NULL)
		return 0;

	if (!node_enabled(node)) {
		log_debug(&sdmmc_log, "%s: disabled in the tree\n", name);
		return 0;
	}
	if (fdt_getprop(node, "no-sd", NULL) != NULL &&
	    fdt_getprop(node, "no-mmc", NULL) != NULL) {
		log_debug(&sdmmc_log, "%s: SDIO only; not a disk\n", name);
		return 0;
	}

	read_devinfo(node, &info);
	if (info.non_removable != s->soldered)
		return 0;	/* the other pass's */
	if (info.base == 0 || info.size == 0) {
		log_warn(&sdmmc_log, "%s: no usable reg\n", name);
		return 0;
	}

	if (s->skip > 0) {
		s->skip--;
		return 0;
	}

	/*
	 * This node is the instance's, and only this one. A driver that
	 * refuses it fails the instance rather than moving on: the next node
	 * along is somebody else's number, possibly a device that another
	 * instance is already serving.
	 */
	read_resets(node, name, &info);
	if (drv->probe(node, &info, s->host) != OK) {
		log_warn(&sdmmc_log, "%s: %s does not take this node\n", name,
		    drv->compatible);
		s->refused = 1;
		return 1;	/* stops the walk */
	}

	s->host->name = drv->compatible;
	s->host->sd_highspeed = info.cap_sd_hs;
	log_info(&sdmmc_log, "%s: %s at 0x%lx (%u bytes), irq %d, "
	    "%u-bit bus, up to %u Hz%s, %u reset lines\n",
	    name, drv->compatible, (unsigned long)info.base,
	    (unsigned)info.size, info.irq, info.bus_width,
	    info.max_freq, info.non_removable ? ", soldered" : "",
	    info.nresets);
	s->found = drv;
	return 1;	/* stops the walk */
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

	s.soldered = 1;
	(void)fdt_walk(dtb, find_host, &s);
	if (s.found == NULL && !s.refused) {
		s.soldered = 0;
		(void)fdt_walk(dtb, find_host, &s);
	}
	free(dtb);

	return (s.found != NULL) ? OK : ENXIO;
}
