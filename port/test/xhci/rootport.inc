/*
 * A device on a root port that arrives after the controller is up, leaves,
 * and is replaced: the Mi Pad 4's keyboard on an OTG adapter, which
 * connects only once the PMIC's boost has put VBUS on the port
 * (port/clover/hw-usbhost.md, step 4).  On the CB2 the root port always
 * has a hub on it, and nothing here was ever asked of the driver.
 *
 * What is checked: a Port Status Change event is only noted where the ring
 * is drained (that may be inside a client's transfer), and acted on by
 * xhci_root_ports(); the device is enumerated and announced once; news
 * that is not about who is there enumerates nothing; a device unplugged
 * goes the way a device behind a hub goes (xhci_device_remove()); a new
 * one gets a new name; one replaced between two looks is removed before
 * its successor is enumerated; the change bits are acknowledged, or the
 * next arrival would be silent; a USB 3 port's news drives nothing.
 *
 * Included by ringtest.c after tree.inc, whose record of devices let go
 * (xhci_urb_device_gone()) it uses, and kbd.inc's keyboard.
 */

#define MAX_ANNOUNCED	8

static unsigned announced[MAX_ANNOUNCED];
static unsigned nannounced;

/* What xhci_urb.c does when a device is found: tell its drivers. */
void
xhci_urb_announce(struct xhci_device *dev)
{
	if (nannounced < MAX_ANNOUNCED)
		announced[nannounced] = dev->id;
	nannounced++;
}

static void
news_in(void)
{
	(void)xhci_events_drain(0, NULL, 0);
}

static uint32_t
portsc(unsigned p)
{
	return xhci_rd(xhci.regs, xhci.caplength + XHCI_PORTSC(p));
}

static void
test_root_port_hotplug(void)
{
	struct xhci_device *d = &xhci.dev[0];
	unsigned first, second;

	section("the root port: a keyboard after the start, out, and another");

	check(bring_up(2) == OK, "the controller starts");
	/* What read_protocols() says of this part: USB 2 on 1, USB 3 on 2. */
	xhci.port_major[0] = 2;
	xhci.port_major[1] = 3;
	model_control_handler(kbd_control);
	nannounced = 0;
	ngone = 0;

	xhci_root_ports();
	check(d->slot == 0 && nannounced == 0, "no news: nothing done");

	model_attach(0, 1 /* full speed */);
	news_in();
	check(xhci.port_news == 0x1, "the event is noted for port 1 (news "
	    "0x%x)", xhci.port_news);
	check(d->slot == 0, "and nothing is done where the ring is drained");

	xhci_root_ports();
	check(d->slot != 0 && d->speed == 1,
	    "the keyboard enumerates on port 1 at full speed (slot %u, "
	    "speed %u)", d->slot, d->speed);
	check(d->niface == 1 && d->iface[0].class == 3 &&
	    d->iface[0].subclass == 1 && d->iface[0].protocol == 1,
	    "as a boot keyboard");
	check(nannounced == 1 && announced[0] == d->id,
	    "and is announced once (%u)", nannounced);
	check((portsc(0) & XHCI_PORTSC_CHANGES) == 0,
	    "the port's change bits are acknowledged (0x%08x)", portsc(0));
	first = d->id;

	/* A reset completing is a change too, and not about who is there. */
	news_in();
	xhci.port_news |= 0x1;
	xhci_root_ports();
	check(d->id == first && nannounced == 1,
	    "news of a port whose device is still there enumerates nothing");

	model_detach(0);
	news_in();
	xhci_root_ports();
	check(d->slot == 0, "unplugged: the entry is free");
	check(ngone == 1 && gone_id[0] == first,
	    "and its drivers were told it is gone (%u, id %u)", ngone,
	    gone_id[0]);

	model_control_handler(device_control);
	model_attach(0, 3 /* high speed */);
	news_in();
	xhci_root_ports();
	check(d->slot != 0 && d->speed == 3 && d->id != first,
	    "another device on the same port, a new name (%u, was %u)",
	    d->id, first);
	check(nannounced == 2, "announced (%u)", nannounced);
	second = d->id;

	/* Out and in again before the driver looked: two changes, one news. */
	model_control_handler(kbd_control);
	model_detach(0);
	model_attach(0, 1);
	news_in();
	xhci_root_ports();
	check(ngone == 2 && gone_id[1] == second,
	    "a device replaced between two looks is let go first (%u)",
	    ngone);
	check(d->slot != 0 && d->id != second && d->speed == 1,
	    "and its successor enumerates (id %u, speed %u)", d->id,
	    d->speed);
	check(nannounced == 3, "announced (%u)", nannounced);

	model_attach(1, 3);
	news_in();
	xhci_root_ports();
	check(xhci.dev[1].slot == 0,
	    "the USB 3 port's news drives nothing on a USB 2 controller");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
}

/*
 * A device that answers nothing: tried XHCI_PORT_TRIES times, then left
 * alone however much news its port makes, and tried again once it has
 * been unplugged and plugged in.  On the tablet a low-speed keyboard that
 * would not give its descriptor filled the screen with retries.
 */
static unsigned mute_asked;

static size_t
mute_control(unsigned slot, const uint8_t setup[8], uint8_t *out, size_t max)
{
	(void)slot;
	(void)setup;
	(void)out;
	(void)max;
	mute_asked++;
	return 0;
}

static void
test_root_port_gives_up(void)
{
	unsigned i, asked;

	section("the root port: a device that cannot be enumerated");

	check(bring_up(2) == OK, "the controller starts");
	xhci.port_major[0] = 2;
	model_control_handler(mute_control);
	nannounced = 0;
	mute_asked = 0;

	model_attach(0, 2 /* low speed */);
	for (i = 0; i < 10; i++) {
		news_in();
		xhci_root_ports();
	}
	check(xhci.dev[0].slot == 0 && nannounced == 0, "nothing enumerated");
	check(xhci.port_tries[0] == XHCI_PORT_TRIES,
	    "tried %u times, want %u", xhci.port_tries[0], XHCI_PORT_TRIES);
	asked = mute_asked;
	for (i = 0; i < 5; i++) {
		news_in();
		xhci.port_news |= 0x1;
		xhci_root_ports();
	}
	check(mute_asked == asked, "asked again after giving up (%u more)",
	    mute_asked - asked);

	model_detach(0);
	news_in();
	xhci_root_ports();
	check(xhci.port_tries[0] == 0, "unplugged: the count is cleared");
	model_control_handler(kbd_control);
	model_attach(0, 1);
	news_in();
	xhci_root_ports();
	check(xhci.dev[0].slot != 0 && nannounced == 1,
	    "a device plugged in after it is tried again, and enumerates");
}
