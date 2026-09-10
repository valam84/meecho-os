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
#include <minix/cachectl.h>
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
 * The transfer this driver is in the middle of, if any.
 *
 * A client's request is answered as soon as the transfer is on the ring -
 * "taken", which is all the protocol's reply means - and the completion
 * message follows when the controller says the transfer is done.  Nothing
 * blocks in between.
 *
 * That is the whole of the debt this closes.  The first version ran the
 * transfer to completion before replying, and waited for it by looking at
 * the event ring in a loop.  On the board that cost thirteen and a half
 * milliseconds per transfer, the same for thirty-one bytes as for
 * thirty-two kilobytes - which is what proved it was not the wire but the
 * scheduler: a driver that busy-waits burns its quantum and then does not
 * run again until it is given another one, long after the controller has
 * finished.
 *
 * One at a time is deliberate.  Two would need two buffers and two grants
 * held at once, and buys nothing here: there is one device doing bulk
 * transfers and one hub polling.  A request that arrives while this one is
 * outstanding is put aside and answered next, in order.
 */
static struct {
	int busy;
	endpoint_t client;
	cp_grant_id_t gid;
	size_t gsize;
	unsigned id;
	struct xhci_device *dev;
	struct xhci_ep *ep;		/* NULL for a control transfer */
	unsigned length;
	int dir_in;
	phys_bytes trb;			/* what the event will name */
} out;

int
xhci_urb_busy(void)
{
	return out.busy;
}

static void hub_news(message *m);

/*
 * Where the time goes, in microseconds, because guessing at that is how
 * optimisation goes wrong.
 *
 * The first measurement of this path was 648 KB/s off a high-speed drive,
 * which is a tenth of what the bus does, and there were three candidate
 * explanations at once: the transfers themselves, the copying in and out
 * of the client's grant, and the wait between one request and the next -
 * which is not this driver's time at all but the client's.  So each is
 * counted separately and the totals are printed, and whatever is optimised
 * afterwards is optimised because a number said so.
 */
static struct {
	unsigned urbs;
	unsigned long bytes;
	unsigned long t_total;		/* receipt of request to reply */
	unsigned long t_copyin;		/* reading the grant */
	unsigned long t_copyout;	/* writing it back */
	unsigned long t_run;		/* the transfer itself */
	unsigned long t_gap;		/* between our reply and the next one */
	unsigned long t_memcpy;		/* copying to and from the buffer */
	u64_t last_done;
} urbstat;

static unsigned stat_every;		/* 0: say nothing */

static u64_t
now(void)
{
	u64_t t;

	read_frclock_64(&t);
	return t;
}

static unsigned long
since(u64_t t)
{
	u64_t n;

	read_frclock_64(&n);
	return (unsigned long)frclock_64_to_micros(delta_frclock_64(t, n));
}

void
xhci_urb_stats(unsigned every)
{
	stat_every = every;
}

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
/*
 * Tell one driver about one device.
 *
 * One at a time, and to a named client, because the alternative was tried
 * and the board said what it thought of it: announcing every device to
 * every matching driver whenever anybody said hello re-offered the hub to
 * the hub driver, which panics on being given a second device.  A driver
 * hears about a device once - when it is found, or when the driver arrives
 * afterwards, never both.
 */
static void
announce_to(struct xhci_device *dev, unsigned who)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_type = USB_ANNOUCE_DEV;
	m.USB_DEV_ID = (long)dev->slot;
	m.USB_INTERFACES = (long)dev->interfaces;

	log_info(&xhci_log, "announcing device %u (class %u/%u, interfaces "
	    "0x%x) to \"%s\"\n", dev->slot, dev->class, dev->iface_class,
	    dev->interfaces, clients[who].name);

	if (asynsend3(clients[who].ep, &m, AMF_NOREPLY) != OK)
		log_warn(&xhci_log, "the announcement to %d did not go\n",
		    clients[who].ep);
	else
		dev->announced = 1;
}

void
xhci_urb_announce(struct xhci_device *dev)
{
	const char *want;

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

	for (i = 0; i < nclients; i++)
		if (strcmp(clients[i].name, want) == 0)
			announce_to(dev, i);

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
 * Answer the client that its request was taken.
 *
 * The reply says "accepted", not "succeeded": the outcome lives in the
 * status field inside the request block, and libusb panics on a non-zero
 * result here.  That is somebody else's contract and it is kept, not
 * improved.
 */
static void
accept(endpoint_t client, unsigned id)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_type = USB_REPLY;
	m.USB_RESULT = 0;
	m.USB_URB_ID = (long)id;

	if (ipc_send(client, &m) != OK)
		log_warn(&xhci_log, "cannot answer %d\n", client);
}

