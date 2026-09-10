/*
 * Enumeration: giving a device a slot, an address and a voice.
 *
 * This is where xHCI stops resembling the controllers the rest of this
 * tree drives.  There is no SET_ADDRESS on the wire written by the driver
 * and no register holding the device's address: the driver builds a
 * description of the device in memory - an input context - and hands it to
 * the controller with one command, and the controller does the addressing
 * itself.  Which is also why the old MINIX HCD interface could not have
 * been used here: it keeps an array of addresses and hands them out.
 *
 * The shape of it:
 *
 *   Enable Slot          the controller answers with a slot number
 *   input context        the driver describes the device: how fast it is,
 *                        which root hub port it hangs off, and where the
 *                        ring for its control endpoint lives
 *   Address Device       the controller reads that description, addresses
 *                        the device and fills in a device context of its
 *                        own, which the driver never writes again
 *   control transfers    three TRBs on the endpoint ring - setup, data,
 *                        status - and one event when they are done
 *
 * Two numbers here are silent when wrong, and both were read off the board
 * first: contexts are 64 bytes on this part, not 32, so every offset in
 * the input context is doubled from what the specification's figures show;
 * and the addresses in it are 32-bit, because this controller has no upper
 * half to its address registers at all.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>

#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

/*
 * A word of a context, by context index and word.  The stride is the
 * controller's context size rather than sizeof(anything): reading a
 * 64-byte layout as 32 does not fail, it reads the next endpoint's
 * fields, and that is the whole reason this is a function.
 */
static uint32_t *
ctx_word(vir_bytes base, unsigned ctx, unsigned word)
{
	return (uint32_t *)(base + ctx * xhci.context_size + word * 4);
}

/*
 * The input context: an input control context, then the slot context,
 * then the endpoint contexts.  Only the first two endpoints matter here -
 * the control endpoint is number 1 in this numbering, which is why the
 * slot context is context 1 and the control endpoint is context 2.
 */
#define IN_CTRL		0
#define IN_SLOT		1
#define IN_EP0		2

static void
build_input_context(struct xhci_device *dev)
{
	uint32_t *w;

	memset((void *)dev->in_ctx, 0, XHCI_PAGE);

	/* Add the slot and the control endpoint; drop nothing. */
	*ctx_word(dev->in_ctx, IN_CTRL, 0) = 0;
	*ctx_word(dev->in_ctx, IN_CTRL, 1) =
	    XHCI_INPUT_ADD_SLOT | XHCI_INPUT_ADD_EP0;

	/*
	 * The slot context.  The route string is zero because this device
	 * is on a root hub port rather than behind a hub; the day something
	 * is behind the hub, this is the field that says where.  "Context
	 * entries" is the index of the last endpoint context that means
	 * anything, which for a device that only has its control endpoint
	 * is 1.
	 */
	w = ctx_word(dev->in_ctx, IN_SLOT, 0);
	w[0] = XHCI_SLOT_ROUTE(0) | XHCI_SLOT_SPEED(dev->speed) |
	    XHCI_SLOT_ENTRIES(1);
	w[1] = XHCI_SLOT_RHPORT(dev->port + 1);

	/*
	 * The control endpoint.  Three errors before it gives up, the type
	 * that says control, and the packet size the device's speed
	 * implies: a high-speed device is required to use 64, and a
	 * full-speed one may use anything from 8 to 64 - so it gets 8,
	 * which is the one size every device must understand, and the
	 * descriptor it answers with says what it really wants.
	 */
	w = ctx_word(dev->in_ctx, IN_EP0, 0);
	w[1] = XHCI_EP_CERR(3) | XHCI_EP_TYPE(XHCI_EP_TYPE_CONTROL) |
	    XHCI_EP_MAXPACKET(dev->max_packet0);
	w[2] = (uint32_t)dev->ep0.p | XHCI_EP_DCS;
	w[3] = 0;
	w[4] = XHCI_EP_AVG_TRB(8);

	xhci_cache(CACHE_CLEAN, (void *)dev->in_ctx, XHCI_PAGE,
	    "an input context");
}

