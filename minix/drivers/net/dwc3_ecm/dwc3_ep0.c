/*
 * Control transfers on ep0 (dwc3_dev.h), the way Linux's
 * drivers/usb/dwc3/ep0.c runs them on this core:
 *
 *   SETUP   one TRB of 8 bytes on ep0 OUT; its XferComplete brings the
 *           request.  A request with a data phase to the host gets its
 *           DATA TRB on ep0 IN at once; one with none waits.
 *   STATUS  started only when the core says XferNotReady(status), on the
 *           endpoint that event names, as STATUS2 (no data phase) or
 *           STATUS3; its XferComplete brings the next SETUP.
 *   STALL   anything not understood: ep0 stalled, SETUP started again.
 *
 * One TRB serves both directions, each phase in turn (Linux's ep0_trb).
 * The requests are chapter 9's standard ones; what the device is - its
 * descriptors, its configuration and interfaces, the class requests - is
 * the function's, ecm.c.
 */

#include "dwc3_dev.h"
#include "dwc3_regs.h"

#include <minix/sysutil.h>

#define USB_DIR_IN		0x80
#define USB_TYPE_MASK		0x60
#define USB_TYPE_CLASS		0x20
#define USB_RECIP_MASK		0x1f
#define USB_RECIP_DEVICE	0
#define USB_REQ_GET_STATUS	0
#define USB_REQ_CLEAR_FEATURE	1
#define USB_REQ_SET_FEATURE	3
#define USB_REQ_SET_ADDRESS	5
#define USB_REQ_GET_DESCRIPTOR	6
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE	10
#define USB_REQ_SET_INTERFACE	11
#define USB_FEATURE_TEST_MODE	2

/* One TRB, a phase of ep0, started on endpoint ep (0 OUT, 1 IN). */
static int
ep0_start(unsigned ep, phys_bytes pa, uint32_t len, uint32_t type)
{
	struct dwc3_trb *t = dwc.trb;
	uint32_t done;
	int r;

	t->bpl = (uint32_t)pa;
	t->bph = (uint32_t)((uint64_t)pa >> 32);
	t->size = len & DWC3_TRB_SIZE_MASK;
	t->ctrl = type | DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_ISP_IMI |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_LST;
	dwc3_give(t, DWC3_LINE);
	if ((r = dwc3_ep_cmd(ep, DWC3_DEPCMD_STARTTRANSFER,
	    (uint32_t)((uint64_t)dwc.trb_pa >> 32), (uint32_t)dwc.trb_pa, 0,
	    &done)) != OK)
		return r;
	dwc.active_ep = (int)ep;
	dwc.rsc[ep] = DWC3_DEPCMD_RSC_IDX(done);
	return OK;
}

void
dwc3_ep0_setup_start(void)
{
	dwc.ep0 = EP0_SETUP;
	dwc3_give(dwc.setup, DWC3_LINE);
	if (ep0_start(0, dwc.setup_pa, 8, DWC3_TRBCTL_SETUP) != OK)
		log_warn(&dwc3_log, "ep0: no SETUP transfer\n");
}

static void
end_transfer(unsigned ep)
{
	(void)dwc3_ep_cmd(ep, DWC3_DEPCMD_ENDTRANSFER |
	    DWC3_DEPCMD_PARAM(dwc.rsc[ep]), 0, 0, 0, NULL);
	micro_delay(100);	/* Linux: an EndTransfer without IOC */
	dwc.active_ep = -1;
}

/* dwc3_ep0_stall_and_restart(). */
static void
stall_restart(void)
{
	(void)dwc3_ep_cmd(0, DWC3_DEPCMD_SETSTALL, 0, 0, 0, NULL);
	dwc.stalls++;
	dwc.active_ep = -1;
	dwc3_ep0_setup_start();
}

/* dwc3_ep0_reset_state(): a bus reset or disconnect mid-transfer. */
void
dwc3_ep0_reset(void)
{
	if (dwc.ep0 == EP0_SETUP)
		return;			/* the SETUP TRB stays */
	if (dwc.active_ep >= 0)
		end_transfer((unsigned)dwc.active_ep);
	stall_restart();
}

/*
 * A request: the number of bytes for the host in the data buffer (0 for
 * none), or -1 to stall.  Class requests without a data phase go to the
 * function; vendor requests and class requests with data are stalled.
 */
