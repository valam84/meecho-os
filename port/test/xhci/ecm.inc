/*
 * CDC Ethernet, and what it asks of the controller that nothing before it
 * did: endpoints that exist only in an alternate setting, and devices with
 * more than one configuration.
 *
 * The data interface of a CDC Ethernet device has no endpoints in
 * alternate 0; its host opens the link by choosing alternate 1, and the
 * bulk pair appears then (xhci_set_interface()).  On the board a transfer
 * on an endpoint the controller was never told about is not an error but
 * silence - no event, the deadline - so each way of getting the change
 * wrong is checked here against a model that knows which contexts are
 * enabled and where it reads their rings.
 *
 * Included by ringtest.c after kbd.inc.
 */

/* The Mi Pad 4's function (minix/drivers/net/dwc3_ecm/ecm.c), byte for byte. */
static const uint8_t tab_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
	0x25, 0x05, 0xa1, 0xa4, 0x00, 0x01, 1, 2, 0, 1
};

static const uint8_t tab_config_desc[80] = {
	9, USB_DESC_CONFIG, 80, 0, 2, 1, 0, 0xc0, 250,
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 0,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x06, 0, 1,
	13, 0x24, 0x0f, 3, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	7, 5, 0x82, 3, 16, 0, 9,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0,
	9, 4, 1, 1, 2, 0x0a, 0, 0, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x01, 2, 0x00, 0x02, 0
};

/*
 * An adapter on a Realtek RTL8153: its own protocol in the first
 * configuration, CDC Ethernet in the second.  The notifications here are
 * on endpoint 1 and the frames on 2 and 3, so the bulk pair's contexts (5
 * and 6) lie beyond what alternate 0 used (3) - the case in which a slot
 * context left with the old number of entries hides them.
 */
static const uint8_t rtl_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x10, 0x02, 0x00, 0x00, 0x00, 64,
	0xda, 0x0b, 0x53, 0x81, 0x00, 0x30, 1, 2, 6, 2
};

static const uint8_t rtl_config0[39] = {
	9, USB_DESC_CONFIG, 39, 0, 1, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 3, 0xff, 0xff, 0x00, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,
	7, 5, 0x02, 2, 0x00, 0x02, 0,
	7, 5, 0x83, 3, 2, 0, 8
};

static const uint8_t rtl_config1[80] = {
	9, USB_DESC_CONFIG, 80, 0, 2, 2, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 5,
	5, 0x24, 0x00, 0x10, 0x01,
	5, 0x24, 0x06, 0, 1,
	13, 0x24, 0x0f, 3, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0,
	7, 5, 0x81, 3, 16, 0, 8,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0,
	9, 4, 1, 1, 2, 0x0a, 0, 0, 0,
	7, 5, 0x82, 2, 0x00, 0x02, 0,
	7, 5, 0x03, 2, 0x00, 0x02, 0
};

/*
 * Linux's own Ethernet gadget with RNDIS: RNDIS first (an interface
 * association, then 2/2/0xff), CDC Ethernet second.  This system has no
 * RNDIS driver, and Linux without one passes over that first
 * configuration; so does this driver.
 */
static const uint8_t rndis_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
	0x25, 0x05, 0xa2, 0xa4, 0x00, 0x01, 1, 2, 0, 2
};

static const uint8_t rndis_config0[35] = {
	9, USB_DESC_CONFIG, 35, 0, 2, 2, 0, 0xc0, 1,
	8, 0x0b, 0, 2, 0x02, 0x02, 0xff, 0,
	9, 4, 0, 0, 1, 0x02, 0x02, 0xff, 0,
	9, 4, 1, 0, 0, 0x0a, 0, 0, 0
};

/* Two configurations of a vendor's own protocol: the first is taken. */
static const uint8_t vendor_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
	0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 0, 0, 0, 2
};

