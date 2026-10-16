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
 * An SDIO slot is not a block device; it is skipped and counts for nothing.
 * Two things say a node is one: "no-sd" and "no-mmc" both (the Wi-Fi chip
 * on the target board's second dw-mshc), or a device described under it
 * (the Wi-Fi chip of an H313 box, which says nothing else - see
 * collect_host()).
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
	/* Qualcomm's SDHCI, version 5 of their wrapper (SDM630/636/660 and
	 * later): taken over as the boot loader left it, never set up
	 * again, and written only through a GPT window - see
	 * sdmmc_sdhci.c, msm_takeover(), and sdmmc_gpt.c. */
	{ "qcom,sdhci-msm-v5",		sdhci_probe },
	/* The Synopsys DesignWare MSHC: Rockchip's SD and SDIO sockets from
	 * the RK3288 on, and the generic binding every other SoC with the
	 * part uses. sdmmc_dwmshc.c tells the two apart itself. */
	{ "rockchip,rk3288-dw-mshc",	dwmshc_probe },
	{ "snps,dw-mshc",		dwmshc_probe },
	/* Allwinner's controller - a Synopsys descendant with registers of
	 * its own - from the A20 to the H616 and H313. sdmmc_sunxi.c picks
	 * the variant (descriptor format, timing mode) itself. */
	{ "allwinner,sun50i-h616-mmc",	sunxi_probe },
	{ "allwinner,sun50i-h616-emmc",	sunxi_probe },
	{ "allwinner,sun50i-a100-mmc",	sunxi_probe },
	{ "allwinner,sun50i-a100-emmc",	sunxi_probe },
	{ "allwinner,sun50i-a64-mmc",	sunxi_probe },
	{ "allwinner,sun50i-a64-emmc",	sunxi_probe },
	{ "allwinner,sun7i-a20-mmc",	sunxi_probe },
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

/*
 * The walk collects, and only collects: every enabled node the table has a
 * driver for, with what the numbering needs to know about it. Deciding in
 * the walk is not possible for the one property that is not on the node
 * itself - whether a device sits *under* it - because a node's children come
 * after it.
 *
 * One node is one entry whatever the number of lines in the table it
 * matches. The eMMC node of the target board carries two strings the table
 * has, and the first version of this walk counted the node once per
 * matching line: instance 1 skipped the eMMC under the first string and took
 * it under the second. Two drivers on one controller is not a slow disk but
 * a root file system whose controller is reset under it, which is how this
 * was found.
 */
#define MAX_CANDIDATES	16

struct candidate {
	struct fdt_node node;		/* valid while the blob is */
	char name[40];
	int depth;
	int soldered;
	int sdio;			/* a function device sits under it */
	const struct host_driver *drv;
};

struct collect {
	struct candidate c[MAX_CANDIDATES];
	unsigned n;
	int open;			/* the candidate whose children these are */
};

static int
collect_host(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct collect *s = cookie;
	struct candidate *c;
	const struct host_driver *drv = NULL;
	unsigned i;

	if (depth == 0)
		return 0;

	/*
	 * A child of a candidate is a device on its bus. The binding allows
	 * exactly one kind of child on a memory card's bus, "mmc-card" (an
	 * eMMC needing a quirk described); anything else is an SDIO function
	 * - the Wi-Fi chip of the Tanix TX1 is "wifi@1" under mmc1, with no
	 * "no-sd" or "no-mmc" to say so, and soldered on, which would make
	 * it the machine's disk number 0.
	 *
	 * A device on the bus has an address there - "reg", the function
	 * number.  A child without one is a description of the controller,
	 * not a device: the Qualcomm trees put an "opp-table" (the
	 * controller's clock and bandwidth levels) under every host, and the
	 * first version of this rule called the Mi Pad 4's eMMC an SDIO slot
	 * for it and left the machine without a disk.
	 */
	if (s->open >= 0 && depth <= s->c[s->open].depth)
		s->open = -1;
	if (s->open >= 0 && depth == s->c[s->open].depth + 1 &&
	    fdt_getprop(node, "reg", NULL) != NULL &&
	    !fdt_node_is_compatible(node, "mmc-card"))
		s->c[s->open].sdio = 1;

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
	if (s->n == MAX_CANDIDATES)
		return 0;

	c = &s->c[s->n];
	memset(c, 0, sizeof(*c));
	c->node = *node;
	strlcpy(c->name, name, sizeof(c->name));
	c->depth = depth;
	c->drv = drv;
	c->soldered = (fdt_getprop(node, "non-removable", NULL) != NULL);
	c->sdio = (fdt_getprop(node, "no-sd", NULL) != NULL &&
	    fdt_getprop(node, "no-mmc", NULL) != NULL);
	s->open = (int)s->n++;
	return 0;
}

/*
 * This node is the instance's, and only this one. A driver that refuses it
 * fails the instance rather than moving on: the next node along is somebody
 * else's number, possibly a device that another instance is already serving.
 */
static int
take_host(const struct candidate *c, struct sdmmc_host *host)
{
	struct sdmmc_devinfo info;

	read_devinfo(&c->node, &info);
	read_resets(&c->node, c->name, &info);
	if (c->drv->probe(&c->node, &info, host) != OK) {
		log_warn(&sdmmc_log, "%s: %s does not take this node\n",
		    c->name, c->drv->compatible);
		return ENXIO;
	}

	host->name = c->drv->compatible;
	host->sd_highspeed = info.cap_sd_hs;
	log_info(&sdmmc_log, "%s: %s at 0x%lx (%u bytes), irq %d, "
	    "%u-bit bus, up to %u Hz%s, %u reset lines\n",
	    c->name, c->drv->compatible, (unsigned long)info.base,
	    (unsigned)info.size, info.irq, info.bus_width,
	    info.max_freq, info.non_removable ? ", soldered" : "",
	    info.nresets);
	return OK;
}

int
sdmmc_host_find(struct sdmmc_host *host)
{
	static struct collect s;
	struct sdmmc_devinfo info;
	long instance = 0;
	int pass, skip, r = ENXIO;
	unsigned i;
	void *dtb;

	(void)env_parse("instance", "d", 0, &instance, 0, 3);

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&sdmmc_log, "this machine has no device tree\n");
		return ENXIO;
	}

	memset(&s, 0, sizeof(s));
	s.open = -1;
	(void)fdt_walk(dtb, collect_host, &s);

	/* Soldered first, then sockets; tree order within each. */
	skip = (int)instance;
	for (pass = 1; pass >= 0; pass--) {
		for (i = 0; i < s.n; i++) {
			struct candidate *c = &s.c[i];

			if (c->soldered != pass)
				continue;
			if (c->sdio) {
				log_debug(&sdmmc_log, "%s: an SDIO slot, not a "
				    "disk\n", c->name);
				continue;
			}
			read_devinfo(&c->node, &info);
			if (info.base == 0 || info.size == 0) {
				log_warn(&sdmmc_log, "%s: no usable reg\n",
				    c->name);
				continue;
			}
			if (skip-- > 0)
				continue;

			r = take_host(c, host);
			goto done;
		}
	}
done:
	free(dtb);
	return r;
}
