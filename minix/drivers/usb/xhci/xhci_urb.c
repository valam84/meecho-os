/*
 * The URB layer: the interface the client drivers of this system already
 * speak.
 *
 * This is the half of the old MINIX USB stack that is worth keeping.
 * usb_hub and usb_storage do not know what controller is underneath them:
 * they build a URB - a request block naming a device, an endpoint, a
 * direction and a buffer - and send it to whichever service answers to the
 * label "usbd".  So the way to make them work over an xHCI is not to port
 * them but to answer that protocol, which is what this file does.
 *
 * The protocol itself is small (minix/lib/libusb/usb.c is the other side):
 *
 *   USB_RQ_INIT          a driver says hello and names itself
 *   USB_RQ_SEND_URB      a request block, passed as a grant
 *   USB_RQ_CANCEL_URB    withdraw one that has not finished
 *   USB_ANNOUCE_DEV      this service tells a driver about a device
 *   USB_COMPLETE_URB     and that its request has finished
 *
 * Two things about this implementation are deliberately smaller than the
 * old server's, and both are written down rather than hidden:
 *
 *   Transfers are synchronous.  A URB is executed before its reply is
 *   sent, and the completion message follows immediately.  The client
 *   cannot tell - it blocks in sendrec either way - but this driver can:
 *   it is inside one transfer at a time and answers nothing else while it
 *   waits.  That is honest for control transfers, which take
 *   microseconds; it will not do for bulk, and the milestone that brings
 *   bulk in brings a queue with it.
 *
 *   There is no devman.  The old stack announced devices to a user-space
 *   matchmaker which started the right driver from a config file.  Here a
 *   device is announced to every driver that has said hello, and the
 *   driver decides.  With one driver running that is the same thing; with
 *   two it is not, and it is the next thing to fix in this area.
 */

#include <minix/drivers.h>
#include <minix/com.h>
#include <minix/ds.h>
#include <minix/ipc.h>
#include <minix/safecopies.h>
#include <minix/syslib.h>
#include <minix/usb.h>

#include <stddef.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

#define MAX_CLIENTS	4

static struct {
	endpoint_t ep;
	char name[16];
} clients[MAX_CLIENTS];

static unsigned nclients;
static unsigned next_urb_id = 1;

/*
 * One request block at a time, in a buffer of this driver's own.
 *
 * The client's grant begins at the URB's second field - libusb grants from
 * &urb->dev_id, leaving the list pointer behind - so the copy lands at
 * that offset into a local block and every field then falls where the
 * structure says it does.  Getting that offset wrong would read the
 * device id out of the middle of a pointer, which is the sort of thing
 * that looks like a broken device rather than a broken driver.
 */
static char urbmem[sizeof(struct usb_urb) + XHCI_DEV_BUF]
    __attribute__((aligned(8)));

#define URB_SKIP	offsetof(struct usb_urb, dev_id)

struct xhci_device *
xhci_device_by_id(unsigned id)
{
	unsigned i;

	for (i = 0; i < XHCI_MAX_PORTS; i++)
		if (xhci.dev[i].slot != 0 && xhci.dev[i].slot == id)
			return &xhci.dev[i];
	return NULL;
}

static void
reply(endpoint_t ep, long result, long urb_id)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_type = USB_REPLY;
	m.USB_RESULT = result;
	m.USB_URB_ID = urb_id;

	if (ipc_send(ep, &m) != OK)
		log_warn(&xhci_log, "cannot answer %d\n", ep);
}

static void
client_add(endpoint_t ep, const char *name)
{
	unsigned i;

	for (i = 0; i < nclients; i++)
		if (clients[i].ep == ep) {
			log_debug(&xhci_log, "%d says hello again\n", ep);
			return;
		}

	if (nclients >= MAX_CLIENTS) {
		log_warn(&xhci_log, "no room for another driver\n");
		return;
	}

	clients[nclients].ep = ep;

	/*
	 * The name a client gives is not its own: every driver built on
	 * this system's client library calls itself "dde".  What tells
	 * them apart is the label they run under, which the directory
	 * service knows - so that is what is asked for, and the name from
	 * the message is kept only for when the answer does not come.
	 */
	if (ds_retrieve_label_name(clients[nclients].name, ep) != OK)
		strncpy(clients[nclients].name, name,
		    sizeof(clients[0].name) - 1);
	clients[nclients].name[sizeof(clients[0].name) - 1] = '\0';
	nclients++;

	log_info(&xhci_log, "driver \"%s\" at %d is listening\n",
	    clients[nclients - 1].name, ep);
}

