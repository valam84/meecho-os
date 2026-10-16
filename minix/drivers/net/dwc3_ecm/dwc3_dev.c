/*
 * The DWC3 core in device mode (dwc3_dev.h): taken over, started, and its
 * events read.  Sequence and fields: Linux drivers/usb/dwc3/core.c
 * (dwc3_core_soft_reset), gadget.c (__dwc3_gadget_start,
 * dwc3_gadget_run_stop, the reset and connect-done interrupts) and ep0.c,
 * for a DWC3 3.00a (port/clover/hw-net.md, §3 and step 3a).
 *
 * The take-over starts from a core a boot loader used and stopped: its
 * event buffer still points into memory MEECHO hands out (on the Mi Pad 4,
 * 0x9b566000), so before anything else the core is told not to raise
 * events, then reset, and only then given a buffer of its own.  Its PHY,
 * clocks and the wrapper's clock selection are not touched.
 *
 * Events are read by polling, on every clock tick: GEVNTSIZ keeps its
 * interrupt mask, the core still writes the events, GEVNTCOUNT says how
 * many bytes of them are new.
 */

#include "dwc3_dev.h"
#include "dwc3_regs.h"

#include <minix/cachectl.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

struct dwc3_dev dwc;

#define NO_RESET_TICKS		(10 * sys_hz())	/* "the host never came" */

static void
cache(int op, void *p, size_t len)
{
	static int complained;
	int r;

	if ((r = sys_cachectl(op, p, len)) != OK && !complained) {
		log_warn(&dwc3_log, "sys_cachectl(%d): %d\n", op, r);
		complained = 1;
	}
}

void
dwc3_give(void *p, size_t len)
{
	cache(CACHE_CLEAN_INVALIDATE, p, len);
}

void
dwc3_take(void *p, size_t len)
{
	cache(CACHE_INVALIDATE, p, len);
}

/*
 * One endpoint command, waited for (dwc3_send_gadget_ep_cmd()): OK, or an
 * error with the command's status printed.  *done, when given, is DEPCMD
 * as the command left it - StartTransfer's resource index is in it.
 */
int
dwc3_ep_cmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2,
	uint32_t *done)
{
	uint32_t v = 0;
	unsigned t;

	dwc3_wr(DWC3_DEPCMDPAR0(ep), p0);
	dwc3_wr(DWC3_DEPCMDPAR1(ep), p1);
	dwc3_wr(DWC3_DEPCMDPAR2(ep), p2);
	dwc3_wr(DWC3_DEPCMD(ep), cmd | DWC3_DEPCMD_CMDACT);
	for (t = 0; t < 5000; t++) {
		v = dwc3_rd(DWC3_DEPCMD(ep));
		if (!(v & DWC3_DEPCMD_CMDACT))
			break;
		micro_delay(1);
	}
	if (done != NULL)
		*done = v;
	if (v & DWC3_DEPCMD_CMDACT) {
		log_warn(&dwc3_log, "ep%u command 0x%x: not done in 5 ms\n",
		    ep, cmd & 0xf);
		return ETIMEDOUT;
	}
	if (DWC3_DEPCMD_STATUS(v) != 0) {
		log_warn(&dwc3_log, "ep%u command 0x%x: status %u\n", ep,
		    cmd & 0xf, DWC3_DEPCMD_STATUS(v));
		return EIO;
	}
	return OK;
}

/* ep0 OUT (0) or IN (1) as a control endpoint of maxpacket mps. */
static int
ep_config(unsigned ep, uint32_t action, unsigned mps)
{
	uint32_t p0, p1;

	p0 = DWC3_DEPCFG_EP_TYPE(0) | DWC3_DEPCFG_MPS(mps) | action;
	if (ep & 1)
		p0 |= DWC3_DEPCFG_FIFO_NUMBER(ep >> 1);
	p1 = DWC3_DEPCFG_XFER_COMPLETE_EN | DWC3_DEPCFG_XFER_NOT_READY_EN |
	    DWC3_DEPCFG_EP_NUMBER(ep);
	return dwc3_ep_cmd(ep, DWC3_DEPCMD_SETEPCONFIG, p0, p1, 0, NULL);
}

