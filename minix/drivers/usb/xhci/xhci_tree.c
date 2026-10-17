/*
 * The devices together: which one hangs off which hub, what each is called,
 * and which client driver it is for.
 *
 * One hub was all this driver knew how to serve, and it knew it in three
 * places that each assumed it.  The hub's news ("something on my port 3,
 * high speed") was taken to be about the last device of class 9 in the
 * table, whoever sent it.  A device leaving took with it the devices on
 * that hub port, and not the devices behind THEM.  And a device's name was
 * its slot number, which the controller hands to the next device once the
 * slot is given back.  A USB-C dock behind the CB2's Terminus hub breaks all
 * three at once: it is a second hub, it has devices of its own behind it,
 * and it is pulled out and plugged back in as a whole.
 *
 * So the tree is kept here, in a file the host stand builds
 * (port/test/xhci), and the rules are:
 *
 *   A device has a name, its id, given once and never again
 *   (xhci_new_id()).  Requests, announcements and hub news all carry it.
 *
 *   Hub news names the hub it is about (USB_INFO_DEV, see xhci_urb.c),
 *   and a hub is found by that name - not guessed.
 *
 *   A device hangs off a hub port (hub_id, hub_port), and a device that
 *   leaves takes everything behind it with it, the farthest first: a
 *   device's slot context names its translator's slot, and a hub's slot
 *   given back while a device below still points at it is a question the
 *   specification does not answer.  Linux goes the same way round
 *   (usb_disconnect() recurses into the children first).
 *
 * Which hub driver instance drives which hub is NOT decided here, because
 * there is only one: usb_hub holds every hub, a thread for each (see the
 * comment at the top of usb_hub.c for why one instance and not one per
 * hub).  The check that news comes from the driver that was given the hub
 * is xhci_urb.c's, with xhci_wants() below.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>

#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

/*
 * A new name.  Zero is "no device" in the protocol and is never given; a
 * counter that wraps after four thousand million devices skips whatever
 * is still alive, so even then a name is never two devices at once.
 */
unsigned
xhci_new_id(void)
{
	do {
		if (++xhci.last_id == 0)
			xhci.last_id = 1;
	} while (xhci_device_by_id(xhci.last_id) != NULL);

	return xhci.last_id;
}

struct xhci_device *
xhci_device_by_id(unsigned id)
{
	unsigned i;

	if (id == 0)
		return NULL;
	for (i = 0; i < XHCI_MAX_DEVS; i++)
		if (xhci.dev[i].slot != 0 && xhci.dev[i].id == id)
			return &xhci.dev[i];
	return NULL;
}

/*
 * The hub a piece of news is about.
 *
 * By name, which is what the news carries since there can be more than one.
 * A hub driver built before that sends no name (zero): the news is then
 * taken to be about the hub when there is exactly one, which is the only
 * case in which the old guess was right, and refused when there are more.
 */
struct xhci_device *
xhci_hub_by_id(unsigned id)
{
	struct xhci_device *d, *only = NULL;
	unsigned i, n = 0;

	if (id != 0) {
		d = xhci_device_by_id(id);
		return d != NULL && d->class == 9 ? d : NULL;
	}

	for (i = 0; i < XHCI_MAX_DEVS; i++)
		if (xhci.dev[i].slot != 0 && xhci.dev[i].class == 9) {
			only = &xhci.dev[i];
			n++;
		}
	return n == 1 ? only : NULL;
}

/*
 * Which driver handles this device.
 *
 * This is the job devman does in the system this stack came from: it reads
 * config files that match a device's class against a driver's binary.
 * There is no devman here yet, so the matching is a table of five lines -
 * and it has to exist rather than announcing to everybody, because a
 * driver told about a device it does not handle does not politely decline.
 *
 * A keyboard is an interface, not a device: class 3 (HID), subclass 1
 * (boot interface), protocol 1 (keyboard), on whichever interface it is.
 * The device's own class is 0 on every keyboard seen so far.  A mouse is
 * the same with protocol 2.
 *
 * So the question is not "which driver" but "does this device want this
 * driver": a receiver for a keyboard and a mouse is one device with both
 * interfaces, and both drivers are told about it - each then speaks to
 * its own interface, and transfers from the two are kept apart by the
 * endpoint, which is the controller's business.  A hub or a flash drive
 * wants exactly one.
 *
 * CDC Ethernet is an interface too: class 2 (communications), subclass 6
 * (Ethernet networking), protocol 0, the one Linux's cdc_ether binds.  The
 * device's own class is 2 on Linux's gadget and on the Mi Pad 4's
 * function, 0 on adapters.
 *
 * Every hub goes to the one hub driver, however many there are: usb_hub
 * holds them all.
 */
