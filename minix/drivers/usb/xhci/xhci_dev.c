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
	w[0] = XHCI_SLOT_ROUTE(dev->route) | XHCI_SLOT_SPEED(dev->speed) |
	    XHCI_SLOT_ENTRIES(1);
	w[1] = XHCI_SLOT_RHPORT(dev->port + 1);

	/*
	 * A low- or full-speed device behind a high-speed hub does not
	 * talk to the controller directly: the hub translates for it, and
	 * the controller has to be told which hub and which of its ports
	 * is doing the translating.  Getting this wrong for a slow device
	 * is not a slow device, it is one that never answers.  The flash
	 * drive this was first run against is high speed and needs none of
	 * it - which is exactly why it is written now, while the case that
	 * needs it is still hypothetical and cheap to reason about.
	 */
	if (dev->parent_slot != 0 && dev->speed != 3 && dev->speed < 4)
		w[2] = XHCI_SLOT_TT_SLOT(dev->parent_slot) |
		    XHCI_SLOT_TT_PORT(dev->parent_port);

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
/*
 * The transfer descriptor as the party waiting for it needs to see it.
 *
 * A TD is one to three TRBs, and the controller reports on it by naming a
 * TRB - not necessarily the last one.  The last, the one with IOC, is
 * named when the TD is through.  But a device that answers with less
 * than was asked for produces an event on the TRB the short packet
 * landed in, and THAT event is the one that carries how much arrived;
 * the event on the last TRB then says nothing was left over, because
 * nothing was asked of it.  A waiter that recognises only the last TRB
 * therefore sees every short answer as a full one.
 *
 * The synchronous path knew this and kept the first event of the wanted
 * kind.  The asynchronous path matched on the status TRB alone, and on
 * the board the mass storage driver asked for 128 bytes of a 32-byte
 * configuration descriptor, was told it got 128, said "Invalid
 * descriptor length", and failed its first open - quietly, with EIO,
 * on every bring-up.  The stand had a mutation for exactly this defect
 * and caught it in the synchronous path, where the defect no longer
 * was.  So the matching now lives here, in a file the stand builds,
 * and is written down once for both paths.
 */
static void
td_reset(struct xhci_td *td, unsigned length)
{
	if (td == NULL)
		return;
	memset(td, 0, sizeof(*td));
	td->length = length;
	td->actual = length;
}

static void
td_add(struct xhci_td *td, phys_bytes trb, unsigned off, unsigned len)
{
	if (td == NULL || td->n >= XHCI_TD_MAX_TRBS)
		return;
	td->trb[td->n] = trb;
	td->off[td->n] = off;
	td->len[td->n] = len;
	td->n++;
}

/*
 * An event against a TD.  Answers 0 when the event names none of the
 * TD's TRBs, 1 when it does and the TD is still running, 2 when the TD
 * is done - after which *actual and *cc say how it went.
 *
 * Done means: the event names the TD's last TRB, or it carries a
 * completion code that is neither success nor short packet, since an
 * endpoint that halted on the data stage will never reach the status
 * stage and no further event is coming.
 */
int
xhci_td_event(struct xhci_td *td, const struct xhci_trb *ev,
	unsigned *actual, unsigned *cc)
{
	unsigned i, residue, code;

	if (td == NULL || td->n == 0)
		return 0;

	for (i = 0; i < td->n; i++)
		if ((uint32_t)td->trb[i] == ev->p0)
			break;
	if (i == td->n)
		return 0;

	code = XHCI_CC_OF(ev->status);
	residue = XHCI_EVENT_LENGTH(ev->status);

	/*
	 * A TRB that carried data says how much of it arrived.  Once a
	 * short packet has been reported the answer is fixed: the TRBs
	 * after it are retired without transferring, and their events
	 * report their whole length as left over, which is true of them
	 * and false of the TD.
	 */
	if (td->len[i] != 0 && !td->short_seen) {
		if (residue > td->len[i])
			residue = td->len[i];
		td->actual = td->off[i] + td->len[i] - residue;
	}
	if (code == XHCI_CC_SHORT_PACKET)
		td->short_seen = 1;

	if (i != td->n - 1 && (code == XHCI_CC_SUCCESS ||
	    code == XHCI_CC_SHORT_PACKET))
		return 1;

	if (actual != NULL)
		*actual = td->actual;
	if (cc != NULL)
		*cc = code;
	return 2;
}