/*
 * Which driver handles this device.
 *
 * This is the job devman does in the system this stack came from: it reads
 * config files that match a device's class against a driver's binary.
 * There is no devman here yet, so the matching is a table of two lines -
 * and it has to exist rather than announcing to everybody, because a
 * driver told about a device it does not handle does not politely decline.
 * The hub driver panics on the second device it is offered.
 */
static const char *
driver_for(struct xhci_device *dev)
{
	if (dev->class == 9)
		return "usb_hub";
	if (dev->class == 8 || dev->iface_class == 8)
		return "usb_storage";
	return NULL;
}

/*
 * Tell the drivers about a device.
 *
 * Sent asynchronously, because a driver that is not waiting for it must
 * not be able to stop this service: the announcement is news, not a
 * question.
 */
void
xhci_urb_announce(struct xhci_device *dev)
{
	const char *want;
	message m;
	unsigned i;

	if (nclients == 0) {
		log_info(&xhci_log, "no driver is listening yet; the device "
		    "on port %u will be announced when one is\n",
		    dev->port + 1);
		return;
	}

	if ((want = driver_for(dev)) == NULL) {
		log_info(&xhci_log, "device %u is class %u/%u, which no "
		    "driver here handles\n", dev->slot, dev->class,
		    dev->iface_class);
		return;
	}

	memset(&m, 0, sizeof(m));
	m.m_type = USB_ANNOUCE_DEV;
	m.USB_DEV_ID = (long)dev->slot;
	m.USB_INTERFACES = (long)dev->interfaces;

	for (i = 0; i < nclients; i++) {
		if (strcmp(clients[i].name, want) != 0)
			continue;

		log_info(&xhci_log, "announcing device %u (class %u/%u, "
		    "interfaces 0x%x) to \"%s\"\n", dev->slot, dev->class,
		    dev->iface_class, dev->interfaces, clients[i].name);

		if (asynsend3(clients[i].ep, &m, AMF_NOREPLY) != OK)
			log_warn(&xhci_log, "the announcement to %d did not "
			    "go\n", clients[i].ep);
		else
			dev->announced = 1;
	}

	if (!dev->announced)
		log_info(&xhci_log, "device %u wants \"%s\", which is not "
		    "running; it will be announced when it starts\n",
		    dev->slot, want);
}

/*
 * Which way a transfer goes.
 *
 * This deserves its own function and this comment, because the tree says
 * it twice and says it differently.  <minix/usb.h>, the header of the very
 * structure being read here, declares USB_IN as 0 and USB_OUT as 1.
 * <ddekit/usb.h>, which every client of this protocol actually uses,
 * declares DDEKIT_USB_IN as 1 and DDEKIT_USB_OUT as 0 - and the client
 * library copies the field across without translating it.  So the value
 * that travels means the opposite of what the header next to it says.
 *
 * The board said so plainly: the mass storage driver's first transfer went
 * to endpoint 1, which on that device is an OUT endpoint, and arrived here
 * asking for endpoint 1 IN.  Reading it the way the sender writes it is
 * the only choice that works; naming the confusion is the least this can
 * do about it.
 */
static int
urb_dir_in(const struct usb_urb *urb)
{
	return urb->direction == 1;
}

/*
 * Run one URB.  Control and bulk; interrupt endpoints go the same way as
 * bulk, and isochronous ones say what they are and are refused.
 */
