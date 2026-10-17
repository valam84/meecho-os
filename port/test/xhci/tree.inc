/*
 * A hub behind a hub, and what hangs behind that: a USB-C dock behind the
 * CB2's Terminus hub.  The board's hub is described as the board's own
 * Linux read it (port/cb2-usb/reference.txt); the dock's hub, Ethernet
 * chip and card reader are shaped like such things are and NOT yet taken
 * from the user's dock, which had not been read when this was written.
 *
 * What the controller is asked to do for it that nothing before asked: a
 * route string two nibbles deep, a translator that belongs to the dock and
 * not to the hub the device hangs off when there is a full-speed hub in
 * between, news from two hubs told apart by name, and a whole subtree
 * leaving at once - farthest first, every slot given back - and coming
 * back on the same slots with new names.  The model knows where every
 * device is (model_tree_add()) and answers only a slot context that
 * reaches it; every check here is silent on the board when wrong, because
 * a controller asked for a device where none is says nothing but
 * "transaction error".
 *
 * Included by ringtest.c after ecm.inc, whose RTL8153-shaped adapter it
 * borrows until the dock's own chip is described.
 */

/*
 * What xhci_urb.c does when a device goes, as far as the tree can see it:
 * in which order the devices were let go.  The rest of it (the drivers
 * told, the transfers ended) is IPC and not this stand's.
 */
#define MAX_GONE	16

static unsigned gone_id[MAX_GONE];
static unsigned ngone;

void
xhci_urb_device_gone(struct xhci_device *dev)
{
	if (ngone < MAX_GONE)
		gone_id[ngone] = dev->id;
	ngone++;
}

/*
 * The devices.  The board's hub is the Terminus of port/cb2-usb/
 * reference.txt as it is: single TT, four ports, ganged power, think time
 * 0, power good in 100 ms.
 */
static const uint8_t term_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x09, 0x00, 0x01, 64,
	0x40, 0x1a, 0x01, 0x01, 0x11, 0x01, 0, 1, 0, 1
};

static const uint8_t term_hub_desc[9] = {
	9, 0x29, 4, 0x00, 0x00, 50, 100, 0x00, 0xff
};

/*
 * The dock's hub: four ports, individual power, think time 1 - a WCH part
 * by its vendor number, the rest a choice (the dock has not been read).
 */
static const uint8_t dock_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x09, 0x00, 0x01, 64,
	0x86, 0x1a, 0x95, 0x80, 0x00, 0x01, 0, 1, 0, 1
};

static const uint8_t dock_hub_desc[9] = {
	9, 0x29, 4, 0x21, 0x00, 50, 100, 0x00, 0xff
};

/* A full-speed hub, for the translator that is not the parent's. */
static const uint8_t fshub_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x10, 0x01, 0x09, 0x00, 0x00, 8,
	0x51, 0x04, 0x46, 0x20, 0x00, 0x01, 0, 0, 0, 1
};

static const uint8_t fshub_hub_desc[9] = {
	9, 0x29, 4, 0x00, 0x00, 50, 100, 0x00, 0xff
};

/* Every hub's configuration: one interface, one interrupt endpoint. */
static const uint8_t anyhub_config_desc[25] = {
	9, USB_DESC_CONFIG, 25, 0, 1, 1, 0, 0xe0, 50,
	9, 4, 0, 0, 1, 9, 0, 0, 0,
	7, 5, 0x81, 3, 1, 0, 12
};

/* The dock's card reader: a bulk-only mass storage device. */
static const uint8_t reader_device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
	0xe3, 0x05, 0x51, 0x07, 0x00, 0x01, 1, 2, 3, 1
};

enum kind { K_NONE, K_TERM, K_DOCK, K_FSHUB, K_RTL, K_READER, K_KBD };

static enum kind node_kind[16];

static size_t
hub_answer(const uint8_t *dd, const uint8_t *hd, const uint8_t setup[8],
	uint8_t *out)
{
	unsigned type = setup[0], request = setup[1];
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);

	if (request != USB_REQ_GET_DESCRIPTOR)
		return 0;
	if (type == 0xa0 && (value >> 8) == 0x29) {
		memcpy(out, hd, 9);
		return 9;
	}
	if ((value >> 8) == USB_DESC_DEVICE) {
		memcpy(out, dd, 18);
		return 18;
	}
	if ((value >> 8) == USB_DESC_CONFIG) {
		memcpy(out, anyhub_config_desc, sizeof(anyhub_config_desc));
		return sizeof(anyhub_config_desc);
	}
	return 0;
}