static const uint8_t vendor_config0[25] = {
	9, USB_DESC_CONFIG, 25, 0, 1, 7, 0, 0xc0, 1,
	9, 4, 0, 0, 1, 0xff, 0, 0, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0
};

static const uint8_t vendor_config1[25] = {
	9, USB_DESC_CONFIG, 25, 0, 1, 8, 0, 0xc0, 1,
	9, 4, 0, 0, 1, 0xff, 1, 0, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0
};

static const struct ecm_mdev {
	const uint8_t *dev;
	const uint8_t *cfg[2];
	size_t cfg_len[2];
} ecm_tablet = {
	tab_device_desc, { tab_config_desc, NULL },
	{ sizeof(tab_config_desc), 0 }
}, ecm_rtl = {
	rtl_device_desc, { rtl_config0, rtl_config1 },
	{ sizeof(rtl_config0), sizeof(rtl_config1) }
}, ecm_rndis = {
	rndis_device_desc, { rndis_config0, rtl_config1 },
	{ sizeof(rndis_config0), sizeof(rtl_config1) }
}, ecm_vendor = {
	vendor_device_desc, { vendor_config0, vendor_config1 },
	{ sizeof(vendor_config0), sizeof(vendor_config1) }
};

static const struct ecm_mdev *ecm_dev;

/* GET_DESCRIPTOR of a configuration by its index, as a device does. */
static size_t
ecm_control(unsigned slot, const uint8_t setup[8], uint8_t *out, size_t max)
{
	unsigned request = setup[1];
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);
	unsigned length = setup[6] | ((unsigned)setup[7] << 8);
	unsigned index = value & 0xff;
	size_t n;

	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_DEVICE) {
		memcpy(out, ecm_dev->dev, 18);
		return 18 < length ? 18 : length;
	}
	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_CONFIG) {
		if (index >= 2 || ecm_dev->cfg[index] == NULL)
			return 0;
		n = ecm_dev->cfg_len[index];
		if (n > length)
			n = length;
		memcpy(out, ecm_dev->cfg[index], n);
		return n;
	}
	if (request == 9 /* SET_CONFIGURATION */)
		set_configuration_value = value;
	return 0;
}

static int
ecm_attach(const struct ecm_mdev *d)
{
	if (bring_up(2) != OK)
		return EIO;
	ecm_dev = d;
	model_control_handler(ecm_control);
	model_attach(0, 3 /* high speed */);
	xhci_port_reset(0);
	set_configuration_value = 0;
	return xhci_device_attach(0, &dev0);
}

/* A frame each way on the bulk pair, the way the URB layer would. */
static void
ecm_frames(struct xhci_ep *in, struct xhci_ep *out, const char *when)
{
	static uint8_t frame[1514], taken[2048];
	unsigned actual, i;

	for (i = 0; i < sizeof(frame); i++)
		frame[i] = (uint8_t)(i * 13 + 5);

	model_ep_supply(dev0.slot, in->dci, frame, 98);
	check(xhci_transfer(&dev0, in, 2048, &actual) == OK && actual == 98,
	    "%s: a frame of 98 bytes is read (%u)", when, actual);
	check(memcmp((void *)dev0.buf, frame, 98) == 0,
	    "%s: and it is the frame the device sent", when);

	memcpy((void *)dev0.buf, frame, 513);
	check(xhci_transfer(&dev0, out, 513, &actual) == OK && actual == 513,
	    "%s: a frame of 513 bytes is written (%u)", when, actual);
	check(model_ep_taken(dev0.slot, out->dci, taken, sizeof(taken)) == 513
	    && memcmp(taken, frame, 513) == 0,
	    "%s: and the device has it", when);
}