static int
urb_run(struct usb_urb *urb)
{
	struct usb_ctrlrequest *req;
	struct xhci_device *dev;
	unsigned actual = 0;
	uint16_t length;
	int r;

	if ((dev = xhci_device_by_id((unsigned)urb->dev_id)) == NULL) {
		log_warn(&xhci_log, "a request for device %d, which this "
		    "driver does not have\n", urb->dev_id);
		return ENODEV;
	}

	/*
	 * Anything that is not a control transfer goes to an endpoint of
	 * its own, which the device told us about when it was configured.
	 * An endpoint nobody configured is an error worth naming: it means
	 * the client is talking about a different device than it thinks.
	 */
	if (urb->type == USB_TRANSFER_BLK || urb->type == USB_TRANSFER_INT) {
		struct xhci_ep *ep;
		unsigned len = (unsigned)urb->size;
		int dir_in = urb_dir_in(urb);

		ep = xhci_device_ep(dev, (unsigned)urb->endpoint, dir_in);
		if (ep == NULL) {
			log_warn(&xhci_log, "device %d has no endpoint %d "
			    "%s\n", urb->dev_id, urb->endpoint,
			    dir_in ? "in" : "out");
			return ENODEV;
		}

		if (len > XHCI_DEV_BUF)
			return EINVAL;

		if (!ep->dir_in && len != 0)
			memcpy((void *)dev->buf, urb->buffer, len);

		r = xhci_transfer(dev, ep, len, &actual);
		if (r != OK) {
			urb->status = r;
			urb->actual_length = 0;
			return r;
		}

		if (ep->dir_in && actual != 0)
			memcpy(urb->buffer, (void *)dev->buf, actual);

		urb->status = 0;
		urb->actual_length = actual;
		return OK;
	}

	if (urb->type != USB_TRANSFER_CTL) {
		log_warn(&xhci_log, "an isochronous transfer was asked for, "
		    "and this driver keeps no schedule for one\n");
		return ENOSYS;
	}

	req = (struct usb_ctrlrequest *)urb->setup_packet;
	length = req->wLength;

	if (length > urb->size)
		length = (uint16_t)urb->size;
	if (length > XHCI_PAGE)
		return EINVAL;

	/* What the client wants written goes out to the device's buffer. */
	if (length != 0 && !(req->bRequestType & USB_REQ_DIR_IN))
		memcpy((void *)dev->buf, urb->buffer, length);

	r = xhci_control(dev, req->bRequestType, req->bRequest, req->wValue,
	    req->wIndex, length, &actual);

	if (r != OK) {
		urb->status = r;
		urb->actual_length = 0;
		return r;
	}

	if (length != 0 && (req->bRequestType & USB_REQ_DIR_IN))
		memcpy(urb->buffer, (void *)dev->buf, actual);

	urb->status = 0;
	urb->actual_length = actual;
	return OK;
}

static void
send_urb(message *m)
{
	struct usb_urb *urb = (struct usb_urb *)urbmem;
	cp_grant_id_t gid = (cp_grant_id_t)m->USB_GRANT_ID;
	size_t size = (size_t)m->USB_GRANT_SIZE;
	message done;
	unsigned id;
	int r;

	if (size > sizeof(urbmem) - URB_SKIP) {
		log_warn(&xhci_log, "a request block of %u bytes, which is "
		    "more than this driver holds\n", (unsigned)size);
		reply(m->m_source, EINVAL, 0);
		return;
	}

	memset(urbmem, 0, sizeof(urbmem));

	if ((r = sys_safecopyfrom(m->m_source, gid, 0,
	    (vir_bytes)urbmem + URB_SKIP, size)) != OK) {
		log_warn(&xhci_log, "cannot read the request block from %d: "
		    "%d\n", m->m_source, r);
		reply(m->m_source, EINVAL, 0);
		return;
	}

	id = next_urb_id++;
	if (next_urb_id == 0)
		next_urb_id = 1;		/* zero means "no URB" */

	r = urb_run(urb);

	/*
	 * Copy the block back whatever happened: the status and the length
	 * are in it, and a client that is told an operation failed still
	 * reads them.
	 */
	if (sys_safecopyto(m->m_source, gid, 0, (vir_bytes)urbmem + URB_SKIP,
	    size) != OK)
		log_warn(&xhci_log, "cannot write the request block back to "
		    "%d\n", m->m_source);

	/*
	 * The reply says the request was taken, not that it succeeded -
	 * that is what the status inside the block is for, and libusb
	 * panics on a non-zero result here.  A request this driver could
	 * not take at all was refused before it got an identifier.
	 */
	reply(m->m_source, 0, (long)id);

	memset(&done, 0, sizeof(done));
	done.m_type = USB_COMPLETE_URB;
	done.USB_URB_ID = (long)id;
	if (asynsend3(m->m_source, &done, AMF_NOREPLY) != OK)
		log_warn(&xhci_log, "the completion for urb %u did not go\n",
		    id);
}