/*
 * One control transfer, which is three entries on the endpoint's ring and
 * one event at the end of them.
 *
 * Only the last of the three asks for an event: the point of a transfer
 * descriptor is that the controller runs it through and reports once.  The
 * data stage is optional and this driver only ever reads, so the
 * directions are fixed - IN data, OUT status - and a request with no data
 * is not needed until something is configured, which is 10.4.
 */
int
xhci_control(struct xhci_device *dev, uint8_t request_type, uint8_t request,
	uint16_t value, uint16_t index, uint16_t length, unsigned *actual)
{
	struct xhci_trb ev;
	unsigned cc, residue;
	int dir_in = (request_type & USB_REQ_DIR_IN) != 0;

	if (length > XHCI_PAGE)
		return EINVAL;

	if (actual != NULL)
		*actual = 0;

	/*
	 * The buffer is about to be handed to the controller either way.
	 * On the way in, the driver's zeroes must not be left dirty in the
	 * cache to fall on top of what arrives; on the way out, what the
	 * caller put there has to reach memory before the controller reads
	 * it.  Clean-invalidate does for both.
	 */
	if (dir_in)
		memset((void *)dev->buf, 0, length);
	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)dev->buf, XHCI_PAGE,
	    "a transfer buffer");

	/* Setup stage: the eight bytes of the request, carried in the TRB. */
	xhci_ring_push(&dev->ep0,
	    (uint32_t)request_type | ((uint32_t)request << 8) |
	    ((uint32_t)value << 16),
	    (uint32_t)index | ((uint32_t)length << 16),
	    8, XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT |
	    (length == 0 ? XHCI_TRB_TRT_NONE :
	    (dir_in ? XHCI_TRB_TRT_IN : XHCI_TRB_TRT_OUT)));

	/* Data stage, when there is data. */
	if (length != 0)
		xhci_ring_push(&dev->ep0, (uint32_t)dev->buf_phys, 0, length,
		    XHCI_TRB_TYPE(XHCI_TRB_DATA) |
		    (dir_in ? XHCI_TRB_DIR_IN : 0));

	/*
	 * Status stage, which goes the other way from the data and is the
	 * one that asks for the event.  With no data at all it goes in.
	 */
	xhci_ring_push(&dev->ep0, 0, 0, 0,
	    XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC |
	    (length != 0 && dir_in ? 0 : XHCI_TRB_DIR_IN));

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev->slot), XHCI_DB_EP0);

	memset(&ev, 0, sizeof(ev));
	if (xhci_events_drain(500000, &ev, XHCI_TRB_TRANSFER_EVENT) == 0 ||
	    XHCI_TRB_TYPE_OF(ev.control) != XHCI_TRB_TRANSFER_EVENT) {
		log_warn(&xhci_log, "no transfer event for request 0x%02x "
		    "0x%02x\n", request_type, request);
		return EIO;
	}

	cc = XHCI_CC_OF(ev.status);
	residue = XHCI_EVENT_LENGTH(ev.status);

	/*
	 * Short is not an error here and saying so is not laxity: a request
	 * for eighteen bytes of a descriptor that is shorter comes back
	 * short by design, and the caller checks what it got.
	 */
	if (cc != XHCI_CC_SUCCESS && cc != 13 /* short packet */) {
		log_warn(&xhci_log, "request 0x%02x 0x%02x completed with "
		    "%u\n", request_type, request, cc);
		return EIO;
	}

	/* Now the buffer is the controller's writing, so read it fresh. */
	if (dir_in)
		xhci_cache(CACHE_INVALIDATE, (void *)dev->buf, XHCI_PAGE,
		    "a transfer buffer");

	if (actual != NULL)
		*actual = length - residue;

	log_debug(&xhci_log, "request 0x%02x 0x%02x: %u of %u byte(s)\n",
	    request_type, request, length - residue, length);

	return OK;
}

