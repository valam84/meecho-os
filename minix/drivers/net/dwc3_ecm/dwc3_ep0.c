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
 * The requests are chapter 9's standard ones; the descriptors make the
 * tablet a high-speed device with one configuration and one interface of
 * the vendor class and no endpoints - enough for a host to enumerate it
 * and bind nothing (port/clover/hw-net.md, step 3a).  No BOS, bcdUSB 2.00:
 * a host enables USB 2.0 LPM only for a device that offers it there.
 */

#include "dwc3_dev.h"
#include "dwc3_regs.h"

#include <minix/sysutil.h>

#define USB_DIR_IN		0x80
#define USB_TYPE_MASK		0x60
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
#define USB_DT_DEVICE		1
#define USB_DT_CONFIG		2
#define USB_DT_STRING		3
#define USB_DT_QUALIFIER	6
#define USB_FEATURE_TEST_MODE	2

/*
 * The device: USB 2.00, maxpacket 64 on ep0, 0525:a4a1 (what Linux's
 * Ethernet gadget calls itself; hosts bind CDC Ethernet by class, not by
 * this), release 1.00, manufacturer 1, product 2, no serial number.
 */
static const uint8_t dev_desc[18] = {
	18, USB_DT_DEVICE, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
	0x25, 0x05, 0xa1, 0xa4, 0x00, 0x01, 1, 2, 0, 1
};

/* Configuration 1, self-powered, 500 mA; interface 0: vendor, no ends. */
static const uint8_t cfg_desc[18] = {
	9, USB_DT_CONFIG, 18, 0, 1, 1, 0, 0xc0, 250,
	9, 4, 0, 0, 0, 0xff, 0, 0, 0
};

/* A high-speed device says what it would be at full speed. */
static const uint8_t qual_desc[10] = {
	10, USB_DT_QUALIFIER, 0x00, 0x02, 0x00, 0x00, 0x00, 64, 1, 0
};

static const char *const strings[] = {
	NULL, "MEECHO", "MEECHO USB (dwc3_ecm)"
};

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

/* A string descriptor from ASCII, in the data buffer. */
static int
string(unsigned idx)
{
	const char *s;
	unsigned i, n;

	if (idx == 0) {			/* the languages: US English */
		dwc.buf[0] = 4;
		dwc.buf[1] = USB_DT_STRING;
		dwc.buf[2] = 0x09;
		dwc.buf[3] = 0x04;
		return 4;
	}
	if (idx >= sizeof(strings) / sizeof(strings[0]))
		return -1;
	s = strings[idx];
	n = strlen(s);
	if (2 + 2 * n > DWC3_EP0_BUF_SIZE || 2 + 2 * n > 255)
		return -1;
	dwc.buf[0] = (uint8_t)(2 + 2 * n);
	dwc.buf[1] = USB_DT_STRING;
	for (i = 0; i < n; i++) {
		dwc.buf[2 + 2 * i] = (uint8_t)s[i];
		dwc.buf[3 + 2 * i] = 0;
	}
	return (int)(2 + 2 * n);
}

static int
copy(const uint8_t *d, size_t len)
{
	memcpy(dwc.buf, d, len);
	return (int)len;
}

static int
descriptor(unsigned type, unsigned idx)
{
	switch (type) {
	case USB_DT_DEVICE:
		return copy(dev_desc, sizeof(dev_desc));
	case USB_DT_CONFIG:
		return idx == 0 ? copy(cfg_desc, sizeof(cfg_desc)) : -1;
	case USB_DT_STRING:
		return string(idx);
	case USB_DT_QUALIFIER:
		return copy(qual_desc, sizeof(qual_desc));
	default:
		return -1;		/* BOS among them: no LPM offered */
	}
}

/*
 * A standard request: the number of bytes for the host in the data
 * buffer (0 for none), or -1 to stall.
 */
static int
request(unsigned type, unsigned req, unsigned value, unsigned index)
{
	uint8_t *b = dwc.buf;

	if ((type & USB_TYPE_MASK) != 0)
		return -1;		/* class and vendor: none yet */
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
		return descriptor(value >> 8, value & 0xff);
	case USB_REQ_GET_CONFIGURATION:
		b[0] = (uint8_t)dwc.config;
		return 1;
	case USB_REQ_SET_CONFIGURATION:
		if ((value & 0xff) > 1 || dwc.address == 0)
			return -1;
		dwc.config = value & 0xff;
		log_info(&dwc3_log, "configuration %u\n", dwc.config);
		return 0;
	case USB_REQ_GET_INTERFACE:
		if (dwc.config == 0 || index != 0)
			return -1;
		b[0] = 0;
		return 1;
	case USB_REQ_SET_INTERFACE:
		if (dwc.config == 0 || index != 0 || value != 0)
			return -1;
		return 0;
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
	n = request(type, req, value, index);
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