/*
 * The hub driver saying that something appeared on one of its ports.
 *
 * This is the only way a device behind a hub is ever found: the root
 * ports have registers a driver can read, and everything below them is
 * reached by asking the hub over the wire.  The hub driver does that
 * polling, resets the port itself, and sends this - the speed it saw and
 * the port it saw it on.
 *
 * Which hub it means is not in the message: the client library drops the
 * device when it sends this, so the hub is identified by who is talking.
 * With one hub driver that is exact.  With two it would not be, and the
 * answer then is the same as for announcements - a matchmaker that knows
 * which driver holds which device, which this driver does not have yet.
 */
static void
hub_news(message *m)
{
	struct xhci_device *hub = NULL, *dev = NULL;
	unsigned port = (unsigned)m->USB_INFO_VALUE;
	unsigned type = (unsigned)m->USB_INFO_TYPE;
	unsigned speed, i;

	/* The hub this driver was told about: the one of class 9. */
	for (i = 0; i < XHCI_MAX_PORTS; i++)
		if (xhci.dev[i].slot != 0 && xhci.dev[i].class == 9)
			hub = &xhci.dev[i];

	if (hub == NULL) {
		log_warn(&xhci_log, "news about hub port %u, and no hub is "
		    "known\n", port);
		return;
	}

	/*
	 * The speeds the hub driver reports are its own three constants,
	 * in the order low, full, high; the fourth is a disconnection.
	 * They are turned into the identifiers the controller uses, which
	 * happen to run in a different order - low is 2 and full is 1 -
	 * and a table of three is clearer than arithmetic that happens to
	 * work.
	 */
	switch (type) {
	case 0:	speed = 2; break;		/* low */
	case 1:	speed = 1; break;		/* full */
	case 2:	speed = 3; break;		/* high */
	default:
		log_info(&xhci_log, "hub port %u: device gone\n", port);
		for (i = 0; i < XHCI_MAX_PORTS; i++)
			if (xhci.dev[i].slot != 0 &&
			    xhci.dev[i].parent_slot == hub->slot &&
			    xhci.dev[i].parent_port == port)
				xhci_device_free(&xhci.dev[i]);
		return;
	}

	/* A free slot in this driver's own table. */
	for (i = 0; i < XHCI_MAX_PORTS; i++)
		if (xhci.dev[i].slot == 0) {
			dev = &xhci.dev[i];
			break;
		}

	if (dev == NULL) {
		log_warn(&xhci_log, "no room for another device\n");
		return;
	}

	log_info(&xhci_log, "hub port %u: a device at %s speed\n", port,
	    type == 0 ? "low" : type == 1 ? "full" : "high");

	if (xhci_device_attach_hub(hub, port, speed, dev) != OK) {
		log_warn(&xhci_log, "hub port %u: could not be enumerated\n",
		    port);
		xhci_device_free(dev);
		return;
	}

	xhci_urb_announce(dev);
}

void
xhci_urb_message(message *m)
{
	unsigned i;

	switch (m->m_type) {
	case USB_RQ_INIT:
		client_add(m->m_source, m->USB_RB_INIT_NAME);
		reply(m->m_source, 0, 0);

		/*
		 * A driver that arrives after the device did still has to
		 * hear about it: enumeration happens at start-up and the
		 * drivers are started by hand or by rc afterwards.
		 */
		for (i = 0; i < XHCI_MAX_PORTS; i++)
			if (xhci.dev[i].slot != 0 && !xhci.dev[i].announced)
				xhci_urb_announce(&xhci.dev[i]);
		break;

	case USB_RQ_DEINIT:
		for (i = 0; i < nclients; i++)
			if (clients[i].ep == m->m_source) {
				clients[i] = clients[--nclients];
				break;
			}
		reply(m->m_source, 0, 0);
		break;

	case USB_RQ_SEND_URB:
		send_urb(m);
		break;

	case USB_RQ_CANCEL_URB:
		/*
		 * Nothing can be cancelled: a URB is finished before its
		 * reply is sent, so by the time anyone could ask, there is
		 * nothing outstanding.  Saying so is better than answering
		 * "done" to a question that was never true.
		 */
		reply(m->m_source, EINVAL, 0);
		break;

	case USB_RQ_SEND_INFO:
		hub_news(m);
		reply(m->m_source, 0, 0);
		break;

	default:
		log_warn(&xhci_log, "unexpected request 0x%x from %d\n",
		    m->m_type, m->m_source);
		reply(m->m_source, EINVAL, 0);
		break;
	}
}