/* Each device answers as itself: the model says which device a slot is. */
static size_t
tree_control(unsigned slot, const uint8_t setup[8], uint8_t *out, size_t max)
{
	int n = model_slot_node(slot);
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);

	switch (n >= 0 ? node_kind[n] : K_NONE) {
	case K_TERM:
		return hub_answer(term_device_desc, term_hub_desc, setup, out);
	case K_DOCK:
		return hub_answer(dock_device_desc, dock_hub_desc, setup, out);
	case K_FSHUB:
		return hub_answer(fshub_device_desc, fshub_hub_desc, setup,
		    out);
	case K_RTL:
		ecm_dev = &ecm_rtl;
		return ecm_control(slot, setup, out, max);
	case K_READER:
		if (setup[1] == USB_REQ_GET_DESCRIPTOR &&
		    (value >> 8) == USB_DESC_DEVICE) {
			memcpy(out, reader_device_desc, 18);
			return 18;
		}
		if (setup[1] == USB_REQ_GET_DESCRIPTOR &&
		    (value >> 8) == USB_DESC_CONFIG) {
			memcpy(out, config_desc, sizeof(config_desc));
			return sizeof(config_desc);
		}
		return 0;
	case K_KBD:
		return kbd_control(slot, setup, out, max);
	default:
		return 0;
	}
}

static int
plug(unsigned route, unsigned speed, enum kind kind)
{
	int n = model_tree_add(0, route, speed,
	    kind == K_TERM || kind == K_DOCK || kind == K_FSHUB);

	if (n >= 0)
		node_kind[n] = kind;
	return n;
}

static int
was_gone(unsigned id)
{
	unsigned i;

	for (i = 0; i < ngone && i < MAX_GONE; i++)
		if (gone_id[i] == id)
			return (int)i;
	return -1;
}

/* The board's hub on root port 1, as xhci.c would find it. */
static struct xhci_device *
board_hub(void)
{
	struct xhci_device *term = &xhci.dev[0];

	memset(node_kind, 0, sizeof(node_kind));
	model_control_handler(tree_control);
	plug(0, 3, K_TERM);
	model_attach(0, 3);
	xhci_port_reset(0);
	if (xhci_device_attach(0, term) != OK)
		return NULL;
	return term;
}