static void
test_ecm_alternates(void)
{
	struct xhci_ep *note, *in, *out;
	phys_bytes note_ring, old_in;
	unsigned actual, w0;
	static const uint8_t connected[8] = { 0xa1, 0, 1, 0, 0, 0, 0, 0 };

	section("CDC Ethernet: endpoints that come with an alternate setting");

	check(ecm_attach(&ecm_tablet) == OK, "the Mi Pad 4's function "
	    "enumerates");
	check(set_configuration_value == 1, "configuration %u was set",
	    set_configuration_value);
	check(dev0.niface == 2 && dev0.iface[0].class == 2 &&
	    dev0.iface[0].subclass == 6 && dev0.iface[1].class == 10,
	    "interfaces 2/6/0 and 10/0/0 recorded (%u)", dev0.niface);

	note = xhci_device_ep(&dev0, 2, 1);
	check(note != NULL && note->type == XHCI_EP_TYPE_INTR_IN &&
	    note->iface == 0, "the notifications' endpoint 2 in is there");
	if (note == NULL)
		return;
	note_ring = note->ring.p;

	check(xhci_device_ep(&dev0, 1, 1) == NULL &&
	    xhci_device_ep(&dev0, 1, 0) == NULL,
	    "and no bulk endpoint before the data interface is opened");
	check(!model_ep_valid(dev0.slot, 2) && !model_ep_valid(dev0.slot, 3),
	    "the controller has neither");

	check(xhci_set_interface(&dev0, 1, 1) == OK, "alternate 1 is set");
	in = xhci_device_ep(&dev0, 1, 1);
	out = xhci_device_ep(&dev0, 1, 0);
	check(in != NULL && in->type == XHCI_EP_TYPE_BULK_IN &&
	    in->max_packet == 512 && in->iface == 1 && in->alt == 1,
	    "endpoint 1 in: bulk, 512 bytes, interface 1 alt 1");
	check(out != NULL && out->type == XHCI_EP_TYPE_BULK_OUT &&
	    out->max_packet == 512, "endpoint 1 out: bulk, 512 bytes");
	if (in == NULL || out == NULL)
		return;
	check(model_ep_valid(dev0.slot, in->dci) &&
	    model_ep_valid(dev0.slot, out->dci),
	    "the controller has contexts %u and %u", in->dci, out->dci);
	check(model_ep_ring(dev0.slot, in->dci) == in->ring.p,
	    "and reads endpoint 1 in's ring from its start");
	check(xhci_device_ep(&dev0, 2, 1) == note && note->ring.p == note_ring
	    && model_ep_valid(dev0.slot, note->dci),
	    "the notifications' endpoint is the same entry on the same ring");
	w0 = model_slot_word(dev0.slot, 0);
	check((w0 >> 27) == 5, "the slot names %u context entries", w0 >> 27);
	check((w0 & XHCI_SLOT_SPEED(0xf)) == XHCI_SLOT_SPEED(3),
	    "and is still a high-speed one (0x%08x)", w0);

	ecm_frames(in, out, "alternate 1");

	check(xhci_set_interface(&dev0, 1, 0) == OK, "alternate 0 is set");
	check(xhci_device_ep(&dev0, 1, 1) == NULL &&
	    xhci_device_ep(&dev0, 1, 0) == NULL,
	    "the bulk pair is gone from the driver");
	check(!model_ep_valid(dev0.slot, 2) && !model_ep_valid(dev0.slot, 3),
	    "and from the controller");
	check(model_ep_valid(dev0.slot, note->dci),
	    "the notifications' endpoint stays");

	old_in = in->ring.p;
	check(xhci_set_interface(&dev0, 1, 1) == OK, "alternate 1 again");
	in = xhci_device_ep(&dev0, 1, 1);
	out = xhci_device_ep(&dev0, 1, 0);
	check(in != NULL && out != NULL, "the bulk pair is back");
	if (in == NULL || out == NULL)
		return;
	check(model_ep_ring(dev0.slot, in->dci) == in->ring.p,
	    "on a ring the controller reads from its start (0x%lx, was 0x%lx)",
	    (unsigned long)in->ring.p, (unsigned long)old_in);
	ecm_frames(in, out, "alternate 1, second time");

	/* What a driver that starts again does: the same setting twice. */
	check(xhci_set_interface(&dev0, 1, 1) == OK,
	    "alternate 1 while in alternate 1");
	in = xhci_device_ep(&dev0, 1, 1);
	out = xhci_device_ep(&dev0, 1, 0);
	check(in != NULL && out != NULL, "the bulk pair, dropped and added");
	if (in == NULL || out == NULL)
		return;
	ecm_frames(in, out, "alternate 1, third time");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());

	/* Settings the device does not have change nothing. */
	check(xhci_set_interface(&dev0, 1, 2) == EINVAL,
	    "alternate 2 of interface 1 is refused");
	check(xhci_set_interface(&dev0, 4, 0) == EINVAL,
	    "interface 4 is refused");
	check(shim_warnings() == 2, "and both are said (%u)", shim_warnings());
	shim_warnings_reset();
	check(xhci_device_ep(&dev0, 1, 1) == in &&
	    model_ep_valid(dev0.slot, in->dci),
	    "the bulk pair is as it was");

	model_ep_supply(dev0.slot, note->dci, connected, sizeof(connected));
	check(xhci_transfer(&dev0, note, 16, &actual) == OK && actual == 8,
	    "a notification of 8 bytes arrives on endpoint 2 in (%u)", actual);

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
}

