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
 * With milestone 10.2 it also starts the controller: the structures in
 * xhci_ring.c are handed over, the command ring is exercised with a command
 * that does nothing, and the ports that can be accounted for are reset.
 *
 * What is deliberately not here yet: enumeration (10.3) and the URB layer
 * that will let the existing usb_hub and usb_storage drivers work against
 * this (10.4).  See port/docs/10-usb-plan.md.
 */

#include <minix/drivers.h>
#include <minix/ds.h>
#include <minix/sysutil.h>
#include <sys/mman.h>

#include "xhci.h"
#include "xhcireg.h"

struct xhci xhci;

struct log xhci_log = {
	.name = "xhci",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static int instance;
static int use_irq = 1;		/* "irq=0" falls back to polling */
static unsigned noops = 1;		/* how many no-op commands to issue */

/*
 * Requests that arrived while the driver was inside a transfer.
 *
 * A transfer waits for an interrupt, and while it waits the driver is in a
 * receive - so a client's next request arrives there rather than in the
 * main loop.  It cannot be answered on the spot: this driver runs one
 * transfer at a time, and starting a second from inside the first is how
 * rings get two writers.  So it is put aside and answered when the current
 * one is done, which is what a queue this shallow is for: with one thread
 * per client driver, only as many requests can be outstanding as there are
 * clients.
 */
#define XHCI_MAX_DEFER	8

static struct {
	message m;
	int ipc_status;
} deferred[XHCI_MAX_DEFER];

static unsigned ndeferred;

void
xhci_defer(message *m, int ipc_status)
{
	if (ndeferred >= XHCI_MAX_DEFER) {
		log_warn(&xhci_log, "no room to put aside a request from %d; "
		    "it is dropped\n", m->m_source);
		return;
	}

	deferred[ndeferred].m = *m;
	deferred[ndeferred].ipc_status = ipc_status;
	ndeferred++;
}

static void
run_deferred(void)
{
	message m;
	unsigned i;

	/*
	 * Taken one at a time from the front, and the queue is re-read
	 * every time round: answering one of these can put another aside.
	 */
	while (ndeferred > 0 && !xhci_urb_busy()) {
		m = deferred[0].m;

		for (i = 1; i < ndeferred; i++)
			deferred[i - 1] = deferred[i];
		ndeferred--;

		xhci_urb_message(&m);
	}
}

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

	/*
	 * Where the other three register blocks are.  Every one of them is
	 * an offset from the same base and every one is stated by the part
	 * rather than fixed by the specification, so they are read once
	 * here and used from the state everywhere else.
	 */
	xhci.rtsoff = xhci_rd(xhci.regs, XHCI_RTSOFF) & ~0x1fu;
	xhci.dboff = xhci_rd(xhci.regs, XHCI_DBOFF) & ~0x3u;

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
			unsigned first = XHCI_ECP_PORT_OFF(ports);
			unsigned major = XHCI_ECP_PROTO_MAJOR(v);
			unsigned i;

			if (count == 0)
				log_debug(&xhci_log, "usb %u.%02u: no ports\n",
				    major, XHCI_ECP_PROTO_MINOR(v));
			else
				log_info(&xhci_log, "usb %u.%02u on port(s) "
				    "%u..%u\n", major,
				    XHCI_ECP_PROTO_MINOR(v), first,
				    first + count - 1);

