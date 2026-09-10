/*
 * The USB host controller of the RK3566/RK3568: an xHCI 1.1 inside a
 * Synopsys DWC3, on the BIGTREETECH CB2 with a USB 2.0 hub plugged into
 * the port of the first one.
 *
 * This file is the controller and the driver's own life.  At this
 * milestone it does what can be proved in one boot and no more: find the
 * part in the device tree, bring up everything the SoC puts in front of it
 * - power domain, clocks, PHY, DWC3 wrapper - and read the controller's
 * own account of itself back out.  Two numbers decide whether all of that
 * worked: GSNPSID, which says a DWC3 core is alive and which revision, and
 * HCIVERSION, which says the xHCI behind it is answering.  Everything
 * below them - fifteen register writes across five blocks, two of which
 * nothing reflects - has no separate test on this hardware, exactly as the
 * GMAC's PHY identifier had none.
 *
 * What is deliberately not here yet: the rings, the contexts and the
 * command interface (milestone 10.2), enumeration (10.3), and the URB
 * layer that will let the existing usb_hub and usb_storage drivers work
 * against this (10.4).  See port/docs/10-usb-plan.md.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/sysutil.h>

#include "xhci.h"
#include "xhcireg.h"

struct xhci xhci;

struct log xhci_log = {
	.name = "xhci",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static int instance;

/*
 * The speed identifiers the specification gives by default; a controller
 * may publish its own table in the extended capabilities, and this part
 * publishes none (the protocol capability says zero speeds), so these are
 * what its PORTSC means.
 */
static const char *
speed_name(unsigned psiv)
{
	switch (psiv) {
	case 0:	return "none";
	case 1: return "full";
	case 2: return "low";
	case 3: return "high";
	case 4: return "super";
	case 5: return "super+";
	default: return "?";
	}
}

/*
 * Read the controller's account of itself.  Three of these numbers change
 * how the rings of the next milestone have to be laid out, and two of them
 * are silent when they are got wrong - so they are read once, here, and
 * printed, rather than being assumed anywhere later.
 */
static void
read_capabilities(void)
{
	uint32_t cap, hcs1, hcs2, hcc1;

	cap = xhci_rd(xhci.regs, XHCI_CAPLENGTH);
	xhci.caplength = cap & 0xff;
	xhci.hciversion = (cap >> 16) & 0xffff;

	hcs1 = xhci_rd(xhci.regs, XHCI_HCSPARAMS1);
	hcs2 = xhci_rd(xhci.regs, XHCI_HCSPARAMS2);
	hcc1 = xhci_rd(xhci.regs, XHCI_HCCPARAMS1);

	xhci.nslots = XHCI_HCS1_MAXSLOTS(hcs1);
	xhci.nintrs = XHCI_HCS1_MAXINTRS(hcs1);
	xhci.nports = XHCI_HCS1_MAXPORTS(hcs1);
	xhci.scratchpad_bufs = XHCI_HCS2_MAX_SCRATCHPAD(hcs2);
	xhci.ac64 = XHCI_HCC1_AC64(hcc1);
	xhci.context_size = XHCI_HCC1_CSZ(hcc1) ? 64 : 32;
	xhci.xecp = XHCI_HCC1_XECP(hcc1);

	log_info(&xhci_log, "xhci %x.%x, %u slot(s), %u port(s), "
	    "%u interrupter(s)\n", xhci.hciversion >> 8,
	    (xhci.hciversion >> 4) & 0xf, xhci.nslots, xhci.nports,
	    xhci.nintrs);

	/*
	 * The two that are silent when wrong.  A context of 64 bytes read
	 * as 32 does not fail, it reads the fields of the next endpoint;
	 * and a 32-bit address bus means every ring, context and buffer
	 * must live below 4 GiB, which on this board's two gigabytes is
	 * true by construction - but knowingly so, not by luck.
	 */
	log_info(&xhci_log, "contexts %u bytes, addressing %u-bit, "
	    "%u scratchpad buffer(s), page size %u KiB\n", xhci.context_size,
	    xhci.ac64 ? 64 : 32, xhci.scratchpad_bufs,
	    ((xhci_rd(xhci.regs, xhci.caplength + XHCI_PAGESIZE) & 0xffff)
	    << 12) / 1024);

	log_debug(&xhci_log, "caplength %u, dboff 0x%x, rtsoff 0x%x, "
	    "xecp 0x%x\n", xhci.caplength,
	    xhci_rd(xhci.regs, XHCI_DBOFF) & ~0x3u,
	    xhci_rd(xhci.regs, XHCI_RTSOFF) & ~0x1fu, xhci.xecp);
}

/*
 * The extended capabilities, where the map of port to protocol lives - and
 * only there.  This is what says that the controller with the hub on it
 * has no SuperSpeed port at all, which is why SuperSpeed is a milestone of
 * its own on the other controller rather than a detail of this one.
 */
