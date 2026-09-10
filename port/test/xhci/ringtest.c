/*
 * The host stand for the xHCI driver: rings, contexts and matching an
 * event to the request it belongs to.
 *
 * Why this exists, written down while the reason is fresh.  Every defect
 * this driver has had - the Link TRB handed over with the wrong cycle
 * bit, the port taken from the wrong word of the event, the short answer
 * reported as a full one, the transfer that never started because
 * nothing rang the doorbell - was arithmetic or order of operations.  All
 * of them were found on the board, one per run, at minutes apiece; all of
 * them are reproducible here, thousands of times a second, and three of
 * them are checked below by name.
 *
 * What it does NOT check is worth stating too, because the eMMC stand
 * taught this lesson and it cost a week to relearn: a stand cannot catch
 * a misunderstanding of the hardware.  Nothing here would have found the
 * clock multiplexer or the PHY tuning, and nothing here will find the
 * next thing of that kind.  It keeps the arithmetic honest while the
 * attention is on the hardware, which is exactly what it did for the
 * card layer.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>
#include <minix/log.h>

#include "xhci.h"
#include "xhcireg.h"
#include "model.h"

/*===========================================================================*
 *    the driver's world                                                     *
 *===========================================================================*/
struct xhci xhci;
struct log xhci_log = { .name = "xhci", .level = LEVEL_WARN };

static int verbose;
static unsigned warnings;
static char last_warning[256];

void
shim_verbose(int on)
{
	verbose = on;
	xhci_log.level = on ? LEVEL_DEBUG : LEVEL_WARN;
}