			/*
			 * Remember which port speaks what.  This is the only
			 * place it is written down, and this driver needs it
			 * for a reason particular to this board: the
			 * controller announces a SuperSpeed port that has no
			 * USB3 PHY behind it - the vendor system announces
			 * one port here, not two - so a port is driven only
			 * when the driver can say what it is.
			 */
			for (i = 0; i < count; i++)
				if (first + i >= 1 &&
				    first + i <= XHCI_MAX_PORTS)
					xhci.port_major[first + i - 1] =
					    (unsigned char)major;
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

/*
 * Reset the ports that have something attached and that this driver can
 * account for.
 *
 * "Can account for" is doing real work here.  On this board the
 * controller announces two ports where the vendor system announces one:
 * the second is SuperSpeed, and no USB3 PHY is wired to this controller
 * at all - the device tree gives it only a usb2-phy.  Something in the
 * DWC3 setup that this driver does not do hides that port under Linux.
 * Until that is understood, a port whose protocol nobody claimed, and a
 * SuperSpeed port on a controller with no SuperSpeed PHY, are left alone:
 * the cost of leaving one alone is a port that does nothing, and the cost
 * of driving one that is not there is a reset that never completes.
 */
static void
reset_ports(void)
{
	unsigned p;
	uint32_t v;

	for (p = 0; p < xhci.nports && p < XHCI_MAX_PORTS; p++) {
		v = xhci_rd(xhci.regs, xhci.caplength + XHCI_PORTSC(p));

		if (!(v & XHCI_PORTSC_CCS))
			continue;

		if (xhci.port_major[p] == 0) {
			log_info(&xhci_log, "port %u: no protocol named for "
			    "it; left alone\n", p + 1);
			continue;
		}
		if (xhci.port_major[p] != 2) {
			log_info(&xhci_log, "port %u: usb %u, which this "
			    "controller has no PHY for; left alone\n", p + 1,
			    xhci.port_major[p]);
			continue;
		}

		(void)xhci_port_reset(p);
	}
}

/*
 * Enumerate what is on the ports that came up enabled.
 *
 * One device per port and nothing behind it: what hangs off the hub is
 * reached by talking to the hub, and that is the hub driver's business
 * rather than this file's.  The usb_hub already in this tree does it, once
 * there is a URB layer for it to speak through - milestone 10.4.
 */
static unsigned
attach_devices(void)
{
	unsigned p, found = 0;
	uint32_t v;

	for (p = 0; p < xhci.nports && p < XHCI_MAX_PORTS; p++) {
		v = xhci_rd(xhci.regs, xhci.caplength + XHCI_PORTSC(p));

		if (!(v & XHCI_PORTSC_CCS) || !(v & XHCI_PORTSC_PED))
			continue;

		if (xhci_device_attach(p, &xhci.dev[p]) != OK) {
			log_warn(&xhci_log, "port %u: could not be "
			    "enumerated\n", p + 1);
			xhci_device_free(&xhci.dev[p]);
			continue;
		}

		xhci_urb_announce(&xhci.dev[p]);
		found++;
	}

	return found;
}

/*
 * Ask the kernel for the line the device tree named.
 *
 * A failure here is not fatal: every wait in this driver falls back to
 * polling, which is slow but correct, and a machine whose tree names no
 * line at all is served that way from the start.  What is not acceptable
 * is being quiet about it, since the difference is a factor of twenty in
 * throughput and would otherwise be discovered as "USB is slow here".
 */
static void
arm_interrupt(void)
{
	int r;

	if (!use_irq) {
		log_info(&xhci_log, "asked not to use the interrupt; every "
		    "wait will poll\n");
		return;
	}

	xhci.irq_line = xhci.info.irq;

	if (xhci.irq_line < 0) {
		log_warn(&xhci_log, "the tree names no interrupt; every "
		    "transfer will poll\n");
		return;
	}

	/*
	 * The hook identifier is not the line number: it is the bit this
	 * driver's interrupts will be reported in, so it has to fit in the
	 * notification word - the kernel refuses anything above 63.  Every
	 * driver in this tree passes the line number instead, which works
	 * only as long as the line is a small number; sdmmc's is 51 and
	 * gets away with it, this one is 201 and does not.  One is used
	 * here because this driver takes one line.
	 */
	xhci.irq_hook = 1;
	if ((r = sys_irqsetpolicy(xhci.irq_line, 0, &xhci.irq_hook)) != OK) {
		log_warn(&xhci_log, "cannot take line %d (%d); every "
		    "transfer will poll\n", xhci.irq_line, r);
		return;
	}

	xhci.irq_ok = 1;

	/*
	 * Let the controller raise it.  Two switches, and both are needed:
	 * the interrupter's own enable and the one in the command register
	 * that lets any interrupter through at all.
	 */
	xhci_wr(xhci.regs, xhci.rtsoff + XHCI_IR(0) + XHCI_IR_IMAN,
	    XHCI_IMAN_IE);
	xhci_wr(xhci.regs, xhci.caplength + XHCI_USBCMD,
	    xhci_rd(xhci.regs, xhci.caplength + XHCI_USBCMD) |
	    XHCI_USBCMD_INTE);

	/*
	 * And switch it on.  Registering a hook does not enable the line
	 * - that is a second call, and forgetting it is invisible: the
	 * controller finishes its transfers, the events land on the ring,
	 * and nothing ever wakes the driver to look at them.  Which is
	 * what the board reported, in those words.
	 */
	if ((r = sys_irqenable(&xhci.irq_hook)) != OK) {
		log_warn(&xhci_log, "cannot switch line %d on (%d)\n",
		    xhci.irq_line, r);
		xhci.irq_dead = 1;
		return;
	}

	log_info(&xhci_log, "interrupt line %d\n", xhci.irq_line);
}


/*
 * Being taken down.
 *
 * A driver of a bus-mastering device cannot simply exit: the part holds
 * physical addresses of this process's memory in its own registers and
 * keeps writing to them.  So the controller is stopped and reset first,
 * and only then is anything given back.  See xhci_halt().
 */
static void
xhci_signal(int signo)
{
	if (signo != SIGTERM)
		return;

	log_info(&xhci_log, "going down; stopping the controller first\n");

	if (xhci.irq_ok)
		(void)sys_irqdisable(&xhci.irq_hook);

	xhci_halt();
	xhci_dma_free();

	exit(0);
}

static int
xhci_init(int type, sef_init_info_t *info)
{
	int r;
	unsigned devices;

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

	/*
	 * And the structures the controller and the driver share, after
	 * which it can be started and asked to do something.
	 */
	if ((r = xhci_dma_alloc()) != OK)
		return r;

	if ((r = xhci_start()) != OK)
		return r;

	if ((r = xhci_cmd_noop()) != OK)
		return r;

	/*
	 * And the interrupt, now that there is something to be interrupted
	 * about.  It is asked for after the controller runs rather than
	 * before, so that a line raised by whatever state the part was left
	 * in does not arrive before this driver can make sense of it.
	 */
	arm_interrupt();

	/*
	 * And, when asked, enough of them to go round the ring.
	 *
	 * One no-op proves the ring is where the controller thinks it is.
	 * It does not prove the arithmetic: the ring holds 256 entries, the
	 * last of which is a Link the driver never writes, and wrapping
	 * means starting again at the beginning with the cycle bit flipped.
	 * Get that wrong and the failure is not the next command but the
	 * two-hundred-and-fifty-seventh, which is exactly the kind of thing
	 * that ships.  So the wrap is tested on the hardware rather than
	 * against a model of it: noops=300 walks past the link twice.
	 */
	if (noops > 1) {
		unsigned i, bad = 0;

		for (i = 1; i < noops; i++)
			if (xhci_cmd_noop_quiet() != OK)
				bad++;

		log_info(&xhci_log, "%u no-op commands, %u failed; the ring "
		    "wrapped %u time(s)\n", noops, bad,
		    noops / (xhci.cmd.slots - 1));
		if (bad != 0)
			return EIO;
	}

	/*
	 * Now the ports.  Only the ones whose protocol the extended
	 * capabilities named, and only USB 2 for now: this controller
	 * announces a SuperSpeed port with no USB3 PHY behind it, and
	 * resetting a port that has no PHY is asking the hardware a
	 * question about wiring that does not exist.
	 */
	reset_ports();

	/*
	 * Whatever the resets stirred up.  A port coming up is an event,
	 * and reading them here proves the ring keeps working after the
	 * first one rather than only once.
	 */
	(void)xhci_events_drain(50000, NULL, 0);

	read_ports();

	/* And what is on the ports that came up. */
	devices = attach_devices();

	log_info(&xhci_log, "up; %u device(s) enumerated, waiting for "
	    "drivers\n", devices);

	return OK;
}

static void
xhci_startup(void)
{
	sef_setcb_init_fresh(xhci_init);
	sef_setcb_signal_handler(xhci_signal);
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
	if (env_parse("noops", "d", 0, &v, 1, 100000) == EP_SET)
		noops = (unsigned)v;

	/*
	 * Where the time of a transfer goes, printed every so many
	 * requests.  Off by default: a driver that prints during a
	 * measurement is measuring its own printing.
	 */
	if (env_parse("urbstats", "d", 0, &v, 0, 100000) == EP_SET)
		xhci_urb_stats((unsigned)v);

	/*
	 * Whether to use the interrupt at all.
	 *
	 * "irq=0" makes every wait poll, which is the path milestone 10.5
	 * was measured on and the one known to read a flash drive
	 * correctly.  It is here so that the two can be compared on the
	 * same boot with the same device - a comparison that is otherwise
	 * impossible, because everything else that differs between two
	 * board runs differs at once.  Any claim about what the interrupt
	 * is worth has to be an A against a B, not a number against a
	 * memory.
	 */
	if (env_parse("irq", "d", 0, &v, 0, 1) == EP_SET)
		use_irq = (int)v;

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
			/*
			 * The controller has something to say: take
			 * everything off the event ring, which is what
			 * finishes any outstanding client transfer, and
			 * then let the line raise again.
			 */
			if (_ENDPOINT_P(m.m_source) == HARDWARE) {
				/*
				 * Acknowledge, drain, say the handler is
				 * done, re-enable: xhci_interrupt() is that
				 * sequence, kept in one place so that the
				 * stand runs the same one.
				 */
				xhci_interrupt();
				run_deferred();
				continue;
			}

			/* The deadline of an outstanding transfer. */
			if (_ENDPOINT_P(m.m_source) == CLOCK) {
				xhci_n_alarm++;
				(void)xhci_alarm_fired("the main loop");
				xhci_urb_tick();
				run_deferred();
				continue;
			}

			continue;
		}

		/*
		 * Everything else is a driver talking the URB protocol -
		 * the one usb_hub and usb_storage already speak.
		 */
		if (m.m_type >= USB_RQ_INIT && m.m_type <= USB_REPLY) {
			xhci_urb_message(&m);
			run_deferred();
		} else
			log_debug(&xhci_log, "unexpected message 0x%x from "
			    "%d\n", m.m_type, m.m_source);
	}

	return 0;
}