static void
read_protocols(void)
{
	unsigned off = xhci.xecp, guard;
	uint32_t v;

	for (guard = 0; off != 0 && guard < 32; guard++) {
		v = xhci_rd(xhci.regs, off);

		if (XHCI_ECP_ID(v) == XHCI_ECP_ID_PROTOCOL) {
			uint32_t ports = xhci_rd(xhci.regs, off + 8);
			unsigned count = XHCI_ECP_PORT_COUNT(ports);

			if (count == 0)
				log_debug(&xhci_log, "usb %u.%02u: no ports\n",
				    XHCI_ECP_PROTO_MAJOR(v),
				    XHCI_ECP_PROTO_MINOR(v));
			else
				log_info(&xhci_log, "usb %u.%02u on port(s) "
				    "%u..%u\n", XHCI_ECP_PROTO_MAJOR(v),
				    XHCI_ECP_PROTO_MINOR(v),
				    XHCI_ECP_PORT_OFF(ports),
				    XHCI_ECP_PORT_OFF(ports) + count - 1);
		}

		if (XHCI_ECP_NEXT(v) == 0)
			break;
		off += XHCI_ECP_NEXT(v);
	}
}

/*
 * What the ports say right now.
 *
 * This is not yet the port handling of milestone 10.2 - nothing is reset,
 * enabled or enumerated here - it is the one observation that says whether
 * everything underneath worked: a device is attached, at a speed, on a
 * port.  On the board the answer to compare against was read out of the
 * running vendor system with the hub awake: PORTSC1 = 0x00000e03, which is
 * connected, enabled, U0, high speed.
 */
static void
read_ports(void)
{
	unsigned p;
	uint32_t v;

	for (p = 0; p < xhci.nports; p++) {
		v = xhci_rd(xhci.regs, xhci.caplength + XHCI_PORTSC(p));

		log_info(&xhci_log, "port %u: 0x%08x  %s, %s, link state %u, "
		    "%s speed%s\n", p + 1, v,
		    (v & XHCI_PORTSC_CCS) ? "device attached" : "empty",
		    (v & XHCI_PORTSC_PED) ? "enabled" : "not enabled",
		    XHCI_PORTSC_PLS(v), speed_name(XHCI_PORTSC_SPEED(v)),
		    (v & XHCI_PORTSC_PP) ? "" : ", no port power");
	}
}

static int
xhci_init(int type, sef_init_info_t *info)
{
	int r;

	(void)type;
	(void)info;

	if ((r = xhci_find(&xhci.info, instance)) != OK)
		return r;

	if ((r = xhci_rk_map()) != OK)
		return r;

	if ((r = xhci_rk_power_domain()) != OK)
		return r;

	xhci_rk_clocks_on();
	xhci_rk_phy_init();

	if ((r = xhci_rk_dwc3_init()) != OK)
		return r;

	read_capabilities();

	/*
	 * A controller that is not ready says so, and saying it beats
	 * reading every register below as a plausible zero.
	 */
	if (xhci_rd(xhci.regs, xhci.caplength + XHCI_USBSTS) &
	    XHCI_USBSTS_CNR) {
		log_warn(&xhci_log, "the controller reports itself not "
		    "ready (USBSTS 0x%08x)\n",
		    xhci_rd(xhci.regs, xhci.caplength + XHCI_USBSTS));
		return EIO;
	}

	read_protocols();
	read_ports();
	xhci_rk_report();

	log_info(&xhci_log, "up; rings, enumeration and the URB layer are "
	    "milestones 10.2 to 10.4\n");

	return OK;
}

static void
xhci_startup(void)
{
	sef_setcb_init_fresh(xhci_init);
	sef_startup();
}

int
main(int argc, char *argv[])
{
	message m;
	int ipc_status, r;
	long v;

	env_setargs(argc, argv);

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		xhci_log.log_level = (int)v;
	if (env_parse("instance", "d", 0, &v, 0, 3) == EP_SET)
		instance = (int)v;

	xhci_startup();

	/*
	 * Nobody talks to this service yet: the interface its clients will
	 * use - URBs, by way of the layer the existing usb_hub and
	 * usb_storage already speak - arrives with milestone 10.4.  Until
	 * then the loop exists so that the service stays alive after
	 * bringing the controller up, and so that the interrupt this part
	 * raises has somewhere to be delivered when 10.2 asks for it.
	 */
	for (;;) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK)
			panic("sef_receive failed: %d", r);

		if (is_ipc_notify(ipc_status)) {
			if (_ENDPOINT_P(m.m_source) == HARDWARE)
				log_debug(&xhci_log, "interrupt\n");
			continue;
		}

		log_debug(&xhci_log, "unexpected message 0x%x from %d\n",
		    m.m_type, m.m_source);
	}

	return 0;
}