static const char *const drivers[] = {
	"usb_hub", "usb_storage", "usb_kbd", "usb_mouse", "usb_ecm"
};

static int
has_iface(const struct xhci_device *dev, unsigned class, unsigned subclass,
	unsigned protocol)
{
	unsigned i;

	for (i = 0; i < dev->niface; i++)
		if (dev->iface[i].class == class &&
		    dev->iface[i].subclass == subclass &&
		    dev->iface[i].protocol == protocol)
			return 1;
	return 0;
}

int
xhci_wants(const struct xhci_device *dev, const char *driver)
{
	if (dev->class == 9)
		return strcmp(driver, "usb_hub") == 0;
	if (dev->class == 8 || dev->iface_class == 8)
		return strcmp(driver, "usb_storage") == 0;
	if (strcmp(driver, "usb_kbd") == 0)
		return has_iface(dev, 3, 1, 1);
	if (strcmp(driver, "usb_mouse") == 0)
		return has_iface(dev, 3, 1, 2);
	if (strcmp(driver, "usb_ecm") == 0)
		return has_iface(dev, 2, 6, 0);
	return 0;
}

/* Whether any driver of this system handles it at all. */
int
xhci_wanted(const struct xhci_device *dev)
{
	unsigned i;

	for (i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++)
		if (xhci_wants(dev, drivers[i]))
			return 1;
	return 0;
}

/*
 * A device leaves, and everything behind it with it, the farthest first.
 *
 * Its drivers are told before its memory goes (xhci_urb_device_gone():
 * the transfers waiting on it end with ENODEV, the drivers that were given
 * it hear USB_WITHDRAW_DEV), and the slot is given back to the controller
 * before its memory is (xhci_device_free()).
 *
 * The recursion is as deep as the tree, which USB keeps to six.
 */
void
xhci_device_remove(struct xhci_device *dev)
{
	unsigned i;

	if (dev->slot == 0)
		return;

	for (i = 0; i < XHCI_MAX_DEVS; i++)
		if (xhci.dev[i].slot != 0 && &xhci.dev[i] != dev &&
		    xhci.dev[i].hub_id == dev->id)
			xhci_device_remove(&xhci.dev[i]);

	log_info(&xhci_log, "device %u (slot %u, route 0x%x) is gone\n",
	    dev->id, dev->slot, dev->route);

	xhci_urb_device_gone(dev);
	xhci_device_free(dev);
}

/*
 * The root ports the controller said changed: a device arrived, left, or
 * left and another arrived between two looks.
 *
 * On the CB2 this never mattered - a hub sits on the root port for good
 * and everything else is that hub's news - but a Mi Pad 4 with an OTG
 * adapter has the keyboard on the root port itself, and it arrives after
 * the controller is up: VBUS comes from the PMIC's boost, which comes
 * from Type-C, which is what made this a host to begin with.
 *
 * Only ports whose protocol the extended capabilities named USB 2 are
 * driven, as at start-up (xhci.c, reset_ports()).  The change bits are
 * acknowledged first, whatever is done next: a controller signals a port
 * again only when a change bit goes from clear to set, and one left set
 * would make the next arrival silent.  A device gets 100 ms of a stable
 * connection before its port is reset (USB 2.0, 7.1.7.3, tATTDB).
 */