static void
test_tree_dock(void)
{
	struct xhci_device *term, *dock, *rtl, *reader, *kbd, *dock2, *rtl2;
	unsigned dock_id, rtl_id, reader_id, kbd_id, dock_slot, ids_before;
	int n_dock, i;

	section("a dock behind the board's hub: a hub behind a hub");

	check(bring_up(2) == OK, "the controller starts");
	term = board_hub();
	check(term != NULL && term->class == 9, "the board's hub enumerates");
	if (term == NULL)
		return;
	check(xhci_wants(term, "usb_hub"), "and is for the hub driver");

	n_dock = plug(0x3, 3, K_DOCK);
	dock = xhci_hub_port(term, 3, XHCI_HUB_HIGH);
	check(dock != NULL, "the dock enumerates behind port 3");
	if (dock == NULL)
		return;
	check(dock->route == 0x3 && dock->tier == 1,
	    "at route 0x%x, tier %u", dock->route, dock->tier);
	check(dock->hub_id == term->id && dock->hub_port == 3,
	    "hanging off the board's hub, port %u", dock->hub_port);
	check(dock >= &xhci.dev[XHCI_MAX_PORTS],
	    "in an entry above the root ports' own (%d)",
	    (int)(dock - xhci.dev));
	check(dock->class == 9 && xhci_wants(dock, "usb_hub"),
	    "a hub, for the hub driver too");
	check((model_slot_word(dock->slot, 0) & XHCI_SLOT_HUB) != 0 &&
	    (model_slot_word(dock->slot, 1) >> 24) == 4 &&
	    ((model_slot_word(dock->slot, 2) >> 16) & 3) == 1,
	    "its slot says hub, 4 ports, think time 1 (%08x %08x %08x)",
	    model_slot_word(dock->slot, 0), model_slot_word(dock->slot, 1),
	    model_slot_word(dock->slot, 2));
	check(dock->id != term->id, "with a name of its own (%u, %u)",
	    dock->id, term->id);

	plug(0x13, 3, K_RTL);
	rtl = xhci_hub_port(dock, 1, XHCI_HUB_HIGH);
	check(rtl != NULL, "the Ethernet chip enumerates behind the dock's "
	    "port 1");
	if (rtl == NULL)
		return;
	check(rtl->route == 0x13 && rtl->tier == 2,
	    "at route 0x%x - port 1 of the dock, behind port 3 - tier %u",
	    rtl->route, rtl->tier);
	check(rtl->config == 2 && xhci_wants(rtl, "usb_ecm") &&
	    !xhci_wants(rtl, "usb_hub"),
	    "in configuration %u, for usb_ecm", rtl->config);

	plug(0x23, 3, K_READER);
	reader = xhci_hub_port(dock, 2, XHCI_HUB_HIGH);
	check(reader != NULL && reader->route == 0x23 &&
	    xhci_wants(reader, "usb_storage"),
	    "the card reader, at route 0x%x, for usb_storage",
	    reader != NULL ? reader->route : 0);

	plug(0x33, 1, K_KBD);
	kbd = xhci_hub_port(dock, 3, XHCI_HUB_FULL);
	check(kbd != NULL, "a full-speed keyboard in the dock's port 3");
	if (reader == NULL || kbd == NULL)
		return;
	check(kbd->tt_slot == dock->slot && kbd->tt_port == 3 &&
	    (model_slot_word(kbd->slot, 2) & 0xffff) ==
	    (dock->slot | (3u << 8)),
	    "reached through the dock's translator, slot %u port %u",
	    model_slot_word(kbd->slot, 2) & 0xff,
	    (model_slot_word(kbd->slot, 2) >> 8) & 0xff);
	check(xhci_wants(kbd, "usb_kbd"), "and for usb_kbd");

	check(model_complaint() == NULL, "the model reached every device: %s",
	    model_complaint() ? model_complaint() : "yes");

	/*
	 * News by name.  The old rule - the last hub in the table - finds
	 * the dock for news from the board's hub, or the other way round,
	 * depending on nothing but the order of the entries.
	 */
	check(xhci_hub_by_id(term->id) == term, "news naming the board's hub "
	    "is about the board's hub");
	check(xhci_hub_by_id(dock->id) == dock, "news naming the dock is about "
	    "the dock");
	check(xhci_hub_by_id(rtl->id) == NULL, "news naming the Ethernet chip "
	    "is about no hub");
	check(xhci_hub_by_id(0) == NULL, "and news naming nothing, with two "
	    "hubs, is about none of them");

	/* The dock pulled out: the news comes from the board's hub. */
	dock_id = dock->id;
	rtl_id = rtl->id;
	reader_id = reader->id;
	kbd_id = kbd->id;
	dock_slot = dock->slot;
	ids_before = xhci.last_id;

	model_tree_unplug(n_dock);
	ngone = 0;
	check(xhci_hub_port(term, 3, XHCI_HUB_GONE) == NULL,
	    "the dock's port says gone");

	check(ngone == 4, "four devices went (%u)", ngone);
	check(was_gone(dock_id) == 3 && was_gone(rtl_id) >= 0 &&
	    was_gone(reader_id) >= 0 && was_gone(kbd_id) >= 0,
	    "the three behind the dock first, the dock last (at %d)",
	    was_gone(dock_id));
	check(model_slots_used() == 1, "the controller holds one slot - the "
	    "board's hub's (%u)", model_slots_used());
	check(xhci_device_by_id(dock_id) == NULL &&
	    xhci_device_by_id(rtl_id) == NULL, "and the driver knows none of "
	    "the four");
	check(xhci_hub_by_id(dock_id) == NULL, "news naming the dock now is "
	    "about no hub");
	check(xhci_hub_by_id(0) == term, "news naming nothing, with one hub, "
	    "is about that one");
	check(model_complaint() == NULL, "the model saw every slot given back "
	    "in order, and no memory freed under one: %s",
	    model_complaint() ? model_complaint() : "yes");

	/* And plugged in again, on the slots the controller gives back. */
	n_dock = plug(0x3, 3, K_DOCK);
	plug(0x13, 3, K_RTL);
	dock2 = xhci_hub_port(term, 3, XHCI_HUB_HIGH);
	check(dock2 != NULL && dock2->slot == dock_slot,
	    "the dock comes back, on slot %u again",
	    dock2 != NULL ? dock2->slot : 0);
	if (dock2 == NULL)
		return;
	check(dock2->id != dock_id && dock2->id > ids_before,
	    "with a new name (%u, was %u)", dock2->id, dock_id);
	check(xhci_hub_by_id(dock_id) == NULL, "so news from the old dock's "
	    "thread, naming the old name, finds nothing on the old slot");
	rtl2 = xhci_hub_port(dock2, 1, XHCI_HUB_HIGH);
	check(rtl2 != NULL && rtl2->route == 0x13 && rtl2->id != rtl_id,
	    "and the Ethernet chip with it");

	for (i = 0; i < 3; i++) {
		model_tree_unplug(n_dock);
		xhci_hub_port(term, 3, XHCI_HUB_GONE);
		n_dock = plug(0x3, 3, K_DOCK);
		plug(0x13, 3, K_RTL);
		dock2 = xhci_hub_port(term, 3, XHCI_HUB_HIGH);
		if (dock2 == NULL ||
		    xhci_hub_port(dock2, 1, XHCI_HUB_HIGH) == NULL)
			break;
	}
	check(i == 3, "three more rounds of out and in, %d of them whole", i);
	check(model_slots_used() == 3, "and the controller holds three slots "
	    "at the end (%u)", model_slots_used());

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

/*
 * A full-speed hub between the board's hub and a full-speed device.  The
 * full-speed hub has no translator; the device's traffic goes up through
 * it as it came, and is split by the board's hub - so the device's slot
 * names the board's hub and the port the full-speed hub is on.  Naming
 * the hub it hangs off, which is the rule that was enough for one tier,
 * is a device that never answers.
 */
static void
test_tree_fullspeed_hub(void)
{
	struct xhci_device *term, *fshub, *dev;

	section("a full-speed hub in between: whose translator");

	check(bring_up(2) == OK, "the controller starts");
	term = board_hub();
	check(term != NULL, "the board's hub enumerates");
	if (term == NULL)
		return;

	plug(0x4, 1, K_FSHUB);
	fshub = xhci_hub_port(term, 4, XHCI_HUB_FULL);
	check(fshub != NULL && fshub->tt_slot == term->slot &&
	    fshub->tt_port == 4, "a full-speed hub on port 4, through the "
	    "board's hub's translator");
	if (fshub == NULL)
		return;

	plug(0x24, 1, K_KBD);
	dev = xhci_hub_port(fshub, 2, XHCI_HUB_FULL);
	check(dev != NULL, "a full-speed device on its port 2 enumerates");
	if (dev == NULL)
		return;
	check(dev->route == 0x24 && dev->hub_id == fshub->id,
	    "at route 0x%x, hanging off the full-speed hub", dev->route);
	check(dev->tt_slot == term->slot && dev->tt_port == 4,
	    "and reached through the board's hub, port 4 - not through the "
	    "hub it hangs off (slot %u port %u)", dev->tt_slot, dev->tt_port);

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

/*
 * News of a device on a port that has one: what a restarted hub driver
 * says about every port in use, and what a device that dropped off and
 * came back between two polls looks like.  The old one goes first.
 */
static void
test_tree_stale_port(void)
{
	struct xhci_device *term, *dock, *a, *b;
	unsigned a_id;

	section("a device reported on a port that already has one");

	check(bring_up(2) == OK, "the controller starts");
	term = board_hub();
	plug(0x3, 3, K_DOCK);
	dock = term != NULL ? xhci_hub_port(term, 3, XHCI_HUB_HIGH) : NULL;
	check(dock != NULL, "the board's hub and the dock enumerate");
	if (dock == NULL)
		return;

	plug(0x13, 3, K_RTL);
	a = xhci_hub_port(dock, 1, XHCI_HUB_HIGH);
	check(a != NULL, "a device on the dock's port 1");
	if (a == NULL)
		return;
	a_id = a->id;

	ngone = 0;
	b = xhci_hub_port(dock, 1, XHCI_HUB_HIGH);
	check(b != NULL && ngone == 1 && gone_id[0] == a_id,
	    "reported again: the old one goes (%u gone)", ngone);
	check(b != NULL && b->id != a_id && xhci_device_by_id(a_id) == NULL,
	    "and the new one is a new device");
	check(model_slots_used() == 3, "three slots, not four (%u)",
	    model_slots_used());

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

/*
 * Five hubs below the root port is as far as USB and the route string go:
 * a device on the fifth enumerates, and news from a hub at that depth is
 * refused rather than given a route with no room for it.
 */
static void
test_tree_depth(void)
{
	struct xhci_device *h[6];
	unsigned t, route = 0, used;

	section("five tiers down, and no further");

	check(bring_up(2) == OK, "the controller starts");
	h[0] = board_hub();
	check(h[0] != NULL, "the board's hub enumerates");
	if (h[0] == NULL)
		return;

	for (t = 1; t <= 5; t++) {
		route |= 1u << (4 * (t - 1));
		plug(route, 3, K_DOCK);
		h[t] = xhci_hub_port(h[t - 1], 1, XHCI_HUB_HIGH);
		if (h[t] == NULL || h[t]->route != route || h[t]->tier != t)
			break;
	}
	check(t == 6, "a hub on port 1 of each, down to tier 5 (route "
	    "0x%05x)", route);
	if (t != 6)
		return;

	used = model_slots_used();
	shim_warnings_reset();
	check(xhci_hub_port(h[5], 1, XHCI_HUB_HIGH) == NULL,
	    "news from the hub at tier 5 is refused");
	check(model_slots_used() == used, "and takes no slot");
	check(shim_warnings() != 0, "and says so");
	shim_warnings_reset();

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
}