/*
 * The device's one configuration, and switching it on.
 *
 * A device that has been addressed and no more can answer questions about
 * itself and nothing else: class requests - which is all a hub driver ever
 * makes - are answered only once it is configured.  So enumeration is not
 * finished until this has run, and the interface numbers it collects are
 * what the client drivers are told about the device.
 */
static int
configure(struct xhci_device *dev)
{
	uint8_t *cfg;
	unsigned total, off, actual;
	int r;

	/*
	 * Nine bytes first, because the configuration descriptor's own
	 * first field says how long the whole thing is - interfaces,
	 * endpoints and all - and asking for a fixed guess would either
	 * truncate it or ask a device for more than it has.
	 */
	if ((r = xhci_control(dev, USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
	    USB_DESC_CONFIG << 8, 0, 9, &actual)) != OK)
		return r;

	cfg = (uint8_t *)dev->buf;
	if (actual < 9 || cfg[1] != USB_DESC_CONFIG)
		return EIO;

	total = cfg[2] | ((unsigned)cfg[3] << 8);
	if (total > XHCI_PAGE)
		total = XHCI_PAGE;

	if ((r = xhci_control(dev, USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
	    USB_DESC_CONFIG << 8, 0, (uint16_t)total, &actual)) != OK)
		return r;

	cfg = (uint8_t *)dev->buf;
	dev->config = cfg[5];			/* bConfigurationValue */
	dev->interfaces = 0;

	/*
	 * Walk the descriptors that follow, which are a chain of
	 * length-and-type records, and note which interface numbers are
	 * there.  That set is what the client drivers are handed; the old
	 * stack computed the same thing the same way.
	 */
	for (off = 0; off + 2 <= actual && cfg[off] != 0; off += cfg[off]) {
		if (cfg[off + 1] == 4 && off + 3 <= actual)	/* interface */
			dev->interfaces |= 1U << (cfg[off + 2] & 0x1f);
	}

	if ((r = xhci_control(dev, 0, 9 /* SET_CONFIGURATION */, dev->config,
	    0, 0, NULL)) != OK) {
		log_warn(&xhci_log, "port %u would not take configuration "
		    "%u\n", dev->port + 1, dev->config);
		return r;
	}

	log_info(&xhci_log, "port %u: configuration %u set, interfaces "
	    "0x%x\n", dev->port + 1, dev->config, dev->interfaces);

	return OK;
}

static const char *
speed_word(unsigned psiv)
{
	switch (psiv) {
	case 1: return "full";
	case 2: return "low";
	case 3: return "high";
	case 4: return "super";
	default: return "?";
	}
}

/*
 * Give the device on this port a slot, an address and a first
 * conversation.
 */