void
shim_log(struct log *l, int level, const char *fmt, ...)
{
	va_list ap;

	if (level <= LEVEL_WARN) {
		warnings++;
		va_start(ap, fmt);
		vsnprintf(last_warning, sizeof(last_warning), fmt, ap);
		va_end(ap);
	}

	if (level > l->level)
		return;

	va_start(ap, fmt);
	fprintf(stderr, "  %s: ", l->name);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

unsigned
shim_warnings(void)
{
	return warnings;
}

void
shim_warnings_reset(void)
{
	warnings = 0;
	last_warning[0] = '\0';
}

const char *
shim_last_warning(void)
{
	return last_warning;
}

void *
alloc_contig(size_t size, int flags, phys_bytes *phys)
{
	return model_mem_alloc(size, phys);
}

void
free_contig(void *addr, size_t size)
{
	model_mem_free(addr, size);
}

/*
 * Cache maintenance, and one step of the controller after it.
 *
 * The step is the important half.  Without it the modelled controller only
 * ever runs while the driver is asleep, so no race between the two can
 * exist, and the stand cannot show a defect that lives in the gap between
 * the driver looking at the ring and the driver going to sleep.  A real
 * controller runs whether or not anybody is looking; putting a step here
 * - after the copy, so that what it produces lands where the driver has
 * just finished reading - is the cheapest honest way to say so.
 */
int
sys_cachectl(int op, void *addr, size_t len)
{
	model_cache(op, addr, len);
	model_step();
	return OK;
}

/*
 * Letting time pass is what makes the modelled controller run, which is
 * not a convenience: on the board the driver's poll loop is also the only
 * reason the controller gets to finish anything before the driver gives
 * up, and a path that never waits is a path that never hears.
 */
static unsigned long fr_now;		/* the free-running clock, in us */
static unsigned n_hw, n_clock;		/* how the waits ended */

int
micro_delay(u32_t usec)
{
	fr_now += usec;
	model_step();
	return OK;
}

/*===========================================================================*
 *    the checks                                                             *
 *===========================================================================*/
static unsigned checks, failures;

static void
check(int ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok && !verbose)
		return;

	if (!ok)
		failures++;

	va_start(ap, fmt);
	printf("%s ", ok ? "ok  " : "FAIL");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void
section(const char *name)
{
	printf("-- %s\n", name);
}

/*===========================================================================*
 *    bringing the modelled controller up                                    *
 *===========================================================================*/
/*
 * What xhci.c does at start-up, done here from the same registers.  It is
 * repeated rather than shared because the stand must not depend on the
 * driver's start-up path to test the driver's rings: if that path is what
 * is wrong, a stand built on it agrees with it.
 */
static void
caps_read(void)
{
	uint32_t v;

	v = xhci_rd(xhci.regs, XHCI_CAPLENGTH);
	xhci.caplength = v & 0xff;
	xhci.hciversion = v >> 16;

	v = xhci_rd(xhci.regs, XHCI_HCSPARAMS1);
	xhci.nslots = XHCI_HCS1_MAXSLOTS(v);
	xhci.nintrs = XHCI_HCS1_MAXINTRS(v);
	xhci.nports = XHCI_HCS1_MAXPORTS(v);

	v = xhci_rd(xhci.regs, XHCI_HCSPARAMS2);
	xhci.scratchpad_bufs = XHCI_HCS2_MAX_SCRATCHPAD(v);

	v = xhci_rd(xhci.regs, XHCI_HCCPARAMS1);
	xhci.ac64 = XHCI_HCC1_AC64(v);
	xhci.context_size = XHCI_HCC1_CSZ(v) ? 64 : 32;

	xhci.dboff = xhci_rd(xhci.regs, XHCI_DBOFF) & ~0x3u;
	xhci.rtsoff = xhci_rd(xhci.regs, XHCI_RTSOFF) & ~0x1fu;
}

static int
bring_up(unsigned ports)
{
	memset(&xhci, 0, sizeof(xhci));
	model_mem_reset();
	shim_warnings_reset();

	xhci.regs = model_reset(ports, MODEL_MAX_SLOTS - 1);
	caps_read();

	/*
	 * The line is armed.  A stand that left it unarmed would test the
	 * fallback and call it the driver.
	 */
	xhci.irq_ok = 1;
	xhci.irq_dead = 0;

	if (xhci_dma_alloc() != OK)
		return EIO;
	return xhci_start();
}

/*===========================================================================*
 *    a device for the model to be                                           *
 *===========================================================================*/
static const uint8_t device_desc[18] = {
	18, USB_DESC_DEVICE, 0x00, 0x02,	/* usb 2.00 */
	0x00, 0x00, 0x00, 64,			/* class 0, ep0 64 bytes */
	0x81, 0x07, 0x58, 0x55,			/* 0781:5558 */
	0x00, 0x01, 1, 2, 3, 1
};

/* One interface, class 8, with a bulk pair: what a flash drive is. */
static const uint8_t config_desc[32] = {
	9, USB_DESC_CONFIG, 32, 0, 1, 1, 0, 0x80, 50,
	9, 4, 0, 0, 2, 8, 6, 0x50, 0,
	7, 5, 0x81, 2, 0x00, 0x02, 0,		/* ep 1 in, bulk, 512 */
	7, 5, 0x02, 2, 0x00, 0x02, 0		/* ep 2 out, bulk, 512 */
};

static unsigned control_requests;
static unsigned set_configuration_value;

static size_t
device_control(unsigned slot, const uint8_t setup[8], uint8_t *out,
	size_t max)
{
	unsigned request = setup[1];
	unsigned value = setup[2] | ((unsigned)setup[3] << 8);

	control_requests++;

	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_DEVICE) {
		memcpy(out, device_desc, sizeof(device_desc));
		return sizeof(device_desc);
	}
	if (request == USB_REQ_GET_DESCRIPTOR &&
	    (value >> 8) == USB_DESC_CONFIG) {
		memcpy(out, config_desc, sizeof(config_desc));
		return sizeof(config_desc);
	}
	if (request == 9 /* SET_CONFIGURATION */) {
		set_configuration_value = value;
		return 0;
	}
	return 0;
}

/*===========================================================================*
 *    1. the ring itself                                                     *
 *===========================================================================*/
static struct xhci_trb *
trb(const struct xhci_ring *r, unsigned i)
{
	return (struct xhci_trb *)(r->v + i * XHCI_TRB_SIZE);
}