void
xhci_root_ports(void)
{
	unsigned p, news, reg;
	uint32_t v;
	struct xhci_device *dev;

	news = xhci.port_news;
	xhci.port_news = 0;

	for (p = 0; p < xhci.nports && p < XHCI_MAX_PORTS; p++) {
		if (!(news & (1u << p)))
			continue;
		reg = xhci.caplength + XHCI_PORTSC(p);
		v = xhci_rd(xhci.regs, reg);
		if (v & XHCI_PORTSC_CHANGES)
			xhci_wr(xhci.regs, reg, XHCI_PORTSC_KEEP(v) |
			    (v & XHCI_PORTSC_CHANGES));
		if (xhci.port_major[p] != 2)
			continue;

		dev = &xhci.dev[p];
		if (dev->slot != 0) {
			/*
			 * Still connected, and never disconnected since it was
			 * enumerated: whatever changed (a reset completing, a
			 * link state) is not news about who is there.
			 */
			if ((v & XHCI_PORTSC_CCS) && !(v & XHCI_PORTSC_CSC))
				continue;
			log_info(&xhci_log, "port %u: device %u %s\n", p + 1,
			    dev->id, (v & XHCI_PORTSC_CCS) ? "replaced by "
			    "another" : "unplugged");
			xhci_device_remove(dev);
		}
		if (!(v & XHCI_PORTSC_CCS)) {
			xhci.port_tries[p] = 0;	/* empty: the next one is new */
			continue;
		}
		if (xhci.port_tries[p] >= XHCI_PORT_TRIES)
			continue;

		micro_delay(100000);
		v = xhci_rd(xhci.regs, reg);
		if (!(v & XHCI_PORTSC_CCS)) {
			log_info(&xhci_log, "port %u: a device came and went\n",
			    p + 1);
			continue;
		}
		log_info(&xhci_log, "port %u: a device arrived\n", p + 1);
		if (xhci_port_reset(p) != OK ||
		    xhci_device_attach(p, dev) != OK) {
			xhci_device_free(dev);
			if (++xhci.port_tries[p] >= XHCI_PORT_TRIES)
				log_warn(&xhci_log, "port %u: could not be "
				    "enumerated %u times; left alone until it "
				    "is unplugged\n", p + 1, XHCI_PORT_TRIES);
			else {
				log_warn(&xhci_log, "port %u: could not be "
				    "enumerated\n", p + 1);
				xhci.port_news |= 1u << p;	/* again */
			}
			continue;
		}
		xhci.port_tries[p] = 0;
		xhci_urb_announce(dev);
	}
}

/* What hangs off this port of this hub now, if anything. */
static struct xhci_device *
child_on(const struct xhci_device *hub, unsigned port)
{
	unsigned i;

	for (i = 0; i < XHCI_MAX_DEVS; i++)
		if (xhci.dev[i].slot != 0 && xhci.dev[i].hub_id == hub->id &&
		    xhci.dev[i].hub_port == port)
			return &xhci.dev[i];
	return NULL;
}

/*
 * A hub driver's news about one of its ports: a device there at a speed, or
 * gone.  Answers the device that arrived, for the caller to announce, or
 * NULL.
 *
 * News of a device on a port that already has one means the old one is not
 * there any more, whatever became of the news of its going: a port is reset
 * before a device on it is reported, and a reset device has lost its
 * address.  That is what a restarted hub driver reports about every port
 * that was in use when its predecessor died, and what a device that drops
 * off and comes back between two polls looks like.  So the old one is
 * removed first; adding the new one beside it would leave two entries for
 * one port and a slot the controller would keep for ever.
 *
 * The speeds the hub driver reports are its own three constants, in the
 * order low, full, high; the fourth is a disconnection.  They are turned
 * into the identifiers the controller uses, which happen to run in a
 * different order - low is 2 and full is 1 - and a table of three is
 * clearer than arithmetic that happens to work.
 *
 * The free entry is looked for above the root ports' own (see
 * XHCI_MAX_DEVS), so that a device behind a hub never sits where xhci.c
 * would put the device on a root port.
 */
struct xhci_device *
xhci_hub_port(struct xhci_device *hub, unsigned port, unsigned news)
{
	struct xhci_device *old, *dev = NULL;
	unsigned speed, i;

	switch (news) {
	case XHCI_HUB_LOW:	speed = 2; break;
	case XHCI_HUB_FULL:	speed = 1; break;
	case XHCI_HUB_HIGH:	speed = 3; break;
	default:		speed = 0; break;
	}

	if ((old = child_on(hub, port)) != NULL) {
		if (speed != 0)
			log_info(&xhci_log, "hub %u port %u: a new device "
			    "where device %u was; that one is gone\n",
			    hub->id, port, old->id);
		xhci_device_remove(old);
	}

	if (speed == 0) {
		if (old == NULL)
			log_info(&xhci_log, "hub %u port %u: gone, and nothing "
			    "was known there\n", hub->id, port);
		return NULL;
	}

	for (i = XHCI_MAX_PORTS; i < XHCI_MAX_DEVS; i++)
		if (xhci.dev[i].slot == 0) {
			dev = &xhci.dev[i];
			break;
		}
	if (dev == NULL) {
		log_warn(&xhci_log, "hub %u port %u: no room for another "
		    "device (%u kept)\n", hub->id, port, XHCI_MAX_DEVS);
		return NULL;
	}

	log_info(&xhci_log, "hub %u port %u: a device at %s speed\n", hub->id,
	    port, speed == 2 ? "low" : speed == 1 ? "full" : "high");

	if (xhci_device_attach_hub(hub, port, speed, dev) != OK) {
		log_warn(&xhci_log, "hub %u port %u: could not be enumerated\n",
		    hub->id, port);
		xhci_device_free(dev);
		return NULL;
	}

	return dev;
}