static void
test_ecm_adapter(void)
{
	struct xhci_ep *note, *in, *out;
	unsigned w0;

	section("an adapter: CDC Ethernet in its second configuration");

	check(ecm_attach(&ecm_rtl) == OK, "the adapter enumerates");
	check(dev0.config == 2 && set_configuration_value == 2,
	    "configuration %u was chosen and %u set - not the vendor's",
	    dev0.config, set_configuration_value);
	check(dev0.niface == 2 && dev0.iface[0].class == 2 &&
	    dev0.iface[0].subclass == 6, "interface 0 is %u/%u/%u",
	    dev0.iface[0].class, dev0.iface[0].subclass,
	    dev0.iface[0].protocol);

	note = xhci_device_ep(&dev0, 1, 1);
	check(note != NULL && note->type == XHCI_EP_TYPE_INTR_IN,
	    "the notifications on endpoint 1 in");
	check(xhci_device_ep(&dev0, 2, 1) == NULL,
	    "and none of the vendor configuration's endpoints");
	w0 = model_slot_word(dev0.slot, 0);
	check((w0 >> 27) == 3, "three context entries (%u)", w0 >> 27);

	check(xhci_set_interface(&dev0, 1, 1) == OK, "alternate 1 is set");
	in = xhci_device_ep(&dev0, 2, 1);
	out = xhci_device_ep(&dev0, 3, 0);
	check(in != NULL && out != NULL, "endpoints 2 in and 3 out");
	if (in == NULL || out == NULL)
		return;
	w0 = model_slot_word(dev0.slot, 0);
	check((w0 >> 27) == 6, "the slot now names %u context entries, for "
	    "contexts %u and %u", w0 >> 27, in->dci, out->dci);
	check(model_ep_valid(dev0.slot, in->dci) &&
	    model_ep_valid(dev0.slot, out->dci), "and the controller has them");
	ecm_frames(in, out, "the adapter");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

static void
test_config_choice(void)
{
	section("which configuration, when there are two");

	check(ecm_attach(&ecm_rndis) == OK, "Linux's RNDIS gadget enumerates");
	check(dev0.config == 2 && dev0.iface[0].class == 2 &&
	    dev0.iface[0].subclass == 6,
	    "the RNDIS configuration is passed over for CDC Ethernet "
	    "(configuration %u, %u/%u)", dev0.config, dev0.iface[0].class,
	    dev0.iface[0].subclass);

	check(ecm_attach(&ecm_vendor) == OK, "a vendor's device enumerates");
	check(dev0.config == 7 && set_configuration_value == 7,
	    "two vendor configurations: the first (%u)", dev0.config);

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}