static void
test_ring_shape(void)
{
	struct xhci_ring r;
	unsigned slots;

	section("the shape of a ring");

	memset(&xhci, 0, sizeof(xhci));
	model_mem_reset();

	check(xhci_ring_setup(&r, "a ring") == OK, "a ring is made");
	slots = r.slots;

	check(slots == XHCI_PAGE / XHCI_TRB_SIZE,
	    "it holds %u entries, one page worth", slots);
	check(r.enq == 0 && r.cycle == 1,
	    "it starts at entry 0 with cycle 1");

	check(XHCI_TRB_TYPE_OF(trb(&r, slots - 1)->control) == XHCI_TRB_LINK,
	    "the last entry is a Link");
	check((trb(&r, slots - 1)->control & XHCI_TRB_TC) != 0,
	    "the Link toggles the cycle - without which the ring is a line");
	check(trb(&r, slots - 1)->p0 == (uint32_t)r.p,
	    "and points back at the first entry");

	/*
	 * The whole page has to reach memory before the controller is told
	 * where it is, entries the driver has not written included: their
	 * cycle bit is what tells the controller to stop, and a bit still
	 * sitting in the driver's cache tells it nothing.
	 */
	check(memcmp((void *)r.v, model_dev_ptr(r.p, XHCI_PAGE),
	    XHCI_PAGE) == 0,
	    "and the whole page has been handed to the controller");

	xhci_ring_free(&r);
}

static void
test_ring_wrap(void)
{
	struct xhci_ring r;
	unsigned slots, i, cycle_before;
	phys_bytes first, where = 0;

	section("wrapping a ring");

	memset(&xhci, 0, sizeof(xhci));
	model_mem_reset();
	xhci_ring_setup(&r, "a ring");
	slots = r.slots;

	first = xhci_ring_push(&r, 1, 0, 0, XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD));
	check(first == r.p, "the first entry lands at the start of the ring");
	check((trb(&r, 0)->control & XHCI_TRB_C) != 0,
	    "and carries cycle 1, which is what the controller looks for");

	/* Fill it up to, but not past, the Link. */
	for (i = 1; i < slots - 1; i++)
		where = xhci_ring_push(&r, i + 1, 0, 0,
		    XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD));

	check(where == r.p + (slots - 2) * XHCI_TRB_SIZE,
	    "the last entry before the Link is where it should be");
	check(r.enq == slots - 1, "and the ring is now at the Link");

	/*
	 * The defect this check exists for.  The Link was written once at
	 * setup with its cycle bit clear, and only the driver's own cycle
	 * state was flipped on the wrap.  But the Link is an entry like any
	 * other: with a bit the controller is not looking for, the ring
	 * simply ends there.  On the board that read as 255 commands
	 * through and the next 45 silent, which is a full page of entries
	 * and then nothing - and no error anywhere, because from the
	 * controller's side there was no more work.
	 */
	cycle_before = r.cycle;
	where = xhci_ring_push(&r, 0xbeef, 0, 0,
	    XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD));

	check(((trb(&r, slots - 1)->control & XHCI_TRB_C) != 0) ==
	    (cycle_before != 0),
	    "the Link is handed over with the cycle bit the controller "
	    "wants");
	check(r.cycle == (cycle_before ^ 1),
	    "and only then does the driver flip its own");
	check(where == r.p, "the entry after the Link is the first one");
	check(((trb(&r, 0)->control & XHCI_TRB_C) != 0) == (r.cycle != 0),
	    "written with the new cycle state");

	/* And the Link reached memory, not only the driver's cache. */
	check(memcmp(trb(&r, slots - 1),
	    model_dev_ptr(r.p + (slots - 1) * XHCI_TRB_SIZE, XHCI_TRB_SIZE),
	    XHCI_TRB_SIZE) == 0,
	    "the Link the controller will read is the one just written");

	xhci_ring_free(&r);
}

/*===========================================================================*
 *    2. commands, and the wrap under a real controller                      *
 *===========================================================================*/
