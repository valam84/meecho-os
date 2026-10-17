/*
 * Qualcomm's glue ("qcom,dwc3"): the Mi Pad 4's USB-C port as a host
 * (port/clover/hw-usbhost.md).
 *
 * The same port is a device the rest of the time - the network to the hub,
 * dwc3_ecm - and this glue is that driver's, borrowed whole: its tree
 * reader (dwc3_find.c), its wrapper (dwc3_qcom.c) and its PHY (qusb2.c),
 * built here from ../../net/dwc3_ecm, with the SoC's clock controller
 * through qcom_soc as there.  So the two drivers of one port agree on what
 * the port is by construction.
 *
 * And they take it over the same way: as the boot loader left it after
 * "fastboot boot" - power domain on, reset released, clocks running, the
 * PHY's PLL locked - and otherwise not at all.  The GCC and the PHY are
 * read and never written (on this SoC a read of a block that is not
 * clocked can stall the bus, so nothing is even mapped until the GCC says
 * the block is on).  A full bring-up, for a boot that did not come through
 * fastboot, is hw-net.md's step 3b and not written yet.
 *
 * Beyond that, three writes: the device half's event buffer masked (the
 * boot loader left one armed, and the core's one interrupt line carries
 * both halves), the wrapper's session override taken off (host()), and on
 * the way out the core put back in device mode (down()), so that
 * dwc3_ecm finds it the way it always has.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <sys/mman.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

#include "dwc3_ecm.h"
#include "qcom_soc.h"

/* The device half's event buffer size register; bit 31 masks it. */
#define DWC3_GEVNTSIZ0		0xc408
#define DWC3_GEVNTSIZ_INTMASK	(1u << 31)

/* dwc3_find.c, dwc3_qcom.c and qusb2.c log here, as xhci. */
struct log dwc3_log = {
	.name = "xhci",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static struct dwc3_dt dt;

/*
 * The two helpers dwc3_ecm.c gives those files, the same here: a block's
 * registers mapped once and kept, and a clock's state as the GCC says it.
 */
volatile uint8_t *
dwc3_map(u64_t pa, u64_t size, const char *what)
{
	void *v;

	if ((v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)size)) ==
	    MAP_FAILED) {
		log_warn(&xhci_log, "cannot map the %s at 0x%llx; not granted "
		    "by RS?\n", what, (unsigned long long)pa);
		return NULL;
	}
	return v;
}

int
dwc3_clk_state(const struct dwc3_clk *c, char *buf, size_t size)
{
	const char *name;
	uint32_t v = 0;
	int st;

	st = qsoc_clk_on(c->id, &v, &name);
	if (st == 1 || st == 0) {
		snprintf(buf, size, "%s %s", c->name, st ? "on" : "off");
		return st;
	}
	snprintf(buf, size, "%s ?(%u)", c->name, c->id);
	return -1;
}

static int
qcom_find(const void *dtb, struct xhci_devinfo *info)
{
	int r;

	dwc3_log.log_level = xhci_log.log_level;
	if ((r = dwc3_find(dtb, &dt)) != OK) {
		log_warn(&xhci_log, "the wrapper of %s does not read: %d\n",
		    info->node, r);
		return ENXIO;
	}
	if ((phys_bytes)dt.core_pa != info->base) {
		log_warn(&xhci_log, "the qcom,dwc3 wrapper holds the core at "
		    "0x%llx, not this one\n", (unsigned long long)dt.core_pa);
		return ENXIO;
	}
	if ((r = qsoc_init_gcc(dtb)) != OK) {
		log_warn(&xhci_log, "no SDM660 clock controller (GCC) in the "
		    "tree, or not granted: %d\n", r);
		return ENXIO;
	}
	if (qsoc_gcc_phandle() != dt.gcc_ref) {
		log_warn(&xhci_log, "clocks and resets name phandle %u, the GCC "
		    "is %u: not trusting them\n", dt.gcc_ref,
		    qsoc_gcc_phandle());
		return EINVAL;
	}
	log_info(&xhci_log, "%s (%s): dr_mode %s, phy %s\n", dt.glue_node,
	    dt.glue_compat, info->dr_mode[0] ? info->dr_mode : "-",
	    dt.phy_compat);
	return OK;
}

static int
qcom_up(void)
{
	volatile uint8_t *core = NULL;
	int rg, rp;

	rg = dt.glue->report(&dt, &core);
	rp = dt.phy->report(&dt);
	if (rg != OK || rp != OK) {
		log_warn(&xhci_log, "not taking the USB block over: %s is not "
		    "running as the boot loader leaves it after fastboot boot\n",
		    rg != OK ? "the wrapper or core" : "the PHY");
		return ENXIO;
	}
	xhci.regs = (vir_bytes)core;

	/* No more events into the boot loader's device-mode buffer. */
	xhci_wr(xhci.regs, DWC3_GEVNTSIZ0,
	    xhci_rd(xhci.regs, DWC3_GEVNTSIZ0) | DWC3_GEVNTSIZ_INTMASK);

	log_info(&xhci_log, "taking over the USB block the boot loader left "
	    "running; GCC and PHY untouched\n");
	return OK;
}

static void
qcom_host(void)
{
	if (dt.glue->host != NULL)
		dt.glue->host();
}

/* The port is a device the rest of the time: leave the core as one. */
static void
qcom_down(void)
{
	xhci_dwc3_set_mode(DWC3_GCTL_PRTCAPDIR_DEVICE);
}

const struct xhci_glue xhci_qcom_glue = {
	.compatible	= "qcom,dwc3",
	.find		= qcom_find,
	.up		= qcom_up,
	.host		= qcom_host,
	.down		= qcom_down,
};