/*
 * Finish the outstanding transfer: give the client back its block and tell
 * it the request is complete.
 */
static void
finish(unsigned actual, int status)
{
	struct usb_urb *urb = (struct usb_urb *)urbmem;
	message done;

	if (!out.busy)
		return;

	urb->status = status;
	urb->actual_length = status == OK ? actual : 0;

	/*
	 * And the identifier, which is the client's own bookkeeping and
	 * must not be destroyed by handing the block back.
	 *
	 * This one was worth a day.  The whole request block is copied back
	 * to the client, and it contains a field the client fills in itself
	 * - the identifier of the outstanding request, which it takes from
	 * the reply and writes into its copy.  While the transfer was
	 * synchronous the block travelled back BEFORE the client had
	 * written that field, so the client's write came last and stood.
	 * Now the block travels back at completion, after the client has
	 * written it, and the copy overwrote it with the zero it had when
	 * the request was made.  The client then looked for a request with
	 * the identifier this driver had announced, found a list holding
	 * zero, and said so: "did not find URB with ID 1".
	 */
	urb->urb_id = out.id;

	if (status == OK && out.dir_in && actual != 0) {
		u64_t t = now();

		memcpy(urb->buffer, (void *)out.dev->buf, actual);
		urbstat.t_memcpy += since(t);
	}

	if (out.dir_in && actual != 0) {
		const uint8_t *b = (const uint8_t *)urb->buffer;

		log_debug(&xhci_log, "returned %u of %u: %02x %02x %02x %02x "
		    "%02x %02x %02x %02x\n", actual, (unsigned)urb->size,
		    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
	}

	{
		u64_t t = now();

		if (sys_safecopyto(out.client, out.gid, 0,
		    (vir_bytes)urbmem + URB_SKIP, out.gsize) != OK)
			log_warn(&xhci_log, "cannot write the request block "
			    "back to %d\n", out.client);
		urbstat.t_copyout += since(t);
	}

	memset(&done, 0, sizeof(done));
	done.m_type = USB_COMPLETE_URB;
	done.USB_URB_ID = (long)out.id;
	/*
	 * Sent synchronously, and that is the experiment: the same message
	 * reached the client when it was posted a microsecond after the
	 * reply, and does not now that it is posted milliseconds later.
	 */
	if (ipc_send(out.client, &done) != OK)
		log_warn(&xhci_log, "the completion for urb %u did not go\n",
		    out.id);
	else
		log_debug(&xhci_log, "completion %u sent to %d, "
		    "%u byte(s)\n", out.id, out.client,
		    urb->actual_length);

	urbstat.bytes += urb->actual_length;
	out.busy = 0;

	if (stat_every != 0 && ++urbstat.urbs % stat_every == 0) {
		log_info(&xhci_log, "%u urbs, %lu bytes: copy in %lu us, "
		    "copy out %lu us, memcpy %lu us, on the wire %lu us in "
		    "%lu transfers; interrupts %lu, alarms %lu\n",
		    urbstat.urbs, urbstat.bytes, urbstat.t_copyin,
		    urbstat.t_copyout, urbstat.t_memcpy, xhci_t_wire,
		    xhci_n_wire, xhci_n_irq, xhci_n_alarm);
		memset(&urbstat, 0, sizeof(urbstat));
		xhci_t_cache = xhci_n_cache = 0;
		xhci_t_poll = xhci_n_poll = 0;
		xhci_t_setup = xhci_t_wire = xhci_n_wire = 0;
		xhci_t_small = xhci_n_small = 0;
		xhci_n_irq = xhci_n_alarm = 0;
	}
}

/*
 * A transfer event off the ring: ours, or somebody else's.
 *
 * "Somebody else's" is not hypothetical - enumeration runs control
 * transfers of its own and waits for them - so the event is matched by the
 * address of the TRB it names, which is what that field is for.
 */
int
xhci_urb_transfer_event(const struct xhci_trb *ev)
{
	unsigned cc, residue, actual;

	log_debug(&xhci_log, "event at 0x%08x, waiting for 0x%08x "
	    "(busy %d)\n", ev->p0, (uint32_t)out.trb, out.busy);

	if (!out.busy || ev->p0 != (uint32_t)out.trb)
		return 0;

	cc = XHCI_CC_OF(ev->status);
	residue = XHCI_EVENT_LENGTH(ev->status);
	actual = residue <= out.length ? out.length - residue : 0;

	if (out.dir_in)
		xhci_cache(CACHE_INVALIDATE, (void *)out.dev->buf,
		    out.ep != NULL ? XHCI_DEV_BUF : XHCI_PAGE,
		    "a transfer buffer");

	if (cc != XHCI_CC_SUCCESS && cc != 13 /* short packet */) {
		log_warn(&xhci_log, "a %u-byte transfer for %d completed "
		    "with %u\n", out.length, out.client, cc);
		finish(0, EIO);
	} else
		finish(actual, OK);

	sys_setalarm(0, 0);
	return 1;
}

/*
 * The deadline.  A transfer that never completes must not take the driver
 * with it: the client is told the request failed and the driver goes on.
 */
void
xhci_urb_tick(void)
{
	uint32_t iman, usbsts;

	if (!out.busy)
		return;

	/*
	 * Look at the ring before giving up.  If the transfer is sitting
	 * there finished, then the transfer worked and the interrupt did
	 * not - a different fault entirely, and one this says out loud
	 * instead of reporting a failed transfer to the client.
	 */
	(void)xhci_events_drain(0, NULL, 0);
	if (!out.busy) {
		log_warn(&xhci_log, "so far: %lu interrupt(s), %lu alarm(s)\n",
		    xhci_n_irq, xhci_n_alarm);
		log_warn(&xhci_log, "the transfer had finished and nobody "
		    "was told: the event was on the ring, the interrupt was "
		    "not\n");
		/* and fall through to the register dump below */
	}

	iman = xhci_rd(xhci.regs, xhci.rtsoff + XHCI_IR(0) + XHCI_IR_IMAN);
	usbsts = xhci_rd(xhci.regs, xhci.caplength + XHCI_USBSTS);

	log_warn(&xhci_log, "so far: %lu interrupt(s), %lu alarm(s)\n",
	    xhci_n_irq, xhci_n_alarm);
	log_warn(&xhci_log, "a %u-byte transfer for %d did not complete "
	    "in time; IMAN 0x%08x (pending %d, enabled %d), USBSTS "
	    "0x%08x (event %d), line %d\n", out.length, out.client,
	    iman, !!(iman & XHCI_IMAN_IP), !!(iman & XHCI_IMAN_IE),
	    usbsts, !!(usbsts & XHCI_USBSTS_EINT), xhci.irq_line);

	finish(0, EIO);
}

static void
send_urb(message *m)
{
	struct usb_urb *urb = (struct usb_urb *)urbmem;
	cp_grant_id_t gid = (cp_grant_id_t)m->USB_GRANT_ID;
	size_t size = (size_t)m->USB_GRANT_SIZE;
	struct usb_ctrlrequest *req;
	struct xhci_device *dev;
	struct xhci_ep *ep = NULL;
	unsigned id, length;
	u64_t t_mark;
	int dir_in, r;

	/*
	 * One at a time.  A request that arrives while a transfer is
	 * outstanding is put aside; the main loop brings it back when the
	 * transfer completes, and the client waits in its sendrec exactly
	 * as it would have.
	 */
	if (out.busy) {
		xhci_defer(m, 0);
		return;
	}

	if (size > sizeof(urbmem) - URB_SKIP) {
		log_warn(&xhci_log, "a request block of %u bytes, which is "
		    "more than this driver holds\n", (unsigned)size);
		reply(m->m_source, EINVAL, 0);
		return;
	}

	memset(urbmem, 0, sizeof(urbmem));

	t_mark = now();
	if ((r = sys_safecopyfrom(m->m_source, gid, 0,
	    (vir_bytes)urbmem + URB_SKIP, size)) != OK) {
		log_warn(&xhci_log, "cannot read the request block from %d: "
		    "%d\n", m->m_source, r);
		reply(m->m_source, EINVAL, 0);
		return;
	}
	urbstat.t_copyin += since(t_mark);

	if ((dev = xhci_device_by_id((unsigned)urb->dev_id)) == NULL) {
		log_warn(&xhci_log, "a request for device %d, which this "
		    "driver does not have\n", urb->dev_id);
		reply(m->m_source, EINVAL, 0);
		return;
	}

	id = next_urb_id++;
	if (next_urb_id == 0)
		next_urb_id = 1;		/* zero means "no URB" */

	out.busy = 1;
	out.client = m->m_source;
	out.gid = gid;
	out.gsize = size;
	out.id = id;
	out.dev = dev;
	out.ep = NULL;

	switch (urb->type) {
	case USB_TRANSFER_BLK:
	case USB_TRANSFER_INT:
		ep = xhci_device_ep(dev, (unsigned)urb->endpoint,
		    urb_dir_in(urb));
		if (ep == NULL) {
			log_warn(&xhci_log, "device %d has no endpoint %d "
			    "%s\n", urb->dev_id, urb->endpoint,
			    urb_dir_in(urb) ? "in" : "out");
			out.busy = 0;
			reply(m->m_source, EINVAL, 0);
			return;
		}

		length = (unsigned)urb->size;
		if (length > XHCI_DEV_BUF) {
			out.busy = 0;
			reply(m->m_source, EINVAL, 0);
			return;
		}

		if (!ep->dir_in && length != 0) {
			t_mark = now();
			memcpy((void *)dev->buf, urb->buffer, length);
			urbstat.t_memcpy += since(t_mark);
		}

		out.ep = ep;
		out.dir_in = ep->dir_in;
		out.length = length;
		out.trb = xhci_transfer_start(dev, ep, length);
		break;

	case USB_TRANSFER_CTL:
		req = (struct usb_ctrlrequest *)urb->setup_packet;
		length = req->wLength;
		if (length > urb->size)
			length = (unsigned)urb->size;
		if (length > XHCI_PAGE) {
			out.busy = 0;
			reply(m->m_source, EINVAL, 0);
			return;
		}

		dir_in = (req->bRequestType & USB_REQ_DIR_IN) != 0;

		if (length != 0 && !dir_in)
			memcpy((void *)dev->buf, urb->buffer, length);

		out.dir_in = dir_in;
		out.length = length;
		out.trb = xhci_control_start(dev, req->bRequestType,
		    req->bRequest, req->wValue, req->wIndex,
		    (uint16_t)length);
		if (out.trb != 0)
			xhci_doorbell(dev, XHCI_DB_EP0);
		break;

	default:
		log_warn(&xhci_log, "an isochronous transfer was asked for, "
		    "and this driver keeps no schedule for one\n");
		out.busy = 0;
		reply(m->m_source, EINVAL, 0);
		return;
	}

	if (out.trb == 0) {
		out.busy = 0;
		reply(m->m_source, EINVAL, 0);
		return;
	}

	/*
	 * A deadline, so that a transfer the controller never reports does
	 * not take the driver with it.  Five seconds is far longer than any
	 * of these take and short enough that a hung device is noticed.
	 */
	sys_setalarm(micros_to_ticks(5000000), 0);

	accept(m->m_source, id);
}

/*
 * The hub driver saying that something appeared on one of its ports.
 *
 * This is the only way a device behind a hub is ever found: the root ports
 * have registers a driver can read, and everything below them is reached by
 * asking the hub over the wire.  The hub driver does that polling, resets
 * the port itself, and sends this - the speed it saw and the port it saw it
 * on.
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
	 * The speeds the hub driver reports are its own three constants, in
	 * the order low, full, high; the fourth is a disconnection.  They
	 * are turned into the identifiers the controller uses, which happen
	 * to run in a different order - low is 2 and full is 1 - and a
	 * table of three is clearer than arithmetic that happens to work.
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
	log_debug(&xhci_log, "message 0x%x from %d\n", m->m_type,
	    m->m_source);

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
		/*
		 * Every device this driver has, announced to whoever just
		 * arrived - not only the ones nobody has been told about.
		 * A driver that crashed and was restarted is a new listener
		 * with no devices, and skipping it because some earlier
		 * instance had been told leaves it running and idle for
		 * ever.  That is not hypothetical: it is how this driver
		 * spent a run watching a storage driver assert on a device
		 * it had never been given.
		 */
		for (i = 0; i < XHCI_MAX_PORTS; i++) {
			const char *want;

			if (xhci.dev[i].slot == 0)
				continue;

			want = driver_for(&xhci.dev[i]);
			if (want != NULL &&
			    strcmp(clients[nclients - 1].name, want) == 0)
				announce_to(&xhci.dev[i], nclients - 1);
		}
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