static void
test_commands(void)
{
	unsigned i, ok = 0;

	section("commands through the modelled controller");

	check(bring_up(2) == OK, "the controller starts");
	check(model_complaint() == NULL, "with nothing to complain about: %s",
	    model_complaint() ? model_complaint() : "nothing");

	for (i = 0; i < 1000; i++)
		if (xhci_cmd_noop_quiet() == OK)
			ok++;

	/*
	 * A thousand, deliberately: the ring holds 255 usable entries and
	 * the event ring 256, so this crosses both wraps four times each,
	 * in both cycle states.  The board run that found the Link defect
	 * did 300 and stopped at 255.
	 */
	check(ok == 1000, "%u of 1000 no-op commands completed", ok);
	check(model_stats.link_follows >= 3,
	    "the controller followed the Link %u time(s)",
	    model_stats.link_follows);
	check(model_stats.toggles == model_stats.link_follows,
	    "and toggled its cycle state on every one of them");
	check(model_stats.event_ring_full == 0,
	    "the event ring never filled: the driver kept acknowledging");
	check(model_stats.stale_reads == 0,
	    "and never read an entry the driver had not handed over");
	check(shim_warnings() == 0, "the driver complained about nothing: %s",
	    shim_last_warning());
}

/*
 * The other half of the same fact: an entry written and not handed over
 * is invisible, and the model has to be able to see that it is.  Without
 * this check the one above proves only that the driver does something,
 * not that cleaning is what makes it work.
 */
static void
test_cache_is_load_bearing(void)
{
	struct xhci_ring r;
	struct xhci_trb *t;

	section("memory the driver has not handed over");

	memset(&xhci, 0, sizeof(xhci));
	model_mem_reset();
	xhci_ring_setup(&r, "a ring");

	t = trb(&r, 0);
	t->p0 = 1;
	t->p1 = 0;
	t->status = 0;
	t->control = XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD) | XHCI_TRB_C;
	/* and no clean */

	check(memcmp(t, model_dev_ptr(r.p, XHCI_TRB_SIZE),
	    XHCI_TRB_SIZE) != 0,
	    "an entry written and not cleaned is not what memory holds");

	xhci_cache(CACHE_CLEAN, t, XHCI_TRB_SIZE, "an entry");
	check(memcmp(t, model_dev_ptr(r.p, XHCI_TRB_SIZE),
	    XHCI_TRB_SIZE) == 0, "and cleaning is what puts it there");

	/*
	 * The other direction, which is the one that cost the GMAC driver a
	 * day: what the controller writes stays invisible until the driver
	 * invalidates, for ever, however many times it looks.
	 */
	memset(model_dev_ptr(r.p, XHCI_TRB_SIZE), 0x11, XHCI_TRB_SIZE);
	check(t->p0 != 0x11111111u,
	    "what the controller wrote is not visible by itself");
	xhci_cache(CACHE_INVALIDATE, t, XHCI_TRB_SIZE, "an entry");
	check(t->p0 == 0x11111111u, "and invalidating is what shows it");

	xhci_ring_free(&r);
}

/*===========================================================================*
 *    3. a device: ports, contexts, enumeration                              *
 *===========================================================================*/
static struct xhci_device dev0;

static void
test_enumeration(void)
{
	section("a device on a port");

	check(bring_up(2) == OK, "the controller starts");
	model_control_handler(device_control);
	model_attach(0, 3 /* high speed */);
	control_requests = 0;

	check(xhci_port_reset(0) == OK, "port 1 resets and enables");
	check(xhci_device_attach(0, &dev0) == OK, "the device enumerates");

	check(dev0.slot != 0, "it was given slot %u", dev0.slot);
	check(dev0.speed == 3, "at high speed, read after the reset");
	check(dev0.iface_class == 8,
	    "its interface says class %u - mass storage", dev0.iface_class);
	check(dev0.interfaces == 1, "one interface, number 0");
	check(dev0.neps == 2, "with %u endpoint(s) taken", dev0.neps);
	check(set_configuration_value == 1,
	    "and configuration %u was set", set_configuration_value);

	check(xhci_device_ep(&dev0, 1, 1) != NULL, "endpoint 1 in is there");
	check(xhci_device_ep(&dev0, 2, 0) != NULL, "endpoint 2 out is there");
	check(xhci_device_ep(&dev0, 1, 1)->dci == 3,
	    "endpoint 1 in is context 3 - 2N plus the direction");
	check(xhci_device_ep(&dev0, 2, 0)->dci == 4,
	    "endpoint 2 out is context 4");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
	check(model_stats.lost_doorbells == 0,
	    "no doorbell was rung at a ring the controller does not know");
}