/*
 * Put a control transfer on the endpoint's ring, without waiting.
 * Answers the address of the status-stage TRB - the one the completion
 * event names when the TD runs to its end - and, when asked, fills in the
 * TD so that the event on a short data stage is recognised too.
 */
phys_bytes
xhci_control_start(struct xhci_device *dev, uint8_t request_type,
	uint8_t request, uint16_t value, uint16_t index, uint16_t length,
	struct xhci_td *td)
{
	int dir_in = (request_type & USB_REQ_DIR_IN) != 0;
	phys_bytes where;

	td_reset(td, length);

	if (length > XHCI_PAGE)
		return 0;

	if (dir_in)
		memset((void *)dev->buf, 0, length);
	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)dev->buf, XHCI_PAGE,
	    "a transfer buffer");

	where = xhci_ring_push(&dev->ep0,
	    (uint32_t)request_type | ((uint32_t)request << 8) |
	    ((uint32_t)value << 16),
	    (uint32_t)index | ((uint32_t)length << 16),
	    8, XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT |
	    (length == 0 ? XHCI_TRB_TRT_NONE :
	    (dir_in ? XHCI_TRB_TRT_IN : XHCI_TRB_TRT_OUT)));
	td_add(td, where, 0, 0);

	if (length != 0) {
		where = xhci_ring_push(&dev->ep0, (uint32_t)dev->buf_phys, 0,
		    length, XHCI_TRB_TYPE(XHCI_TRB_DATA) | XHCI_TRB_ISP |
		    (dir_in ? XHCI_TRB_DIR_IN : 0));
		td_add(td, where, 0, length);
	}

	where = xhci_ring_push(&dev->ep0, 0, 0, 0,
	    XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC |
	    (length != 0 && dir_in ? 0 : XHCI_TRB_DIR_IN));
	td_add(td, where, length, 0);

	return where;
}

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

	/*
	 * Data stage, when there is data - and it asks to be told about a
	 * short packet.
	 *
	 * Without that bit a device that answers with less than was asked
	 * for produces no event of its own: the transfer runs on to the
	 * status stage, whose event says nothing was left over, and the
	 * driver reports the full length as transferred.  Which is exactly
	 * what happened on the board - the mass storage driver asked for
	 * 128 bytes of a 32-byte configuration descriptor, was told it got
	 * 128, and refused the device as having an invalid descriptor.
	 * Asking a device for more than it has is not an error case, it is
	 * how descriptors are read.
	 */
	if (length != 0)
		xhci_ring_push(&dev->ep0, (uint32_t)dev->buf_phys, 0, length,
		    XHCI_TRB_TYPE(XHCI_TRB_DATA) | XHCI_TRB_ISP |
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
 * One endpoint out of a descriptor, and the ring it will be driven from.
 *
 * Only bulk and interrupt endpoints are taken.  Isochronous ones need a
 * schedule this driver does not keep, and saying so here is better than
 * configuring one and failing at the first transfer.
 */
static void
add_endpoint(struct xhci_device *dev, const uint8_t *d)
{
	unsigned num = d[2] & 0x0f;
	int dir_in = (d[2] & 0x80) != 0;
	unsigned attr = d[3] & 0x3;
	struct xhci_ep *ep;

	if (dev->neps >= XHCI_MAX_EPS || num == 0)
		return;

	if (attr == 1) {
		log_info(&xhci_log, "port %u: endpoint %u is isochronous and "
		    "is left alone\n", dev->port + 1, num);
		return;
	}

	ep = &dev->ep[dev->neps];
	memset(ep, 0, sizeof(*ep));

	ep->num = num;
	ep->dir_in = dir_in;
	ep->dci = num * 2 + (dir_in ? 1 : 0);
	ep->max_packet = d[4] | ((unsigned)d[5] << 8);
	ep->interval = d[6];

	if (attr == 2)
		ep->type = dir_in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
	else
		ep->type = dir_in ? XHCI_EP_TYPE_INTR_IN : XHCI_EP_TYPE_INTR_OUT;

	if (xhci_ring_setup(&ep->ring, "a transfer ring") != OK)
		return;

	dev->neps++;

	log_info(&xhci_log, "port %u: endpoint %u %s, %s, %u bytes "
	    "(context %u)\n", dev->port + 1, num, dir_in ? "in" : "out",
	    attr == 2 ? "bulk" : "interrupt", ep->max_packet, ep->dci);
}

/*
 * Tell the controller about all of them at once.
 *
 * One Configure Endpoint command carries an input context with a bit set
 * per endpoint being added, plus the slot context, whose "context
 * entries" field has to name the highest one used - the controller reads
 * exactly that many and no more, so an endpoint beyond it is configured
 * and then invisible.
 */
static int
configure_endpoints(struct xhci_device *dev)
{
	unsigned i, add = XHCI_INPUT_ADD_SLOT, last = 1;
	uint32_t *w;
	int r;

	memset((void *)dev->in_ctx, 0, XHCI_PAGE);

	for (i = 0; i < dev->neps; i++) {
		struct xhci_ep *ep = &dev->ep[i];

		add |= 1U << ep->dci;
		if (ep->dci > last)
			last = ep->dci;

		w = ctx_word(dev->in_ctx, ep->dci + 1, 0);
		w[0] = (ep->type == XHCI_EP_TYPE_INTR_IN ||
		    ep->type == XHCI_EP_TYPE_INTR_OUT) ?
		    XHCI_EP_INTERVAL(ep->interval) : 0;
		w[1] = XHCI_EP_CERR(3) | XHCI_EP_TYPE(ep->type) |
		    XHCI_EP_MAXPACKET(ep->max_packet);
		w[2] = (uint32_t)ep->ring.p | XHCI_EP_DCS;
		w[3] = 0;
		w[4] = XHCI_EP_AVG_TRB(ep->max_packet);
	}

	*ctx_word(dev->in_ctx, IN_CTRL, 0) = 0;
	*ctx_word(dev->in_ctx, IN_CTRL, 1) = add;

	/* The slot context again, with the endpoint count it now has. */
	w = ctx_word(dev->in_ctx, IN_SLOT, 0);
	w[0] = XHCI_SLOT_ROUTE(dev->route) | XHCI_SLOT_SPEED(dev->speed) |
	    XHCI_SLOT_ENTRIES(last);
	w[1] = XHCI_SLOT_RHPORT(dev->port + 1);
	if (dev->parent_slot != 0 && dev->speed != 3 && dev->speed < 4)
		w[2] = XHCI_SLOT_TT_SLOT(dev->parent_slot) |
		    XHCI_SLOT_TT_PORT(dev->parent_port);

	xhci_cache(CACHE_CLEAN, (void *)dev->in_ctx, XHCI_PAGE,
	    "an input context");

	if ((r = xhci_cmd((uint32_t)dev->in_ctx_phys, 0, 0,
	    XHCI_TRB_TYPE(XHCI_TRB_CONFIGURE_EP) |
	    XHCI_TRB_SLOT_ID(dev->slot), NULL)) != OK) {
		log_warn(&xhci_log, "port %u: the controller would not "
		    "configure %u endpoint(s)\n", dev->port + 1, dev->neps);
		return r;
	}

	log_info(&xhci_log, "port %u: %u endpoint(s) configured, contexts "
	    "up to %u\n", dev->port + 1, dev->neps, last);
	return OK;
}

struct xhci_ep *
xhci_device_ep(struct xhci_device *dev, unsigned num, int dir_in)
{
	unsigned i;

	for (i = 0; i < dev->neps; i++)
		if (dev->ep[i].num == num && dev->ep[i].dir_in == !!dir_in)
			return &dev->ep[i];
	return NULL;
}

/*
 * A transfer on an endpoint that is not the control one: one or more
 * Normal TRBs and one event at the end.
 *
 * More than one because of a rule that is easy to miss and impossible to
 * see the effect of by reading: a transfer TRB's buffer may not cross a
 * 64 KiB boundary.  The buffer here is one allocation of that size, so it
 * crosses at most one such boundary, but "at most one" is not "none", and
 * a driver that assumes none works until the allocator hands it the wrong
 * page.  So the range is split at the boundary and the pieces are chained.
 */
/*
 * Put a transfer on an endpoint's ring and ring the doorbell, without
 * waiting for it.  Answers the address of the last TRB, which is what the
 * completion event will name - that is how the answer is matched to the
 * question when several are outstanding.
 */
phys_bytes
xhci_transfer_start(struct xhci_device *dev, struct xhci_ep *ep, size_t length,
	struct xhci_td *td)
{
	phys_bytes phys = dev->buf_phys, last = 0;
	size_t left = length, done = 0;

	td_reset(td, length);

	if (length > XHCI_DEV_BUF)
		return 0;

	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)dev->buf, XHCI_DEV_BUF,
	    "a transfer buffer");

	do {
		size_t chunk = 0x10000 - (phys & 0xffff);
		uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL);

		if (chunk > left)
			chunk = left;
		left -= chunk;

		if (left != 0)
			control |= XHCI_TRB_CH;
		else
			control |= XHCI_TRB_IOC;

		if (ep->dir_in)
			control |= XHCI_TRB_ISP;

		last = xhci_ring_push(&ep->ring, (uint32_t)phys, 0,
		    (uint32_t)chunk, control);
		td_add(td, last, done, chunk);

		phys += chunk;
		done += chunk;
	} while (left != 0);

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev->slot), ep->dci);

	return last;
}

