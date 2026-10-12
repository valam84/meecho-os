/*
 * A keyboard, and the hub it hangs off.
 *
 * Everything the driver needed for a keyboard that it did not need for a
 * flash drive behind a hub: a full-speed device whose control packets are
 * not 8 bytes, an interrupt endpoint whose bInterval is milliseconds, a
 * transfer that waits in a buffer of its own, an endpoint brought back
 * after a halt, and a hub whose slot context says that it is one.  None of
 * it is reachable on QEMU, and every part of it is silent when wrong - so
 * each is checked here before the board sees it.
 *
 * Included by ringtest.c after async.inc, whose stand-in for the URB layer
 * (td_watch, the claims) it uses.
 */

/* A Logitech-shaped keyboard: full speed, and 64-byte control packets. */
static const uint8_t kbd_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x10, 0x01,	/* usb 1.10 */
	0x00, 0x00, 0x00, 64,			/* class 0, ep0 64 bytes */
	0x6d, 0x04, 0x1c, 0xc3,			/* 046d:c31c */
	0x00, 0x01, 1, 2, 0, 1
};

/*
 * One interface, 3/1/1 - the boot keyboard - with its HID descriptor in
 * between, and one interrupt endpoint: 8 bytes every 10 ms.
 */
static const uint8_t kbd_config_desc[34] = {
	9, USB_DESC_CONFIG, 34, 0, 1, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 3, 1, 1, 0,
	9, 0x21, 0x10, 0x01, 0, 1, 0x22, 0x41, 0,
	7, 5, 0x81, 3, 8, 0, 10
};

static size_t
kbd_control(unsigned slot, const uint8_t setup[8], uint8_t *out, size_t max)
{
	unsigned request = setup[1];
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);

	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_DEVICE) {
		memcpy(out, kbd_device_desc, sizeof(kbd_device_desc));
		return sizeof(kbd_device_desc);
	}
	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_CONFIG) {
		memcpy(out, kbd_config_desc, sizeof(kbd_config_desc));
		return sizeof(kbd_config_desc);
	}
	if (request == 9 /* SET_CONFIGURATION */)
		set_configuration_value = value;
	return 0;
}

static uint32_t
in_ctx_word(const struct xhci_device *dev, unsigned ctx, unsigned word)
{
	return *(uint32_t *)(dev->in_ctx + ctx * xhci.context_size + word * 4);
}

static void
test_keyboard(void)
{
	static const uint8_t press[8] = { 0x02, 0, 0x04, 0, 0, 0, 0, 0 };
	static const uint8_t next[8] = { 0x00, 0, 0x05, 0, 0, 0, 0, 0 };
	struct xhci_ep *ep;
	struct xhci_td td;
	phys_bytes kb_phys, abandoned;
	uint8_t *kb;
	unsigned i, untouched;

	section("a keyboard: full speed, an interrupt endpoint");

	check(bring_up(2) == OK, "the controller starts");
	model_control_handler(kbd_control);
	model_attach(0, 1 /* full speed */);
	xhci_port_reset(0);
	set_configuration_value = 0;

	check(xhci_device_attach(0, &dev0) == OK, "the keyboard enumerates");

	/*
	 * Asked for eighteen bytes with 8-byte packets, a device that
	 * wants 64 answers in one packet of eighteen: babble on the board.
	 * The model does not babble, so what is checked is that the
	 * controller was told before the long question was asked.
	 */
	check(model_ep0_max_packet(dev0.slot) == 64,
	    "the controller was told its control packets are %u bytes",
	    model_ep0_max_packet(dev0.slot));
	check(dev0.max_packet0 == 64, "and the driver knows it too");

	check(dev0.class == 0, "the device says class %u", dev0.class);
	check(dev0.niface == 1, "%u interface(s) recorded", dev0.niface);
	check(dev0.iface[0].class == 3 && dev0.iface[0].subclass == 1 &&
	    dev0.iface[0].protocol == 1,
	    "interface 0 is %u/%u/%u - a boot keyboard",
	    dev0.iface[0].class, dev0.iface[0].subclass,
	    dev0.iface[0].protocol);
	check(set_configuration_value == 1, "configuration %u was set",
	    set_configuration_value);

	ep = xhci_device_ep(&dev0, 1, 1);
	check(ep != NULL, "endpoint 1 in is there");
	if (ep == NULL)
		return;
	check(ep->type == XHCI_EP_TYPE_INTR_IN, "and it is interrupt in");
	check(ep->max_packet == 8, "of %u bytes", ep->max_packet);

	/*
	 * The context the endpoint was configured with.  bInterval 10 is
	 * milliseconds at full speed, and the controller counts 2^n
	 * microframes: 8 ms is n = 6.  Written as it stood, 10 would have
	 * been every 128 ms.
	 */
	check(((in_ctx_word(&dev0, ep->dci + 1, 0) >> 16) & 0xff) == 6,
	    "polled every 2^%u microframes - 8 ms for a bInterval of 10",
	    (in_ctx_word(&dev0, ep->dci + 1, 0) >> 16) & 0xff);
	check((in_ctx_word(&dev0, ep->dci + 1, 4) >> 16) == 8,
	    "with %u bytes of bus time reserved per interval",
	    in_ctx_word(&dev0, ep->dci + 1, 4) >> 16);

	/*
	 * A report, into a buffer that is not the device's.  The device's
	 * buffer is filled with a pattern first, and must still hold it:
	 * that is the buffer the LED requests go through while a key
	 * report waits.
	 */
	kb = xhci_alloc_dma(XHCI_PAGE, &kb_phys, "an interrupt buffer");
	check(kb != NULL, "a buffer of its own");
	if (kb == NULL)
		return;
	memset((void *)dev0.buf, 0x5a, 64);
	xhci_cache(CACHE_CLEAN, (void *)dev0.buf, 64, "a transfer buffer");

	model_ep_supply(dev0.slot, ep->dci, press, sizeof(press));
	td_watch = &td;
	td_state = 0;
	check(xhci_transfer_start_buf(&dev0, ep, (vir_bytes)kb, kb_phys,
	    sizeof(press), &td) != 0, "a report is asked for");
	xhci_events_drain(2000, NULL, XHCI_TRB_TRANSFER_EVENT);

	check(td_state == 2 && td_cc == XHCI_CC_SUCCESS,
	    "and arrives (state %d, completion %u)", td_state, td_cc);
	check(td_actual == 8, "%u bytes of it", td_actual);
	xhci_cache(CACHE_INVALIDATE, kb, XHCI_CACHE_LINE, "a buffer");
	check(memcmp(kb, press, sizeof(press)) == 0,
	    "in the buffer it was asked into");
	xhci_cache(CACHE_INVALIDATE, (void *)dev0.buf, XHCI_CACHE_LINE,
	    "a transfer buffer");
	for (i = 0, untouched = 1; i < 64; i++)
		if (((uint8_t *)dev0.buf)[i] != 0x5a)
			untouched = 0;
	check(untouched, "and not in the device's");

	/*
	 * A halt, as the driver sees it: an entry left on the ring that the
	 * controller will never finish.  The reset has to move the
	 * controller past it - otherwise the next report is read into the
	 * abandoned entry, and the transfer waiting for it waits for ever.
	 */
	abandoned = xhci_ring_push(&ep->ring, (uint32_t)kb_phys, 0, 8,
	    XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC | XHCI_TRB_ISP);
	ep->halted = 1;

	check(xhci_ep_reset(&dev0, ep) == OK, "the endpoint resets");
	check(!ep->halted, "and is no longer marked halted");

	claims_reset();
	claim_addr[0] = abandoned;
	nclaims = 1;
	model_ep_supply(dev0.slot, ep->dci, next, sizeof(next));
	td_state = 0;
	check(xhci_transfer_start_buf(&dev0, ep, (vir_bytes)kb, kb_phys,
	    sizeof(next), &td) != 0, "the next report is asked for");
	xhci_events_drain(2000, NULL, XHCI_TRB_TRANSFER_EVENT);

	check(td_state == 2 && td_actual == 8,
	    "and arrives where it was asked (state %d, %u bytes)",
	    td_state, td_actual);
	check(!claim_seen[0], "the abandoned entry was skipped, not run");
	xhci_cache(CACHE_INVALIDATE, kb, XHCI_CACHE_LINE, "a buffer");
	check(memcmp(kb, next, sizeof(next)) == 0, "with the right bytes");

	td_watch = NULL;
	claims_reset();

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

/* A four-port hub with one transaction translator, think time 1. */
static const uint8_t hub_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02,
	0x09, 0x00, 0x01, 64,			/* class 9/0/1 */
	0x40, 0x1a, 0x01, 0x01,			/* 1a40:0101 */
	0x11, 0x01, 0, 1, 0, 1
};