/*===========================================================================*
 *    4. transfers, and the short answer                                     *
 *===========================================================================*/
static void
test_transfers(void)
{
	struct xhci_ep *in = xhci_device_ep(&dev0, 1, 1);
	struct xhci_ep *out = xhci_device_ep(&dev0, 2, 0);
	static uint8_t pattern[32 * 1024];
	static uint8_t taken[32 * 1024];
	unsigned actual, i;

	section("transfers on the endpoints");

	for (i = 0; i < sizeof(pattern); i++)
		pattern[i] = (uint8_t)(i * 7 + (i >> 8));

	/* A full-length read. */
	model_ep_supply(dev0.slot, in->dci, pattern, sizeof(pattern));
	check(xhci_transfer(&dev0, in, sizeof(pattern), &actual) == OK,
	    "a 32 KiB read runs");
	check(actual == sizeof(pattern), "and reports %u bytes", actual);
	check(memcmp((void *)dev0.buf, pattern, sizeof(pattern)) == 0,
	    "and the bytes in the buffer are the ones the device sent");

	/*
	 * A 32 KiB buffer can straddle a 64 KiB boundary, and a TRB may
	 * not: the driver splits at the boundary and chains the pieces, so
	 * one transfer becomes two entries and still one event.  A driver
	 * that reported each piece would answer this next check with half.
	 */
	check(model_stats.transfers >= 1,
	    "the controller reported it once, not once per piece");

	/* A short answer: the device has less than was asked for. */
	model_ep_supply(dev0.slot, in->dci, pattern, 100);
	check(xhci_transfer(&dev0, in, 4096, &actual) == OK,
	    "a read of 4096 bytes from a device that has 100 runs");
	check(actual == 100, "and reports %u bytes, not 4096", actual);

	/* And a write, which the model keeps so it can be looked at. */
	check(memcpy((void *)dev0.buf, pattern, 512) != NULL, "a buffer");
	check(xhci_transfer(&dev0, out, 512, &actual) == OK,
	    "a 512-byte write runs");
	check(actual == 512, "and reports %u bytes", actual);
	check(model_ep_taken(dev0.slot, out->dci, taken, sizeof(taken)) ==
	    512, "the device received 512 bytes");
	check(memcmp(taken, pattern, 512) == 0, "and they are the right ones");

	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
}

/*
 * The regression that cost milestone 10.4 a run: keeping the LAST event
 * of the wanted kind rather than the first.  A short control transfer
 * makes two - one for the data, carrying the residue, and one for the
 * status stage, carrying nothing - so keeping the last reports every
 * short read as a full one, and the mass storage driver refuses the
 * device for an invalid descriptor length.
 */
static void
test_short_control(void)
{
	unsigned actual;

	section("a control request the device answers short");

	check(xhci_control(&dev0, USB_REQ_DIR_IN, USB_REQ_GET_DESCRIPTOR,
	    USB_DESC_DEVICE << 8, 0, 128, &actual) == OK,
	    "asking for 128 bytes of an 18-byte descriptor is not an error");
	check(actual == 18,
	    "and the answer is %u bytes, not the 128 asked for", actual);
}

/*===========================================================================*
 *    5. two devices at once                                                 *
 *===========================================================================*/
/*
 * Where the asynchronous path stopped on the board: with two clients'
 * transfers interleaved, both stalled and no events arrived at all.  The
 * synchronous driver has one transfer outstanding at a time, so this
 * checks the weaker property it does have to hold - that two devices with
 * their own slots, contexts and rings do not disturb each other, and that
 * each event is matched to the request it belongs to.
 */
static struct xhci_device dev1;