int
xhci_device_attach(unsigned port, struct xhci_device *dev)
{
	struct usb_device_descriptor *desc;
	struct xhci_trb ev;
	uint64_t *dcbaa;
	uint32_t portsc;
	void *v;
	int r;

	memset(dev, 0, sizeof(*dev));
	dev->port = port;

	portsc = xhci_rd(xhci.regs, xhci.caplength + XHCI_PORTSC(port));
	dev->speed = XHCI_PORTSC_SPEED(portsc);

	/*
	 * The control endpoint's packet size before anything has been
	 * asked: a high-speed device must use 64, and anything slower is
	 * given 8 - the size every device understands - until its own
	 * descriptor says otherwise.
	 */
	dev->max_packet0 = (dev->speed == 3 || dev->speed >= 4) ? 64 : 8;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &dev->in_ctx_phys, "an input "
	    "context")) == NULL)
		return ENOMEM;
	dev->in_ctx = (vir_bytes)v;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &dev->dev_ctx_phys, "a device "
	    "context")) == NULL)
		return ENOMEM;
	dev->dev_ctx = (vir_bytes)v;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &dev->buf_phys, "a descriptor "
	    "buffer")) == NULL)
		return ENOMEM;
	dev->buf = (vir_bytes)v;

	if ((r = xhci_ring_setup(&dev->ep0, "a control endpoint ring")) != OK)
		return r;

	/* The device context is the controller's to write, not ours. */
	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)dev->dev_ctx, XHCI_PAGE,
	    "a device context");

	/* A slot, please. */
	if ((r = xhci_cmd(0, 0, 0, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT), &ev))
	    != OK)
		return r;

	dev->slot = XHCI_EVENT_SLOT_ID(ev.control);
	if (dev->slot == 0 || dev->slot > xhci.nslots) {
		log_warn(&xhci_log, "the controller answered with slot %u\n",
		    dev->slot);
		return EIO;
	}

	/*
	 * Where the controller is to keep this device's context.  This is
	 * the only entry of the array the driver writes after start-up, and
	 * it has to be there before the Address Device command runs.
	 */
	dcbaa = (uint64_t *)xhci.dcbaa;
	dcbaa[dev->slot] = (uint64_t)dev->dev_ctx_phys;
	xhci_cache(CACHE_CLEAN, &dcbaa[dev->slot], sizeof(dcbaa[0]),
	    "a dcbaa entry");

	build_input_context(dev);

	if ((r = xhci_cmd((uint32_t)dev->in_ctx_phys, 0, 0,
	    XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) |
	    XHCI_TRB_SLOT_ID(dev->slot), &ev)) != OK) {
		log_warn(&xhci_log, "the device on port %u would not take an "
		    "address\n", port + 1);
		return r;
	}

	log_info(&xhci_log, "port %u: slot %u, %s speed, addressed\n",
	    port + 1, dev->slot, speed_word(dev->speed));

	/*
	 * And the first question anybody asks a USB device.  Eighteen bytes
	 * is the whole device descriptor; a device that answers fewer is
	 * answering something, and the length it reports is checked rather
	 * than assumed.
	 */
	if ((r = xhci_control(dev, USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
	    USB_DESC_DEVICE << 8, 0, 18, NULL)) != OK)
		return r;

	desc = (struct usb_device_descriptor *)dev->buf;

	if (desc->bLength < 18 || desc->bDescriptorType != USB_DESC_DEVICE) {
		log_warn(&xhci_log, "port %u answered with something that is "
		    "not a device descriptor: length %u, type %u\n", port + 1,
		    desc->bLength, desc->bDescriptorType);
		return EIO;
	}

	log_info(&xhci_log, "port %u: %04x:%04x usb %x.%02x, class %u/%u/%u, "
	    "ep0 %u bytes, %u configuration(s)\n", port + 1, desc->idVendor,
	    desc->idProduct, desc->bcdUSB >> 8, desc->bcdUSB & 0xff,
	    desc->bDeviceClass, desc->bDeviceSubClass, desc->bDeviceProtocol,
	    desc->bMaxPacketSize0, desc->bNumConfigurations);

	/*
	 * What the device says its control endpoint can carry, against what
	 * it was given.  For a high-speed device the answer is required to
	 * be 64 and a difference would mean this driver misread the speed;
	 * for a slower one the difference is expected and is what an
	 * Evaluate Context command is for, which arrives with the endpoints
	 * of 10.4.
	 */
	if (desc->bMaxPacketSize0 != dev->max_packet0)
		log_warn(&xhci_log, "port %u wants %u-byte control packets, "
		    "not %u; not changed yet\n", port + 1,
		    desc->bMaxPacketSize0, dev->max_packet0);

	dev->class = desc->bDeviceClass;

	/* And the configuration, without which class requests are refused. */
	if ((r = configure(dev)) != OK)
		return r;

	return OK;
}

void
xhci_device_free(struct xhci_device *dev)
{
	if (dev->in_ctx != 0)
		free_contig((void *)dev->in_ctx, XHCI_PAGE);
	if (dev->dev_ctx != 0)
		free_contig((void *)dev->dev_ctx, XHCI_PAGE);
	if (dev->buf != 0)
		free_contig((void *)dev->buf, XHCI_PAGE);
	xhci_ring_free(&dev->ep0);
	memset(dev, 0, sizeof(*dev));
}