static const uint8_t hub_config_desc[25] = {
	9, USB_DESC_CONFIG, 25, 0, 1, 1, 0, 0xe0, 50,
	9, 4, 0, 0, 1, 9, 0, 0, 0,
	7, 5, 0x81, 3, 1, 0, 12
};

static const uint8_t hub_desc[9] = {
	9, 0x29, 4, 0x20 /* TTT 1 */, 0x00, 50, 100, 0x00, 0xff
};

static size_t
hub_control(unsigned slot, const uint8_t setup[8], uint8_t *out, size_t max)
{
	unsigned type = setup[0], request = setup[1];
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);

	if (request == USB_REQ_GET_DESCRIPTOR && type == 0xa0 &&
	    (value >> 8) == 0x29) {
		memcpy(out, hub_desc, sizeof(hub_desc));
		return sizeof(hub_desc);
	}
	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_DEVICE) {
		memcpy(out, hub_device_desc, sizeof(hub_device_desc));
		return sizeof(hub_device_desc);
	}
	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_CONFIG) {
		memcpy(out, hub_config_desc, sizeof(hub_config_desc));
		return sizeof(hub_config_desc);
	}
	return 0;
}

static void
test_hub_slot(void)
{
	struct xhci_ep *ep;
	uint32_t w0, w1, w2;

	section("a hub: its slot context says it is one");

	check(bring_up(2) == OK, "the controller starts");
	model_control_handler(hub_control);
	model_attach(0, 3 /* high speed */);
	xhci_port_reset(0);

	check(xhci_device_attach(0, &dev0) == OK, "the hub enumerates");

	w0 = model_slot_word(dev0.slot, 0);
	w1 = model_slot_word(dev0.slot, 1);
	w2 = model_slot_word(dev0.slot, 2);

	check((w0 & XHCI_SLOT_HUB) != 0, "the slot is marked a hub (0x%08x)",
	    w0);
	check((w1 >> 24) == 4, "of %u ports", w1 >> 24);
	check(((w2 >> 16) & 0x3) == 1, "with think time %u",
	    (w2 >> 16) & 0x3);
	check((w0 >> 27) == 3,
	    "and still with its context entries (%u): the update kept what "
	    "the controller had", w0 >> 27);
	check(((w1 >> 16) & 0xff) == 1, "on root port %u", (w1 >> 16) & 0xff);

	ep = xhci_device_ep(&dev0, 1, 1);
	check(ep != NULL && ep->type == XHCI_EP_TYPE_INTR_IN,
	    "its interrupt endpoint is there");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}