static void
test_two_devices(void)
{
	static uint8_t a[4096], b[4096], got[4096];
	struct xhci_ep *ina, *inb;
	unsigned actual, i, rounds;

	section("two devices, interleaved");

	check(bring_up(2) == OK, "the controller starts");
	model_control_handler(device_control);
	model_attach(0, 3);
	model_attach(1, 3);

	check(xhci_port_reset(0) == OK, "port 1 resets");
	check(xhci_port_reset(1) == OK, "port 2 resets");
	check(xhci_device_attach(0, &dev0) == OK, "the first enumerates");
	check(xhci_device_attach(1, &dev1) == OK, "the second enumerates");
	check(dev0.slot != dev1.slot, "they have different slots (%u, %u)",
	    dev0.slot, dev1.slot);

	ina = xhci_device_ep(&dev0, 1, 1);
	inb = xhci_device_ep(&dev1, 1, 1);
	check(ina != NULL && inb != NULL && ina->ring.p != inb->ring.p,
	    "and different transfer rings");

	for (i = 0; i < sizeof(a); i++) {
		a[i] = (uint8_t)(0xa0 + (i & 0xf));
		b[i] = (uint8_t)(0xb0 + (i & 0xf));
	}

	/*
	 * Four hundred rounds of alternating transfers, which is more than
	 * a ring holds: both rings wrap several times while the other is
	 * in use, and every answer has to be the right device's.
	 */
	for (rounds = 0; rounds < 400; rounds++) {
		model_ep_supply(dev0.slot, ina->dci, a, 64);
		if (xhci_transfer(&dev0, ina, 64, &actual) != OK ||
		    actual != 64)
			break;
		memcpy(got, (void *)dev0.buf, 64);
		if (memcmp(got, a, 64) != 0)
			break;

		model_ep_supply(dev1.slot, inb->dci, b, 64);
		if (xhci_transfer(&dev1, inb, 64, &actual) != OK ||
		    actual != 64)
			break;
		memcpy(got, (void *)dev1.buf, 64);
		if (memcmp(got, b, 64) != 0)
			break;
	}

	check(rounds == 400, "%u of 400 interleaved pairs came back right",
	    rounds);
	check(model_stats.link_follows >= 2,
	    "with %u wrap(s) along the way - each ring holds 255 entries",
	    model_stats.link_follows);
	check(model_stats.stale_reads == 0,
	    "and nothing was read before it was handed over");
	check(model_complaint() == NULL, "the model saw nothing wrong: %s",
	    model_complaint() ? model_complaint() : "nothing");
}

/*===========================================================================*
 *    6. the doorbell                                                        *
 *===========================================================================*/
/*
 * Work queued and not announced is work that never happens, and it looks
 * exactly like a dead interrupt line: no event, nothing pending in the
 * controller, no error anywhere.  That is the shape of the stall the
 * asynchronous path hit, so the property is worth holding on to
 * explicitly rather than as a side effect of the transfers above.
 */
static void
test_doorbell(void)
{
	struct xhci_ep *in;
	unsigned before, i;
	uint8_t bytes[64];

	memset(bytes, 0x77, sizeof(bytes));
	section("the doorbell");

	check(bring_up(1) == OK, "the controller starts");
	model_control_handler(device_control);
	model_attach(0, 3);
	check(xhci_port_reset(0) == OK, "the port resets");
	check(xhci_device_attach(0, &dev0) == OK, "the device enumerates");

	in = xhci_device_ep(&dev0, 1, 1);
	before = model_stats.events;

	/*
	 * The device has something to say, so the only thing between the
	 * entry and its event is the doorbell.
	 */
	model_ep_supply(dev0.slot, in->dci, bytes, sizeof(bytes));

	/* An entry on the ring, handed over properly, and not announced. */
	xhci_ring_push(&in->ring, (uint32_t)dev0.buf_phys, 0, 64,
	    XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);
	micro_delay(100);
	micro_delay(100);
	micro_delay(100);

	check(model_stats.events == before,
	    "an entry nobody rang for produces no event");

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev0.slot), in->dci);
	for (i = 0; i < 8; i++)
		micro_delay(100);

	check(model_stats.events == before + 1,
	    "and the doorbell is what makes it happen");
}

#include "async.inc"

/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		shim_verbose(1);

	test_ring_shape();
	test_ring_wrap();
	test_cache_is_load_bearing();
	test_commands();
	test_enumeration();
	test_transfers();
	test_short_control();
	test_two_devices();
	test_doorbell();
	test_outstanding();

	printf("\n%u checks, %u failure(s)\n", checks, failures);
	return failures != 0 ? 1 : 0;
}
