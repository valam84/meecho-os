/*
 * The Synopsys DWC3 core around the xHCI, whatever SoC wraps it.
 *
 * Host-only, which shortens this considerably: the device side's own soft
 * reset is for the gadget half, and Linux skips it in host mode on the
 * grounds that the xHCI driver resets the host block itself - which is
 * what xhci.c does with USBCMD.HCRST.  What is left is the mode change,
 * which the core requires be made with its soft reset held, and the
 * description of the HS PHY interface - which is the tree's: Linux's
 * dwc3_hsphy_mode_setup() and the quirks it reads from the core's node.
 * On the CB2 that is phy_type = "utmi_wide" and three quirks; on the
 * Mi Pad 4 no phy_type (the boot loader's 8-bit interface stays) and two.
 */

#include <minix/drivers.h>

#include "xhci.h"
#include "xhcireg.h"

#define DWC3_USBTRDTIM_SHIFT	10
#define DWC3_USBTRDTIM_MASK	(0xfu << DWC3_USBTRDTIM_SHIFT)
#define DWC3_USBTRDTIM_UTMI_8	9
#define DWC3_USBTRDTIM_UTMI_16	5

/*
 * Hold the core in soft reset for a mode change, change it, let go.  The
 * documentation asks for 100 ms before it is let go, and this is one of
 * those waits that only shows up as an intermittent failure if it is
 * shortened.  Linux (__dwc3_set_mode()) does the same on a role switch.
 */
static uint32_t
mode_change(unsigned prtcapdir)
{
	uint32_t reg;

	reg = xhci_rd(xhci.regs, DWC3_GCTL);
	reg |= DWC3_GCTL_CORESOFTRESET;
	xhci_wr(xhci.regs, DWC3_GCTL, reg);

	micro_delay(100000);

	reg = xhci_rd(xhci.regs, DWC3_GCTL);
	reg &= ~DWC3_GCTL_CORESOFTRESET;
	reg &= ~(DWC3_GCTL_PRTCAPDIR_MASK << DWC3_GCTL_PRTCAPDIR_SHIFT);
	reg |= prtcapdir << DWC3_GCTL_PRTCAPDIR_SHIFT;
	return reg;
}

static void
wait_out_of_reset(void)
{
	unsigned spins;

	for (spins = 0; spins < 1000; spins++) {
		if (!(xhci_rd(xhci.regs, DWC3_GCTL) & DWC3_GCTL_CORESOFTRESET))
			break;
		micro_delay(100);
	}
}

int
xhci_dwc3_init(void)
{
	uint32_t reg, id;

	id = xhci_rd(xhci.regs, DWC3_GSNPSID);
	xhci.dwc3_id = id;

	if ((id & DWC3_GSNPSID_MASK) != DWC3_GSNPSID_DWC3) {
		log_warn(&xhci_log, "no DWC3 core answers at 0x%lx: "
		    "GSNPSID 0x%08x\n", (unsigned long)xhci.info.base, id);
		return ENXIO;
	}

	log_info(&xhci_log, "dwc3 core %x.%02x (GSNPSID 0x%08x)\n",
	    (id >> 12) & 0xf, id & 0xfff, id);

	reg = mode_change(DWC3_GCTL_PRTCAPDIR_HOST);
	reg &= ~DWC3_GCTL_SCALEDOWN_MASK;
	reg &= ~DWC3_GCTL_DISSCRAMBLE;
	xhci_wr(xhci.regs, DWC3_GCTL, reg);

	/*
	 * The HS PHY interface, as wide as the tree says, and a turnaround
	 * time that goes with the width; no phy_type, and the field is left
	 * as whoever came before set it.  Then the suspend-related bits the
	 * quirk list asks to be clear - the core may set them again once it
	 * is up, and on the CB2's other controller the vendor system does.
	 */
	reg = xhci_rd(xhci.regs, DWC3_GUSB2PHYCFG0);
	if (xhci.info.phy_type == XHCI_PHY_UTMI_WIDE) {
		reg |= DWC3_GUSB2PHYCFG_PHYIF;
		reg &= ~DWC3_USBTRDTIM_MASK;
		reg |= DWC3_USBTRDTIM_UTMI_16 << DWC3_USBTRDTIM_SHIFT;
	} else if (xhci.info.phy_type == XHCI_PHY_UTMI) {
		reg &= ~DWC3_GUSB2PHYCFG_PHYIF;
		reg &= ~DWC3_USBTRDTIM_MASK;
		reg |= DWC3_USBTRDTIM_UTMI_8 << DWC3_USBTRDTIM_SHIFT;
	}
	if (xhci.info.quirks & XHCI_Q_DIS_U2_SUSPHY)
		reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
	if (xhci.info.quirks & XHCI_Q_DIS_ENBLSLPM)
		reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
	if (xhci.info.quirks & XHCI_Q_DIS_U2_FREECLK)
		reg &= ~DWC3_GUSB2PHYCFG_U2FREECLK_EXISTS;
	xhci_wr(xhci.regs, DWC3_GUSB2PHYCFG0, reg);

	/* And wait for the core to say it is out of reset. */
	wait_out_of_reset();

	log_info(&xhci_log, "dwc3 in host mode: GCTL 0x%08x, "
	    "GUSB2PHYCFG 0x%08x, GSTS 0x%08x\n",
	    xhci_rd(xhci.regs, DWC3_GCTL),
	    xhci_rd(xhci.regs, DWC3_GUSB2PHYCFG0),
	    xhci_rd(xhci.regs, DWC3_GSTS));

	return OK;
}

/*
 * Put the core in another mode - device, when this driver leaves a port
 * that is a device the rest of the time (the Mi Pad 4's: the network to
 * the hub).  Whoever drives it next then finds it the way it was found,
 * rather than in host mode with a device-side reset ahead of it.
 */
void
xhci_dwc3_set_mode(unsigned prtcapdir)
{
	xhci_wr(xhci.regs, DWC3_GCTL, mode_change(prtcapdir));
	wait_out_of_reset();
	log_info(&xhci_log, "dwc3 left in %s mode: GCTL 0x%08x\n",
	    prtcapdir == DWC3_GCTL_PRTCAPDIR_DEVICE ? "device" : "host",
	    xhci_rd(xhci.regs, DWC3_GCTL));
}