static int
dma_alloc(void)
{
	phys_bytes pa;
	uint8_t *p;

	if ((p = alloc_contig(2 * 4096, AC_ALIGN4K, &pa)) == NULL) {
		log_warn(&dwc3_log, "no DMA memory\n");
		return ENOMEM;
	}
	if ((uint64_t)pa + 2 * 4096 > 0x100000000ULL) {
		log_warn(&dwc3_log, "DMA memory at 0x%llx: above 4 GiB\n",
		    (unsigned long long)pa);
		return ENOMEM;
	}
	memset(p, 0, 2 * 4096);
	dwc.dma = p;
	dwc.dma_pa = pa;
	dwc.trb = (struct dwc3_trb *)p;
	dwc.trb_pa = pa;
	dwc.setup = p + DWC3_LINE;
	dwc.setup_pa = pa + DWC3_LINE;
	dwc.buf = p + 2 * DWC3_LINE;
	dwc.buf_pa = pa + 2 * DWC3_LINE;
	dwc.ev = p + 4096;
	dwc.ev_pa = pa + 4096;
	dwc3_give(p, 2 * 4096);
	return OK;
}

int
dwc3_dev_start(volatile uint8_t *core)
{
	uint32_t v;
	unsigned t;
	int r;

	memset(&dwc, 0, sizeof(dwc));
	dwc.core = core;
	dwc.active_ep = -1;
	if ((r = dma_alloc()) != OK)
		return r;

	/* 1. No more events into the boot loader's buffer. */
	dwc3_wr(DWC3_GEVNTSIZ, dwc3_rd(DWC3_GEVNTSIZ) | DWC3_GEVNTSIZ_INTMASK);

	/* 2. The core's soft reset, with the pull-up off. */
	v = dwc3_rd(DWC3_DCTL);
	dwc3_wr(DWC3_DCTL, (v | DWC3_DCTL_CSFTRST) & ~DWC3_DCTL_RUN_STOP);
	for (t = 0; t < 1000 && (dwc3_rd(DWC3_DCTL) & DWC3_DCTL_CSFTRST); t++)
		micro_delay(10);
	if (dwc3_rd(DWC3_DCTL) & DWC3_DCTL_CSFTRST) {
		log_warn(&dwc3_log, "core reset: not done in 10 ms (DCTL "
		    "0x%08x)\n", dwc3_rd(DWC3_DCTL));
		return ETIMEDOUT;
	}
	log_info(&dwc3_log, "core reset: GCTL 0x%08x, DCFG 0x%08x, DCTL "
	    "0x%08x, GEVNTADRLO 0x%08x\n", dwc3_rd(DWC3_GCTL),
	    dwc3_rd(DWC3_DCFG), dwc3_rd(DWC3_DCTL),
	    dwc3_rd(DWC3_GEVNTADRLO));

	/* 3. Device mode; the HS PHY never suspended (the tree's quirks). */
	v = dwc3_rd(DWC3_GCTL);
	if (DWC3_GCTL_PRTCAP(v) != DWC3_GCTL_PRTCAP_DEVICE)
		dwc3_wr(DWC3_GCTL, (v & ~DWC3_GCTL_PRTCAPDIR_MASK) |
		    DWC3_GCTL_PRTCAP_DEVICE << 12);
	v = dwc3_rd(DWC3_GUSB2PHYCFG);
	if (v & (DWC3_GUSB2PHYCFG_SUSPHY | DWC3_GUSB2PHYCFG_ENBLSLPM))
		dwc3_wr(DWC3_GUSB2PHYCFG, v & ~(DWC3_GUSB2PHYCFG_SUSPHY |
		    DWC3_GUSB2PHYCFG_ENBLSLPM));

	/* 4. Our event buffer; the interrupt stays masked - we poll. */
	dwc3_wr(DWC3_GEVNTADRLO, (uint32_t)dwc.ev_pa);
	dwc3_wr(DWC3_GEVNTADRHI, (uint32_t)((uint64_t)dwc.ev_pa >> 32));
	dwc3_wr(DWC3_GEVNTSIZ, DWC3_GEVNTSIZ_INTMASK | DWC3_EVBUF_SIZE);
	dwc3_wr(DWC3_GEVNTCOUNT, dwc3_rd(DWC3_GEVNTCOUNT) & 0xfffc);

	/* 5. High speed, address 0, and no USB 2.0 LPM: the boot loader
	 * announced it, and the hub's controller fails with it. */
	v = dwc3_rd(DWC3_DCFG);
	v &= ~(DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK |
	    DWC3_DCFG_LPM_CAP);
	dwc3_wr(DWC3_DCFG, v | DWC3_DCFG_HIGHSPEED | DWC3_DCFG_IGNSTRMPP);

	/* 6. ep0 (dwc3_gadget_start_config(), __dwc3_gadget_ep_enable()). */
	if ((r = dwc3_ep_cmd(0, DWC3_DEPCMD_DEPSTARTCFG | DWC3_DEPCMD_PARAM(0),
	    0, 0, 0, NULL)) != OK)
		return r;
	for (t = 0; t < 2; t++) {
		if ((r = ep_config(t, DWC3_DEPCFG_ACTION_INIT, 512)) != OK ||
		    (r = dwc3_ep_cmd(t, DWC3_DEPCMD_SETTRANSFRESOURCE,
		    DWC3_DEPXFERCFG_NUM_XFER_RES(1), 0, 0, NULL)) != OK)
			return r;
		dwc3_wr(DWC3_DALEPENA, dwc3_rd(DWC3_DALEPENA) | (1u << t));
	}
	dwc3_ep0_setup_start();

	/* 7. The events wanted (dwc3_gadget_enable_irq(), 3.00a), and run. */
	dwc3_wr(DWC3_DEVTEN, DWC3_DEVTEN_EVNTOVERFLOWEN |
	    DWC3_DEVTEN_CMDCMPLTEN | DWC3_DEVTEN_ERRTICERREN |
	    DWC3_DEVTEN_WKUPEVTEN | DWC3_DEVTEN_CONNECTDONEEN |
	    DWC3_DEVTEN_USBRSTEN | DWC3_DEVTEN_DISCONNEVTEN |
	    DWC3_DEVTEN_U3L2L1SUSPEN);
	v = dwc3_rd(DWC3_DCTL) & ~(DWC3_DCTL_KEEP_CONNECT |
	    DWC3_DCTL_TSTCTRL_MASK);
	dwc3_wr(DWC3_DCTL, v | DWC3_DCTL_RUN_STOP);
	for (t = 0; t < 500 && (dwc3_rd(DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT);
	    t++)
		micro_delay(1000);
	if (dwc3_rd(DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) {
		log_warn(&dwc3_log, "run: still halted after 500 ms (DSTS "
		    "0x%08x)\n", dwc3_rd(DWC3_DSTS));
		return ETIMEDOUT;
	}
	log_info(&dwc3_log, "run: events at 0x%llx, DCFG 0x%08x, DCTL "
	    "0x%08x, DSTS 0x%08x; waiting for the host\n",
	    (unsigned long long)dwc.ev_pa, dwc3_rd(DWC3_DCFG),
	    dwc3_rd(DWC3_DCTL), dwc3_rd(DWC3_DSTS));
	return OK;
}

/*===========================================================================*
 *    events                                                                  *
 *===========================================================================*/
static void
device_event(unsigned type, unsigned info)
{
	uint32_t v;
	unsigned speed;

	switch (type) {
	case DWC3_DEVT_RESET:
		/* dwc3_gadget_reset_interrupt(), as far as ep0 goes. */
		dwc.resets++;
		dwc3_ep0_reset();
		v = dwc3_rd(DWC3_DCTL) & ~DWC3_DCTL_TSTCTRL_MASK;
		dwc3_wr(DWC3_DCTL, v);
		dwc3_wr(DWC3_DCFG, dwc3_rd(DWC3_DCFG) & ~DWC3_DCFG_DEVADDR_MASK);
		dwc.address = dwc.config = 0;
		dwc.connected = 1;
		if (dwc.resets <= 3)
			log_info(&dwc3_log, "reset by the host\n");
		break;
	case DWC3_DEVT_CONNECT_DONE:
		/* ep0's maxpacket for the speed (64 at high and full). */
		speed = dwc3_rd(DWC3_DSTS) & DWC3_DSTS_CONNECTSPD;
		(void)ep_config(0, DWC3_DEPCFG_ACTION_MODIFY, 64);
		(void)ep_config(1, DWC3_DEPCFG_ACTION_MODIFY, 64);
		if (dwc.resets <= 3)
			log_info(&dwc3_log, "connect: %s\n", speed == 0 ?
			    "high-speed" : speed == 1 ? "full-speed" :
			    "unknown speed");
		break;
	case DWC3_DEVT_DISCONNECT:
		dwc3_ep0_reset();
		dwc.connected = 0;
		log_info(&dwc3_log, "disconnect\n");
		break;
	case DWC3_DEVT_OVERFLOW:
		log_warn(&dwc3_log, "event buffer overflow\n");
		break;
	case DWC3_DEVT_ERRATIC:
		log_warn(&dwc3_log, "erratic error\n");
		break;
	default:
		log_debug(&dwc3_log, "device event %u (0x%03x)\n", type, info);
	}
}

static void
event(uint32_t e)
{
	unsigned ep;

	if (DWC3_EV_IS_DEV(e)) {
		if (DWC3_EV_DEV_CLASS(e) == 0)
			device_event(DWC3_EV_DEV_TYPE(e), DWC3_EV_DEV_INFO(e));
		return;
	}
	ep = DWC3_EV_EP(e);
	if (ep > 1) {
		log_debug(&dwc3_log, "event 0x%08x on ep%u\n", e, ep);
		return;
	}
	switch (DWC3_EV_EP_TYPE(e)) {
	case DWC3_DEPEVT_XFERCOMPLETE:
		dwc3_ep0_xfer_complete(ep);
		break;
	case DWC3_DEPEVT_XFERNOTREADY:
		dwc3_ep0_xfer_notready(ep, DWC3_EV_EP_STATUS(e));
		break;
	default:
		break;
	}
}

void
dwc3_dev_poll(void)
{
	uint32_t count, done;

	dwc.polls++;
	count = dwc3_rd(DWC3_GEVNTCOUNT) & 0xfffc;
	if (count == 0) {
		if (dwc.resets == 0 && !dwc.said_no_reset &&
		    dwc.polls >= (unsigned long)NO_RESET_TICKS) {
			dwc.said_no_reset = 1;
			log_info(&dwc3_log, "no reset from the host in 10 s: DSTS "
			    "0x%08x, DCTL 0x%08x\n", dwc3_rd(DWC3_DSTS),
			    dwc3_rd(DWC3_DCTL));
		}
		return;
	}
	dwc3_take(dwc.ev, DWC3_EVBUF_SIZE);
	for (done = 0; done < count; done += 4) {
		event(*(volatile uint32_t *)(dwc.ev + dwc.ev_pos));
		dwc.ev_pos = (dwc.ev_pos + 4) % DWC3_EVBUF_SIZE;
	}
	dwc3_wr(DWC3_GEVNTCOUNT, count);
}