int
xhci_transfer(struct xhci_device *dev, struct xhci_ep *ep, size_t length,
	unsigned *actual)
{
	struct xhci_trb ev;
	phys_bytes phys = dev->buf_phys;
	size_t left = length;
	unsigned cc, residue;
	u64_t t_enter, t_ring;

	read_frclock_64(&t_enter);

	if (actual != NULL)
		*actual = 0;
	if (length > XHCI_DEV_BUF)
		return EINVAL;

	/*
	 * Hand the buffer over.  Clean-invalidate covers both directions:
	 * what the caller wrote has to reach memory before the controller
	 * reads it, and none of the driver's lines may be left dirty over
	 * what the controller is about to write.
	 */
	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)dev->buf, XHCI_DEV_BUF,
	    "a transfer buffer");

	do {
		size_t chunk = 0x10000 - (phys & 0xffff);
		uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL);

		if (chunk > left)
			chunk = left;
		left -= chunk;

		/*
		 * Every piece but the last is chained to the next; the last
		 * one asks for the event.  A chained TRB is not a transfer
		 * of its own - the controller reports the lot once.
		 */
		if (left != 0)
			control |= XHCI_TRB_CH;
		else
			control |= XHCI_TRB_IOC;

		if (ep->dir_in)
			control |= XHCI_TRB_ISP;

		xhci_ring_push(&ep->ring, (uint32_t)phys, 0,
		    (uint32_t)chunk, control);

		phys += chunk;
	} while (left != 0);

	read_frclock_64(&t_ring);
	xhci_t_setup += (unsigned long)frclock_64_to_micros(
	    delta_frclock_64(t_enter, t_ring));

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev->slot), ep->dci);

	memset(&ev, 0, sizeof(ev));
	if (xhci_events_drain(5000000, &ev, XHCI_TRB_TRANSFER_EVENT) == 0 ||
	    XHCI_TRB_TYPE_OF(ev.control) != XHCI_TRB_TRANSFER_EVENT) {
		log_warn(&xhci_log, "no event for a %u-byte transfer on "
		    "endpoint %u\n", (unsigned)length, ep->num);
		return EIO;
	}

	{
		u64_t t_done;
		unsigned long us;

		read_frclock_64(&t_done);
		us = (unsigned long)frclock_64_to_micros(
		    delta_frclock_64(t_ring, t_done));

		/*
		 * Split by size: a transfer of a few dozen bytes and one of
		 * thirty-two kilobytes cost the same if the cost is a fixed
		 * overhead, and differ by fifty if it is the data.
		 */
		if (length <= 512) {
			xhci_t_small += us;
			xhci_n_small++;
		} else {
			xhci_t_wire += us;
			xhci_n_wire++;
		}
	}

	cc = XHCI_CC_OF(ev.status);
	residue = XHCI_EVENT_LENGTH(ev.status);

	if (cc != XHCI_CC_SUCCESS && cc != 13 /* short packet */) {
		log_warn(&xhci_log, "a %u-byte transfer on endpoint %u "
		    "completed with %u\n", (unsigned)length, ep->num, cc);
		return EIO;
	}

	if (ep->dir_in)
		xhci_cache(CACHE_INVALIDATE, (void *)dev->buf, XHCI_DEV_BUF,
		    "a transfer buffer");

	if (actual != NULL)
		*actual = (unsigned)length - residue;

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
	dev->neps = 0;

	/*
	 * Walk the descriptors that follow, which are a chain of
	 * length-and-type records: note which interface numbers are there
	 * - that set is what the client drivers are handed - and collect
	 * the endpoints, which is what the controller has to be told about
	 * before anything can be sent to them.
	 */
	for (off = 0; off + 2 <= actual && cfg[off] != 0; off += cfg[off]) {
		if (cfg[off + 1] == 4 && off + 6 <= actual) {	/* interface */
			dev->interfaces |= 1U << (cfg[off + 2] & 0x1f);

			/*
			 * And what the interface says it is.  A device may
			 * leave its own class field zero and declare itself
			 * here instead - the flash drive does exactly that,
			 * class 0 in the device descriptor and class 8 in
			 * the interface - so a driver is chosen by looking
			 * at both.
			 */
			if (dev->iface_class == 0)
				dev->iface_class = cfg[off + 5];
		}

		if (cfg[off + 1] == 5 && off + 7 <= actual)	/* endpoint */
			add_endpoint(dev, cfg + off);
	}

	if (dev->neps != 0 && (r = configure_endpoints(dev)) != OK)
		return r;

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
static int
device_attach(struct xhci_device *dev, unsigned port, unsigned speed,
	unsigned route, unsigned tier, unsigned parent_slot,
	unsigned parent_port)
{
	struct usb_device_descriptor *desc;
	struct xhci_trb ev;
	uint64_t *dcbaa;
	void *v;
	int r;

	memset(dev, 0, sizeof(*dev));
	dev->port = port;
	dev->speed = speed;
	dev->route = route;
	dev->tier = tier;
	dev->parent_slot = parent_slot;
	dev->parent_port = parent_port;

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

	if ((v = xhci_alloc_dma(XHCI_DEV_BUF, &dev->buf_phys, "a transfer "
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

	log_info(&xhci_log, "root port %u route 0x%x: slot %u, %s speed, "
	    "addressed\n", dev->port + 1, dev->route, dev->slot,
	    speed_word(dev->speed));

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

/*
 * A device on a root port: the speed comes from the port's own register,
 * and there is no hub above it, so no route and no translator.
 */
int
xhci_device_attach(unsigned port, struct xhci_device *dev)
{
	uint32_t portsc = xhci_rd(xhci.regs, xhci.caplength +
	    XHCI_PORTSC(port));

	return device_attach(dev, port, XHCI_PORTSC_SPEED(portsc), 0, 0, 0, 0);
}

void
xhci_device_free(struct xhci_device *dev)
{
	unsigned i;

	if (dev->in_ctx != 0)
		free_contig((void *)dev->in_ctx, XHCI_PAGE);
	if (dev->dev_ctx != 0)
		free_contig((void *)dev->dev_ctx, XHCI_PAGE);
	if (dev->buf != 0)
		free_contig((void *)dev->buf, XHCI_DEV_BUF);
	xhci_ring_free(&dev->ep0);
	for (i = 0; i < dev->neps; i++)
		xhci_ring_free(&dev->ep[i].ring);
	memset(dev, 0, sizeof(*dev));
}

/*
 * The same device, arriving the other way: behind a hub.
 *
 * Nothing found it by looking at a register.  The hub driver polls its own
 * ports over the wire, resets the one that has something on it, and tells
 * this driver the port number and the speed - so this is the entry point
 * that a hub's news arrives at, and the only difference from a device on a
 * root port is two fields.
 *
 * The route string is the first: four bits per tier, saying which port to
 * take at each hub on the way down.  The controller walks it; the driver
 * never addresses anything.  The second is the root hub port, which stays
 * that of the hub at the top - the whole chain hangs off it.
 */
int
xhci_device_attach_hub(struct xhci_device *hub, unsigned hubport,
	unsigned speed, struct xhci_device *dev)
{
	unsigned route;

	if (hub->tier >= 5) {
		log_warn(&xhci_log, "a hub five tiers down; USB does not go "
		    "that deep\n");
		return EINVAL;
	}

	route = hub->route | ((hubport & 0xf) << (4 * hub->tier));

	return device_attach(dev, hub->port, speed, route, hub->tier + 1,
	    hub->slot, hubport);
}