static int
request(unsigned type, unsigned req, unsigned value, unsigned index,
	unsigned len)
{
	uint8_t *b = dwc.buf;
	int r;

	if ((type & USB_TYPE_MASK) == USB_TYPE_CLASS && len == 0)
		return ecm_class_request(type, req, value, index);
	if ((type & USB_TYPE_MASK) != 0)
		return -1;
	switch (req) {
	case USB_REQ_GET_STATUS:
		/* The device is self-powered; nothing else has status. */
		b[0] = (type & USB_RECIP_MASK) == USB_RECIP_DEVICE ? 1 : 0;
		b[1] = 0;
		return 2;
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		if ((type & USB_RECIP_MASK) == USB_RECIP_DEVICE &&
		    value == USB_FEATURE_TEST_MODE)
			return -1;
		return 0;
	case USB_REQ_SET_ADDRESS:
		/* dwc3_ep0_set_address(): before the status phase. */
		if (value > 127 || dwc.config != 0)
			return -1;
		dwc3_wr(DWC3_DCFG, (dwc3_rd(DWC3_DCFG) &
		    ~DWC3_DCFG_DEVADDR_MASK) | DWC3_DCFG_DEVADDR(value));
		dwc.address = value;
		if (dwc.resets <= 3)
			log_info(&dwc3_log, "address %u\n", value);
		return 0;
	case USB_REQ_GET_DESCRIPTOR:
		return ecm_descriptor(value >> 8, value & 0xff, b,
		    DWC3_EP0_BUF_SIZE);
	case USB_REQ_GET_CONFIGURATION:
		b[0] = (uint8_t)dwc.config;
		return 1;
	case USB_REQ_SET_CONFIGURATION:
		/* dwc3_ep0_set_config(): from the Address state, the other
		 * endpoints' resources first. */
		if ((value & 0xff) > 1 || dwc.address == 0)
			return -1;
		if ((value & 0xff) != 0 && dwc.config == 0 &&
		    dwc3_restart_config() != OK)
			return -1;
		if (ecm_set_config(value & 0xff) != 0)
			return -1;
		dwc.config = value & 0xff;
		log_info(&dwc3_log, "configuration %u\n", dwc.config);
		return 0;
	case USB_REQ_GET_INTERFACE:
		if (dwc.config == 0 || (r = ecm_get_interface(index)) < 0)
			return -1;
		b[0] = (uint8_t)r;
		return 1;
	case USB_REQ_SET_INTERFACE:
		if (dwc.config == 0)
			return -1;
		return ecm_set_interface(index, value) == 0 ? 0 : -1;
	default:
		return -1;
	}
}

/* dwc3_ep0_inspect_setup(). */
static void
inspect_setup(void)
{
	const uint8_t *s = dwc.setup;
	unsigned type = s[0], req = s[1], value = s[2] | s[3] << 8,
	    index = s[4] | s[5] << 8, len = s[6] | s[7] << 8;
	int n;

	dwc.setups++;
	dwc.three_stage = len != 0;
	dwc.expect_in = (type & USB_DIR_IN) != 0;
	n = request(type, req, value, index, len);
	if (n < 0 || (dwc.three_stage && !dwc.expect_in)) {
		if (dwc.stalls < 8)
			log_info(&dwc3_log, "ep0: stalled %02x %02x %04x %04x "
			    "%u\n", type, req, value, index, len);
		stall_restart();
		return;
	}
	log_debug(&dwc3_log, "ep0: %02x %02x %04x %04x %u -> %d\n", type, req,
	    value, index, len, n);
	if (!dwc.three_stage) {
		dwc.ep0 = EP0_WAIT_STATUS;
		return;
	}
	if ((unsigned)n > len)
		n = (int)len;
	if ((unsigned)n < len && n % 64 == 0)
		log_warn(&dwc3_log, "ep0: %d bytes short of %u would need a "
		    "zero-length packet\n", n, len);
	dwc3_give(dwc.buf, DWC3_EP0_BUF_SIZE);
	dwc.ep0 = EP0_DATA;
	if (ep0_start(1, dwc.buf_pa, (uint32_t)n, DWC3_TRBCTL_DATA) != OK)
		stall_restart();
}

void
dwc3_ep0_xfer_complete(unsigned ep)
{
	dwc3_take(dwc.trb, DWC3_LINE);
	dwc.active_ep = -1;
	switch (dwc.ep0) {
	case EP0_SETUP:
		if (ep != 0)
			return;
		dwc3_take(dwc.setup, DWC3_LINE);
		inspect_setup();
		break;
	case EP0_DATA:
		dwc.ep0 = EP0_WAIT_STATUS;
		break;
	case EP0_STATUS:
		dwc3_ep0_setup_start();
		break;
	default:
		break;
	}
}

/* dwc3_ep0_xfernotready(). */
void
dwc3_ep0_xfer_notready(unsigned ep, unsigned status)
{
	switch (status & 3) {
	case DWC3_DEPEVT_CONTROL_DATA:
		/* The data phase asked for the other way round. */
		if (dwc.ep0 == EP0_DATA && ep != (dwc.expect_in ? 1u : 0u)) {
			log_info(&dwc3_log, "ep0: data phase on ep%u, not ep%u\n",
			    ep, dwc.expect_in ? 1 : 0);
			if (dwc.active_ep >= 0)
				end_transfer((unsigned)dwc.active_ep);
			stall_restart();
		}
		break;
	case DWC3_DEPEVT_CONTROL_STATUS:
		if (dwc.ep0 != EP0_WAIT_STATUS)
			return;
		dwc.ep0 = EP0_STATUS;
		if (ep0_start(ep, dwc.setup_pa, 0, dwc.three_stage ?
		    DWC3_TRBCTL_STATUS3 : DWC3_TRBCTL_STATUS2) != OK)
			stall_restart();
		break;
	default:
		break;
	}
}
