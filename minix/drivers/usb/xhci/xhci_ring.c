/*
 * The structures the controller and the driver share: the device context
 * array, the command ring, the event ring, the scratchpad - and the ring
 * arithmetic that every ring in this driver uses, transfer rings included.
 *
 * Three properties of this part were read off the board before any of this
 * was written, and each of them shapes the code rather than decorating it:
 *
 *   AC64 = 0   the address bus is 32 bits, so every one of these
 *              structures must live below 4 GiB.  The board has two
 *              gigabytes of RAM, so it holds by construction - but it is
 *              checked, because "true by luck" and "true by construction"
 *              look identical right up to the machine where it is neither.
 *   CSZ = 1    contexts are 64 bytes.
 *   1 buffer   the controller asks for one page of scratchpad, and it will
 *              not run without it.
 *
 * The cache is the hard part, not the registers.  The kernel maps all of
 * RAM cacheable and there is no uncached alias to be had - the same wall
 * the eMMC and the GMAC drivers hit - so every hand-over is explicit, with
 * sys_cachectl(2).  What that means here, direction by direction:
 *
 *   rings the driver    written by the driver, read by the controller:
 *   writes              cleaned before the doorbell.  The controller never
 *                       writes them, so cleaning a cache line that also
 *                       holds entries not yet issued is harmless.
 *   the event ring      written by the controller, read by the driver:
 *                       invalidated before every read.  Not once per
 *                       group, before every read - a line read once while
 *                       the entry was still the controller's stays in the
 *                       cache for ever otherwise, and that exact mistake
 *                       cost this port a day on the GMAC's receive ring.
 *                       Invalidating is safe in this direction because the
 *                       driver has nothing of its own in those lines to
 *                       lose.
 *   the arrays          written once by the driver: cleaned after writing.
 *
 * And one that is easy to miss: freshly allocated memory that the
 * controller is about to own has the driver's zeroes sitting dirty in the
 * cache.  If such a line is evicted later it lands on top of whatever the
 * controller has put there since.  So everything the controller writes -
 * the event ring, the scratchpad, a device context, a transfer buffer - is
 * clean-invalidated after being zeroed, which leaves nothing of the
 * driver's behind to fall on it.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>
#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhcireg.h"

/*
 * Cache maintenance with the answer looked at, as in the GMAC driver and
 * for the reason written there: a call that quietly does nothing looks
 * exactly like a call that worked, and the symptom turns up layers away.
 */
/*
 * How much of a transfer is spent where.  Measured rather than reasoned
 * about: the first attempt to explain 648 KB/s produced three plausible
 * stories, and the way to choose between them is a counter.
 */
unsigned long xhci_t_cache, xhci_n_cache;
unsigned long xhci_t_poll, xhci_n_poll;
unsigned long xhci_t_setup, xhci_t_wire, xhci_n_wire;
unsigned long xhci_t_small, xhci_n_small;

static unsigned long
elapsed(u64_t t)
{
	u64_t n;

	read_frclock_64(&n);
	return (unsigned long)frclock_64_to_micros(delta_frclock_64(t, n));
}

void
xhci_cache(int op, void *addr, size_t len, const char *what)
{
	static int complained;
	u64_t t;
	int r;

	read_frclock_64(&t);
	xhci_n_cache++;

	if ((r = sys_cachectl(op, addr, len)) != OK && !complained) {
		log_warn(&xhci_log, "cache maintenance refused for %s: %d; "
		    "nothing below this will work\n", what, r);
		complained = 1;
	}

	xhci_t_cache += elapsed(t);
}

static struct xhci_trb *
trb_at(vir_bytes ring, unsigned i)
{
	return (struct xhci_trb *)(ring + i * XHCI_TRB_SIZE);
}

static phys_bytes
trb_phys(phys_bytes ring, unsigned i)
{
	return ring + i * XHCI_TRB_SIZE;
}

/* The operational and runtime blocks, both offsets from the same base. */
static unsigned
op(void)
{
	return xhci.caplength;
}

static unsigned
rt(void)
{
	return xhci.rtsoff;
}

/*
 * One contiguous allocation per structure, page-aligned, which satisfies
 * every alignment and boundary rule the specification states at once: the
 * arrays and rings want 64-byte alignment and must not cross a 64 KiB
 * boundary, and a page-aligned 4 KiB page can do neither.  Saying it that
 * way is cheaper than four different alignments, and it is checked below
 * rather than asserted in a comment.
 */
void *
xhci_alloc_dma(size_t size, phys_bytes *phys, const char *what)
{
	void *v;

	v = alloc_contig(size, AC_ALIGN4K, phys);
	if (v == NULL) {
		log_warn(&xhci_log, "out of contiguous memory for %s\n", what);
		return NULL;
	}

	/*
	 * The controller cannot address above 4 GiB on this part, and it
	 * would not say so if it were handed such an address: the top half
	 * of the register is simply not there, and the transfer would land
	 * somewhere else entirely.
	 */
	if (!xhci.ac64 && (uint64_t)*phys + size > 0x100000000ULL) {
		log_warn(&xhci_log, "%s landed at 0x%lx, above the 4 GiB this "
		    "controller can address\n", what, (unsigned long)*phys);
		free_contig(v, size);
		return NULL;
	}

	memset(v, 0, size);
	return v;
}

/*
 * A ring: one page of entries, the last of which is a Link back to the
 * beginning with the toggle-cycle bit set.  That is how a fixed number of
 * entries becomes endless - the controller flips the cycle state it looks
 * for every time it follows the link, so an entry the driver has not
 * written yet never looks ready by accident.
 *
 * Command ring and transfer rings are the same shape and use the same
 * code, which is not tidiness: the wrap is where this driver's first real
 * defect was, and having one copy of it means it was fixed everywhere at
 * once.
 */
int
xhci_ring_setup(struct xhci_ring *r, const char *what)
{
	struct xhci_trb *link;
	void *v;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &r->p, what)) == NULL)
		return ENOMEM;

	r->v = (vir_bytes)v;
	r->slots = XHCI_PAGE / XHCI_TRB_SIZE;
	r->enq = 0;
	r->cycle = 1;

	link = trb_at(r->v, r->slots - 1);
	link->p0 = (uint32_t)r->p;
	link->p1 = 0;
	link->status = 0;
	link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC;

	xhci_cache(CACHE_CLEAN, (void *)r->v, XHCI_PAGE, what);
	return OK;
}

/*
 * Put one entry on a ring.  Answers where it landed, because a completion
 * event names the entry it belongs to by address and that is how a caller
 * tells its own command from somebody else's.
 */
phys_bytes
xhci_ring_push(struct xhci_ring *r, uint32_t p0, uint32_t p1, uint32_t status,
	uint32_t control)
{
	struct xhci_trb *trb;
	phys_bytes where;

	/*
	 * The last entry is the Link, and wrapping means handing it over.
	 *
	 * This is where the first version of this code was wrong, and the
	 * way it was wrong is worth keeping: the Link was written once at
	 * initialisation with its cycle bit clear, and the driver only
	 * flipped its own cycle state.  But the Link is an entry like any
	 * other, and the controller reads its cycle bit to decide whether
	 * it is allowed to follow it.  Left clear while the controller
	 * looks for a set bit, the ring simply ends there: everything up to
	 * the Link works, and every command after the wrap gets no
	 * completion at all.  Which is exactly what the board reported -
	 * 255 commands through and the next 45 silent - and is why the
	 * wrap is tested on hardware rather than reasoned about.
	 *
	 * So the Link is given the cycle state the controller is looking
	 * for now, and only then does the driver flip its own.
	 */
	if (r->enq == r->slots - 1) {
		struct xhci_trb *link = trb_at(r->v, r->slots - 1);

		link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
		    (r->cycle ? XHCI_TRB_C : 0);
		xhci_cache(CACHE_CLEAN, link, XHCI_TRB_SIZE, "a link");

		r->enq = 0;
		r->cycle ^= 1;
	}

	trb = trb_at(r->v, r->enq);
	where = trb_phys(r->p, r->enq);

	trb->p0 = p0;
	trb->p1 = p1;
	trb->status = status;
	trb->control = control | (r->cycle ? XHCI_TRB_C : 0);

	xhci_cache(CACHE_CLEAN, trb, XHCI_TRB_SIZE, "a ring entry");

	r->enq++;
	return where;
}

void
xhci_doorbell(struct xhci_device *dev, unsigned target)
{
	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(dev->slot), target);
}

void
xhci_ring_free(struct xhci_ring *r)
{
	if (r->v != 0) {
		free_contig((void *)r->v, XHCI_PAGE);
		r->v = 0;
	}
}

int
xhci_dma_alloc(void)
{
	void *v;

	/* The device context array, and the scratchpad that hangs off it. */
	if ((v = xhci_alloc_dma(XHCI_PAGE, &xhci.dcbaa_phys, "the device "
	    "context array")) == NULL)
		return ENOMEM;
	xhci.dcbaa = (vir_bytes)v;

	if (xhci.scratchpad_bufs != 0) {
		if ((v = xhci_alloc_dma(XHCI_PAGE, &xhci.spad_arr_phys,
		    "the scratchpad array")) == NULL)
			return ENOMEM;
		xhci.spad_arr = (vir_bytes)v;

		xhci.spad_size = (size_t)xhci.scratchpad_bufs * XHCI_PAGE;
		if ((v = xhci_alloc_dma(xhci.spad_size, &xhci.spad_phys,
		    "the scratchpad")) == NULL)
			return ENOMEM;
		xhci.spad = (vir_bytes)v;
	}

	if (xhci_ring_setup(&xhci.cmd, "the command ring") != OK)
		return ENOMEM;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &xhci.erst_phys, "the event ring "
	    "segment table")) == NULL)
		return ENOMEM;
	xhci.erst = (vir_bytes)v;

	if ((v = xhci_alloc_dma(XHCI_PAGE, &xhci.event_phys, "the event "
	    "ring")) == NULL)
		return ENOMEM;
	xhci.event = (vir_bytes)v;

	log_debug(&xhci_log, "dcbaa 0x%lx (%u entries), cmd 0x%lx, "
	    "event 0x%lx, erst 0x%lx, scratchpad 0x%lx\n",
	    (unsigned long)xhci.dcbaa_phys, xhci.nslots + 1,
	    (unsigned long)xhci.cmd.p, (unsigned long)xhci.event_phys,
	    (unsigned long)xhci.erst_phys, (unsigned long)xhci.spad_phys);

	return OK;
}

/* Lay out what is not a ring, and hand all of it over. */
static void
structures_init(void)
{
	uint64_t *dcbaa, *spad;
	uint32_t *erst;
	unsigned i;

	xhci.event_slots = XHCI_PAGE / XHCI_TRB_SIZE;
	xhci.event_deq = 0;
	xhci.event_cycle = 1;

	/* Slot 0 of the device context array points at the scratchpad. */
	dcbaa = (uint64_t *)xhci.dcbaa;
	if (xhci.scratchpad_bufs != 0) {
		spad = (uint64_t *)xhci.spad_arr;
		for (i = 0; i < xhci.scratchpad_bufs; i++)
			spad[i] = (uint64_t)(xhci.spad_phys + i * XHCI_PAGE);
		dcbaa[0] = (uint64_t)xhci.spad_arr_phys;
	}

	/* One event ring segment, and the table that says where it is. */
	erst = (uint32_t *)xhci.erst;
	erst[0] = (uint32_t)xhci.event_phys;
	erst[1] = 0;
	erst[2] = xhci.event_slots;
	erst[3] = 0;

	xhci_cache(CACHE_CLEAN, (void *)xhci.dcbaa, XHCI_PAGE, "the dcbaa");
	xhci_cache(CACHE_CLEAN, (void *)xhci.erst, XHCI_PAGE, "the erst");
	xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)xhci.event, XHCI_PAGE,
	    "the event ring");
	if (xhci.scratchpad_bufs != 0) {
		xhci_cache(CACHE_CLEAN, (void *)xhci.spad_arr, XHCI_PAGE,
		    "the scratchpad array");
		xhci_cache(CACHE_CLEAN_INVALIDATE, (void *)xhci.spad,
		    xhci.spad_size, "the scratchpad");
	}
}

/*
 * Bring the controller from wherever it was to halted-and-reset.
 *
 * "Wherever it was" is not hypothetical here: this driver is started by
 * hand into a live system, so a second start finds its own previous state,
 * and a warm reboot out of the vendor system leaves the part running.  So
 * the sequence is written to be the same either way rather than assuming a
 * fresh part.
 */
static int
halt_and_reset(void)
{
	unsigned spins;
	uint32_t v;

	v = xhci_rd(xhci.regs, op() + XHCI_USBCMD);
	if (v & XHCI_USBCMD_RS) {
		xhci_wr(xhci.regs, op() + XHCI_USBCMD, v & ~XHCI_USBCMD_RS);

		for (spins = 0; spins < 2000; spins++) {
			if (xhci_rd(xhci.regs, op() + XHCI_USBSTS) &
			    XHCI_USBSTS_HCH)
				break;
			micro_delay(100);
		}
		if (!(xhci_rd(xhci.regs, op() + XHCI_USBSTS) &
		    XHCI_USBSTS_HCH)) {
			log_warn(&xhci_log, "the controller will not halt "
			    "(USBSTS 0x%08x)\n",
			    xhci_rd(xhci.regs, op() + XHCI_USBSTS));
			return EIO;
		}
	}

	xhci_wr(xhci.regs, op() + XHCI_USBCMD, XHCI_USBCMD_HCRST);

	/*
	 * Two things have to go quiet, not one: the reset bit itself, and
	 * the controller-not-ready bit.  Reading any other register while
	 * CNR still stands answers zeroes that look like data.
	 */
	for (spins = 0; spins < 5000; spins++) {
		if (!(xhci_rd(xhci.regs, op() + XHCI_USBCMD) &
		    XHCI_USBCMD_HCRST) &&
		    !(xhci_rd(xhci.regs, op() + XHCI_USBSTS) &
		    XHCI_USBSTS_CNR))
			break;
		micro_delay(100);
	}

	if ((xhci_rd(xhci.regs, op() + XHCI_USBCMD) & XHCI_USBCMD_HCRST) ||
	    (xhci_rd(xhci.regs, op() + XHCI_USBSTS) & XHCI_USBSTS_CNR)) {
		log_warn(&xhci_log, "reset did not finish: USBCMD 0x%08x "
		    "USBSTS 0x%08x\n",
		    xhci_rd(xhci.regs, op() + XHCI_USBCMD),
		    xhci_rd(xhci.regs, op() + XHCI_USBSTS));
		return EIO;
	}

	return OK;
}

int
xhci_start(void)
{
	unsigned spins;
	uint32_t v;
	int r;

	if ((r = halt_and_reset()) != OK)
		return r;

	structures_init();

	/*
	 * How many device slots the driver will use.  Asking for all
	 * sixty-four costs nothing in memory here - the array is one page
	 * either way - and asking for fewer than are used is an error the
	 * controller reports as a parameter error on the first Enable Slot.
	 */
	v = xhci_rd(xhci.regs, op() + XHCI_CONFIG) &
	    ~(uint32_t)XHCI_CONFIG_MAXSLOTS_MASK;
	xhci_wr(xhci.regs, op() + XHCI_CONFIG, v | xhci.nslots);

	/* Where the device contexts are, and where the command ring is. */
	xhci_wr(xhci.regs, op() + XHCI_DCBAAP, (uint32_t)xhci.dcbaa_phys);
	xhci_wr(xhci.regs, op() + XHCI_DCBAAP + 4, 0);

	xhci_wr(xhci.regs, op() + XHCI_CRCR,
	    (uint32_t)xhci.cmd.p | XHCI_CRCR_RCS);
	xhci_wr(xhci.regs, op() + XHCI_CRCR + 4, 0);

	/*
	 * The event ring, and the order matters: the size and the dequeue
	 * pointer are set before the segment table's address, because
	 * writing that address is what makes the controller read the table.
	 */
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERSTSZ, 1);
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERDP,
	    (uint32_t)xhci.event_phys);
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERDP + 4, 0);
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERSTBA,
	    (uint32_t)xhci.erst_phys);
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERSTBA + 4, 0);

	/*
	 * No interrupt yet: this driver reads the event ring by polling,
	 * with a deadline, the way sdmmc's fallback path does.  The
	 * interrupt is one line in the tree and a hook, and it is left for
	 * the milestone that has something to do asynchronously - a driver
	 * that takes interrupts before it can act on them is harder to
	 * debug, not easier.
	 */
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_IMOD, 0);

	/* Run. */
	v = xhci_rd(xhci.regs, op() + XHCI_USBCMD);
	xhci_wr(xhci.regs, op() + XHCI_USBCMD, v | XHCI_USBCMD_RS);

	for (spins = 0; spins < 2000; spins++) {
		if (!(xhci_rd(xhci.regs, op() + XHCI_USBSTS) &
		    XHCI_USBSTS_HCH))
			break;
		micro_delay(100);
	}

	v = xhci_rd(xhci.regs, op() + XHCI_USBSTS);
	if (v & XHCI_USBSTS_HCH) {
		log_warn(&xhci_log, "the controller stayed halted after run "
		    "(USBSTS 0x%08x)\n", v);
		return EIO;
	}
	if (v & XHCI_USBSTS_HSE) {
		log_warn(&xhci_log, "host system error on start "
		    "(USBSTS 0x%08x): the controller could not read what it "
		    "was pointed at\n", v);
		return EIO;
	}

	log_info(&xhci_log, "running: USBSTS 0x%08x, CRCR %s, %u slots "
	    "enabled\n", v,
	    (xhci_rd(xhci.regs, op() + XHCI_CRCR) & XHCI_CRCR_CRR) ?
	    "running" : "not running",
	    xhci_rd(xhci.regs, op() + XHCI_CONFIG) &
	    XHCI_CONFIG_MAXSLOTS_MASK);

	return OK;
}

/*
 * One entry off the event ring, or nothing.
 *
 * The cycle bit is the whole protocol: the controller writes an entry with
 * the cycle state the driver is looking for, and the driver flips what it
 * looks for each time it wraps.  So "is there an event" is one bit
 * compared, and the invalidate before reading it is what makes that bit
 * come from memory rather than from a line read minutes ago.
 */
static int
event_next(struct xhci_trb *out)
{
	struct xhci_trb *trb = trb_at(xhci.event, xhci.event_deq);

	xhci_cache(CACHE_INVALIDATE, trb, XHCI_TRB_SIZE, "an event");

	if (((trb->control & XHCI_TRB_C) != 0) != (xhci.event_cycle != 0))
		return 0;

	*out = *trb;

	if (++xhci.event_deq == xhci.event_slots) {
		xhci.event_deq = 0;
		xhci.event_cycle ^= 1;
	}

	return 1;
}

/*
 * Tell the controller how far the driver has read, and that it is done.
 * The busy flag is write-one-to-clear and lives in the same register as
 * the pointer, so it has to be written along with it - a pointer written
 * without it leaves the controller believing the handler never finished.
 */
/*
 * Is there an event waiting, without taking it?
 *
 * Needed because the driver is about to go to sleep, and between the last
 * look at the ring and that sleep it acknowledges the controller's
 * interrupt flags.  See wait_for_event().
 */
static int
event_waiting(void)
{
	struct xhci_trb *trb = trb_at(xhci.event, xhci.event_deq);

	xhci_cache(CACHE_INVALIDATE, trb, XHCI_TRB_SIZE, "an event");

	return ((trb->control & XHCI_TRB_C) != 0) == (xhci.event_cycle != 0);
}

static void
event_done(void)
{
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERDP,
	    (uint32_t)trb_phys(xhci.event_phys, xhci.event_deq) |
	    XHCI_ERDP_EHB);
}

/*
 * Wait for the controller to say something, and answer how long that took.
 *
 * This is where the driver stopped being slow.  The first version spun:
 * look at the ring, wait a hundred microseconds, look again.  On the board
 * that cost thirteen and a half milliseconds per transfer - the same for a
 * thirty-one byte command as for thirty-two kilobytes of data, which is
 * what gave it away, since a fixed cost cannot be the wire.  It was the
 * scheduler: a driver that busy-waits burns its quantum, and then nobody
 * looks at the event ring until it is given another one.  The transfer had
 * been finished for milliseconds.
 *
 * So the wait is a real wait: the line is armed, the driver blocks, and
 * the kernel wakes it when the controller raises the interrupt.  The
 * alarm is the deadline; a request from a client that arrives meanwhile is
 * put aside rather than answered, because this driver is inside one
 * transfer and cannot start another.
 *
 * Every way this can fail ends in polling rather than in hanging, and each
 * is noticed once: the kernel refusing the line, the line interrupting
 * with nothing to show, and the deadline passing with no interrupt at all.
 */
/*
 * How many interrupts in a row have arrived with nothing on the ring.
 *
 * Counting every interrupt here was this driver's own mistake and cost a
 * board run: the line was declared useless after sixty-five perfectly good
 * interrupts, and every transfer went back to polling - which looked
 * exactly like the interrupt not working at all, because the timings did
 * not move.  Only an interrupt that produced nothing is spurious, so the
 * count is cleared where events are actually taken off the ring.
 */
static unsigned spurious;
unsigned long xhci_n_irq, xhci_n_alarm;

/*
 * Every alarm this driver sets goes through here, and so does every
 * clock notification it receives - so that a notification can be asked
 * who ordered it.
 *
 * A clock notification carries nothing: not which alarm it is for, nor
 * whether that alarm still stands.  The kernel marks it pending when the
 * alarm expires and delivers it at the driver's next receive, and
 * cancelling the alarm in between does not take the mark back.  So a
 * notification can arrive for an alarm that was cancelled, or long
 * before the alarm that is currently set is due, and the driver would
 * then act on a deadline that has not passed.  On the board that read as
 * "the transfer had finished and nobody was told", twice per bring-up at
 * the same two moments - which is not what a lost interrupt looks like,
 * it is what a stale clock looks like.  This makes the difference
 * visible instead of inferred.
 */
static u64_t alarm_at;
static unsigned alarm_for;		/* microseconds asked for; 0: none */
static const char *alarm_site, *alarm_last_site;

void
xhci_alarm(unsigned usec, const char *site)
{
	if (usec == 0) {
		sys_setalarm(0, 0);
		if (alarm_site != NULL)
			alarm_last_site = alarm_site;
		alarm_site = NULL;
		alarm_for = 0;
		return;
	}

	read_frclock_64(&alarm_at);
	alarm_for = usec;
	alarm_site = site;
	sys_setalarm(micros_to_ticks(usec), 0);
}

/*
 * Answers whether the alarm that is set was due: 0 when this
 * notification cannot be for it - none is set, or it is not nearly due
 * - and says so.
 */
int
xhci_alarm_fired(const char *where)
{
	unsigned long since = elapsed(alarm_at);

	if (alarm_for == 0) {
		log_warn(&xhci_log, "a clock notification in %s with no alarm "
		    "set; the last one, %s, was cancelled %lu us ago\n", where,
		    alarm_last_site != NULL ? alarm_last_site : "none",
		    since);
		return 0;
	}

	/* A tick early is the granularity of the clock, not a stale one. */
	if (since + 20000 < alarm_for) {
		log_warn(&xhci_log, "a clock notification in %s %lu us into "
		    "an alarm set for %u us by %s; the last one, %s, was "
		    "cancelled\n", where, since, alarm_for, alarm_site,
		    alarm_last_site != NULL ? alarm_last_site : "none");
		return 0;
	}

	return 1;
}

/*
 * Acknowledge the interrupt at the controller.  Both flags are
 * write-one-to-clear and level-driven: left standing they make the next
 * enable interrupt at once and for ever - the live-lock this port met on
 * the UART.
 */
void
xhci_irq_ack(void)
{
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_IMAN,
	    xhci_rd(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_IMAN) |
	    XHCI_IMAN_IP | XHCI_IMAN_IE);
	xhci_wr(xhci.regs, op() + XHCI_USBSTS, XHCI_USBSTS_EINT);
}

/*
 * Tell the controller the handler has finished - whether or not it took
 * anything off the ring.
 *
 * This is the dequeue pointer write with the Event Handler Busy flag,
 * the same write event_done() makes for every event taken; the point of
 * making it here as well is the interrupt that had nothing behind it.
 * The controller sets the busy flag when it raises the line, and it will
 * not raise the line again while the flag stands; the flag is cleared
 * only by this write.  Now consider a driver that polls the ring - the
 * synchronous path does, for every command and every control transfer
 * of enumeration.  It can take an event in the moment between the
 * controller writing it and the controller raising the line for it, and
 * write the dequeue pointer then; the line is raised afterwards, for an
 * event that is already gone, and the busy flag with it.  The driver
 * wakes, finds nothing, acknowledges the line and goes back to sleep
 * without touching the dequeue pointer.  From that moment the controller
 * writes events and says nothing about them: the next transfer finishes
 * on its deadline, five seconds later, and the alarm's drain - which
 * does write the dequeue pointer - is what wakes the controller up
 * again.
 *
 * On the board that was "the transfer had finished and nobody was told:
 * the event was on the ring, the interrupt was not", twice per
 * bring-up, each time the first client transfer after a polled
 * enumeration.  It was first tried a day earlier and taken out again,
 * because it was judged by reads that came back "Input/output error" -
 * which the deadline handler was producing on its own at the time, by
 * failing a request its drain had just completed.  A guess judged by a
 * symptom with another cause.  This time the stand shows the stall and
 * shows this write ending it, and the alarm count on the board says the
 * same.
 */
void
xhci_event_handled(void)
{
	event_done();
}

/*
 * The controller has something to say: what the main loop does on the
 * interrupt.  One place, so that the stand can do exactly what the
 * driver does, and so that the synchronous wait and the main loop cannot
 * drift apart in how they answer the line.
 */
void
xhci_interrupt(void)
{
	xhci_n_irq++;
	xhci_irq_ack();
	(void)xhci_events_drain(0, NULL, 0);
	xhci_event_handled();
	if (xhci.irq_ok && !xhci.irq_dead)
		(void)sys_irqenable(&xhci.irq_hook);
}

static unsigned
wait_for_event(unsigned usec)
{
	u64_t t_start;
	message m;
	int ipc_status;

	read_frclock_64(&t_start);

	if (!xhci.irq_ok || xhci.irq_dead || spurious > 64) {
		if (spurious == 65) {
			log_warn(&xhci_log, "line %d interrupted %u times "
			    "with nothing on the ring; polling from now on\n",
			    xhci.irq_line, spurious);
			xhci.irq_dead = 1;
			spurious++;
		}
		micro_delay(100);
		return elapsed(t_start);
	}

	/*
	 * Acknowledge what has already been signalled before arming the
	 * line again; see xhci_irq_ack() for why it cannot be left.
	 */
	xhci_irq_ack();

	if (sys_irqenable(&xhci.irq_hook) != OK) {
		log_warn(&xhci_log, "cannot enable line %d; polling from now "
		    "on\n", xhci.irq_line);
		xhci.irq_dead = 1;
		micro_delay(100);
		return elapsed(t_start);
	}

	/*
	 * One more look at the ring before going to sleep, and this is not
	 * belt and braces - it is the whole correctness of the wait.
	 *
	 * The caller looked at the ring, saw nothing, and called this.
	 * Between those two moments the controller can finish a transfer,
	 * write its event and raise the line.  The acknowledgement above
	 * then clears the assertion for an event nobody has read, the
	 * kernel has nothing to deliver, and the driver sleeps until the
	 * deadline over an event that is already lying there.  On the board
	 * that read as "the transfer had finished and nobody was told: the
	 * event was on the ring, the interrupt was not", once per second,
	 * with the transfer that lost the race stalling for its full
	 * timeout.
	 *
	 * Looking after arming closes the window: an event that arrived
	 * before the acknowledgement is seen here, and one that arrives
	 * after it raises a line that is now armed.
	 */
	if (event_waiting())
		return elapsed(t_start);

	xhci_alarm(usec, "a wait for the controller");

	for (;;) {
		if (sef_receive_status(ANY, &m, &ipc_status) != OK) {
			micro_delay(100);
			break;
		}

		if (is_ipc_notify(ipc_status)) {
			if (_ENDPOINT_P(m.m_source) == HARDWARE) {
				spurious++;
				xhci_n_irq++;
				/*
				 * Acknowledged here, at the controller, and
				 * not only at the next wait: the line is
				 * level-driven, and re-enabling it with the
				 * flags still standing is one more interrupt
				 * for nothing.  And the busy flag goes with
				 * it - see xhci_event_handled().
				 */
				xhci_irq_ack();
				xhci_event_handled();
				(void)sys_irqenable(&xhci.irq_hook);
				break;
			}
			if (_ENDPOINT_P(m.m_source) == CLOCK) {
				xhci_n_alarm++;
				xhci_alarm_fired("a wait for the controller");
				break;
			}
			continue;
		}

		/* Somebody else's request: it waits until this one is done. */
		xhci_defer(&m, ipc_status);
	}

	xhci_alarm(0, NULL);
	/*
	 * The line is left ON.  Switching it off here is what the first
	 * version did, and it broke the other half of the driver: the
	 * asynchronous path waits for an interrupt from a line this one had
	 * quietly disabled on its way out, so transfers finished, events
	 * landed on the ring, and nothing ever woke anybody.  Both paths
	 * now leave it armed, and the kernel disables it only for the
	 * moment between delivering an interrupt and being told to enable
	 * it again.
	 */

	/*
	 * How long this took is not counted in microseconds of delay any
	 * more - the caller only needs to know the deadline is nearer, and
	 * a wait that returned because the controller spoke has cost almost
	 * nothing.
	 */
	return elapsed(t_start);
}

/*
 * Drain the event ring for up to this many microseconds, stopping early
 * when an event of the wanted type turns up.  Returns how many were seen.
 *
 * Reporting every event at info level was right while there was one of
 * them per milestone; it is not right now that a control transfer
 * produces one each.  So the routine ones are logged at debug and only
 * what nobody asked for is loud.
 */
int
xhci_events_drain(unsigned usec, struct xhci_trb *want, unsigned want_type)
{
	struct xhci_trb ev;
	unsigned waited = 0;
	int seen = 0, got_wanted = 0;

	for (;;) {
		while (event_next(&ev)) {
			unsigned type = XHCI_TRB_TYPE_OF(ev.control);
			int claimed = 0;

			seen++;
			spurious = 0;

			switch (type) {
			case XHCI_TRB_PORT_STATUS:
				log_debug(&xhci_log, "event: port %u changed "
				    "(completion %u)\n",
				    XHCI_EVENT_PORT_ID(ev.p0),
				    XHCI_CC_OF(ev.status));
				break;
			case XHCI_TRB_CMD_COMPLETION:
				log_debug(&xhci_log, "event: command at "
				    "0x%08x completed %u, slot %u\n", ev.p0,
				    XHCI_CC_OF(ev.status),
				    XHCI_EVENT_SLOT_ID(ev.control));
				break;
			case XHCI_TRB_TRANSFER_EVENT:
				log_debug(&xhci_log, "event: transfer at "
				    "0x%08x, completion %u, %u byte(s) "
				    "short\n", ev.p0, XHCI_CC_OF(ev.status),
				    XHCI_EVENT_LENGTH(ev.status));

				/*
				 * A client's transfer finishes here, and
				 * finishing it is all that happens: the
				 * request was answered when it was
				 * submitted.  If nobody claims the event it
				 * belongs to a transfer this driver is
				 * waiting for itself, and it falls through
				 * to the wanted-event logic below.
				 *
				 * It is claimed, not skipped, and the
				 * difference is the whole defect this
				 * routine had.  A "continue" here leaves
				 * the loop before event_done(), so the
				 * controller is never told how far the
				 * driver has read.  Two hundred and
				 * fifty-five unacknowledged events later
				 * the ring is full and the controller stops
				 * writing events at all - for every client,
				 * not only the one whose events were
				 * skipped.  On the board that read as both
				 * clients stopping at once with "IMAN
				 * pending 0, USBSTS event 0": no events, no
				 * interrupt, nothing to complain about.
				 * The events had been taken; only the
				 * acknowledgement was missing.
				 */
				claimed = xhci_urb_transfer_event(&ev);
				break;
			default:
				log_info(&xhci_log, "event: type %u, "
				    "completion %u\n", type,
				    XHCI_CC_OF(ev.status));
				break;
			}

			/*
			 * The FIRST event of the wanted kind is kept, not
			 * the last.  A control transfer whose device
			 * answered short produces two - one for the data,
			 * which carries how much was left over, and one for
			 * the status stage, which carries nothing.  Keeping
			 * the last would report every short read as full.
			 */
			if (!claimed && want != NULL && type == want_type &&
			    !got_wanted) {
				*want = ev;
				got_wanted = 1;
			}

			event_done();
		}

		if (got_wanted || waited >= usec)
			break;

		{
			u64_t t;

			read_frclock_64(&t);
			waited += wait_for_event(usec - waited);
			xhci_t_poll += elapsed(t);
			xhci_n_poll++;
		}
	}

	return seen;
}

/*
 * Put one command on the ring, ring the doorbell and wait for the event
 * that belongs to it.
 *
 * "Belongs to it" is checked by address: the completion event carries the
 * address of the command it is about, and this driver has one thread and
 * one command outstanding, but checking the address costs a comparison and
 * turns a whole class of confusion into an error message.
 */
int
xhci_cmd(uint32_t p0, uint32_t p1, uint32_t status, uint32_t control,
	struct xhci_trb *ev)
{
	struct xhci_trb got;
	phys_bytes where;
	unsigned cc;

	memset(&got, 0, sizeof(got));

	where = xhci_ring_push(&xhci.cmd, p0, p1, status, control);

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(XHCI_DB_CMD), 0);

	if (xhci_events_drain(200000, &got, XHCI_TRB_CMD_COMPLETION) == 0 ||
	    XHCI_TRB_TYPE_OF(got.control) != XHCI_TRB_CMD_COMPLETION) {
		log_warn(&xhci_log, "no completion for the command at 0x%lx "
		    "(type %u); USBSTS 0x%08x, CRCR 0x%08x\n",
		    (unsigned long)where, XHCI_TRB_TYPE_OF(control),
		    xhci_rd(xhci.regs, op() + XHCI_USBSTS),
		    xhci_rd(xhci.regs, op() + XHCI_CRCR));
		return EIO;
	}

	if (got.p0 != (uint32_t)where)
		log_warn(&xhci_log, "the completion names the command at "
		    "0x%08x, not the one at 0x%lx\n", got.p0,
		    (unsigned long)where);

	if (ev != NULL)
		*ev = got;

	cc = XHCI_CC_OF(got.status);
	if (cc != XHCI_CC_SUCCESS) {
		log_warn(&xhci_log, "the command of type %u completed with "
		    "%u, not success\n", XHCI_TRB_TYPE_OF(control), cc);
		return EIO;
	}

	return OK;
}

int
xhci_cmd_noop_quiet(void)
{
	return xhci_cmd(0, 0, 0, XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD), NULL);
}

/*
 * The proof of milestone 10.2: a command that does nothing.
 *
 * No Op Command exists for exactly this - it asks the controller to fetch
 * a command, understand it and report completion, and nothing else.  A
 * Success back means the command ring is where the controller thinks it
 * is, the cycle bit is the right way round, the doorbell reaches it, the
 * event ring segment table is readable, the event landed in memory and the
 * driver's cache maintenance let it be seen.  Six things, one number.
 */
int
xhci_cmd_noop(void)
{
	int r;

	if ((r = xhci_cmd_noop_quiet()) != OK)
		return r;

	log_info(&xhci_log, "no-op command completed: the command ring, the "
	    "doorbell and the event ring all work\n");
	return OK;
}

/*
 * Reset one port and say what came of it.
 *
 * Only the speed after a successful reset means anything: before it, the
 * speed field of PORTSC is whatever the port last negotiated or nothing at
 * all, which is why the driver's first look at this port reported "full
 * speed" for a device the vendor system knows is high speed.
 */
int
xhci_port_reset(unsigned port)
{
	unsigned spins, reg = op() + XHCI_PORTSC(port);
	uint32_t v;

	v = xhci_rd(xhci.regs, reg);
	if (!(v & XHCI_PORTSC_CCS)) {
		log_debug(&xhci_log, "port %u: nothing attached\n", port + 1);
		return ENODEV;
	}

	/* Acknowledge whatever has changed so far, then ask for the reset. */
	xhci_wr(xhci.regs, reg, XHCI_PORTSC_KEEP(v) |
	    (v & XHCI_PORTSC_CHANGES));

	v = xhci_rd(xhci.regs, reg);
	xhci_wr(xhci.regs, reg, XHCI_PORTSC_KEEP(v) | XHCI_PORTSC_PR);

	/*
	 * The specification allows the reset itself twenty milliseconds and
	 * the port up to fifty to come back; the wait here is generous
	 * because the cost of being wrong is a port that reads "not
	 * enabled" for reasons that have nothing to do with the code.
	 */
	for (spins = 0; spins < 5000; spins++) {
		v = xhci_rd(xhci.regs, reg);
		if ((v & XHCI_PORTSC_PRC) || (v & XHCI_PORTSC_PED))
			break;
		micro_delay(100);
	}

	v = xhci_rd(xhci.regs, reg);
	if (!(v & XHCI_PORTSC_PED)) {
		log_warn(&xhci_log, "port %u did not enable: 0x%08x\n",
		    port + 1, v);
		return EIO;
	}

	/* Acknowledge the reset-complete and connect-change bits. */
	xhci_wr(xhci.regs, reg, XHCI_PORTSC_KEEP(v) |
	    (v & XHCI_PORTSC_CHANGES));

	v = xhci_rd(xhci.regs, reg);
	log_info(&xhci_log, "port %u after reset: 0x%08x  enabled, link "
	    "state %u, speed id %u\n", port + 1, v, XHCI_PORTSC_PLS(v),
	    XHCI_PORTSC_SPEED(v));

	return OK;
}

/*
 * Stop the controller, and do it before anything is given back.
 *
 * Without this the driver could be taken down and the part carried on:
 * the device context array, the command ring and the event ring are
 * physical addresses it holds in its own registers, and it goes on
 * reading and WRITING them after the process that allocated them is gone
 * and VM has handed those pages to somebody else.  That is not a leak, it
 * is a device writing into another process's memory at a time nothing can
 * be traced back to this driver.
 *
 * It also explains what could not be explained on the board: after
 * several stop-and-start cycles the same binary read thirty-two megabytes
 * cleanly one time and answered "Input/output error" the next.  A
 * measurement taken across a restart was not a measurement of anything.
 *
 * The sequence is the one the specification gives for going quiet, and it
 * is the same one start-up uses to find the part in a known state: stop,
 * wait for halted, then reset - which is what clears the registers
 * pointing at memory this driver is about to hand back.
 */
void
xhci_halt(void)
{
	unsigned spins;
	uint32_t v;

	if (xhci.regs == 0)
		return;

	v = xhci_rd(xhci.regs, op() + XHCI_USBCMD);
	if (v & XHCI_USBCMD_RS) {
		xhci_wr(xhci.regs, op() + XHCI_USBCMD, v & ~XHCI_USBCMD_RS);

		for (spins = 0; spins < 2000; spins++) {
			if (xhci_rd(xhci.regs, op() + XHCI_USBSTS) &
			    XHCI_USBSTS_HCH)
				break;
			micro_delay(100);
		}
	}

	/*
	 * And reset, which is what actually drops DCBAAP, CRCR and the
	 * event ring segment table.  A halted controller still holds them.
	 */
	xhci_wr(xhci.regs, op() + XHCI_USBCMD, XHCI_USBCMD_HCRST);

	for (spins = 0; spins < 5000; spins++) {
		if (!(xhci_rd(xhci.regs, op() + XHCI_USBCMD) &
		    XHCI_USBCMD_HCRST))
			break;
		micro_delay(100);
	}

	/* Nothing left to interrupt about. */
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_IMAN, 0);

	log_info(&xhci_log, "halted: USBCMD 0x%08x, USBSTS 0x%08x\n",
	    xhci_rd(xhci.regs, op() + XHCI_USBCMD),
	    xhci_rd(xhci.regs, op() + XHCI_USBSTS));
}

void
xhci_dma_free(void)
{
	if (xhci.dcbaa != 0)
		free_contig((void *)xhci.dcbaa, XHCI_PAGE);
	if (xhci.spad_arr != 0)
		free_contig((void *)xhci.spad_arr, XHCI_PAGE);
	if (xhci.spad != 0)
		free_contig((void *)xhci.spad, xhci.spad_size);
	if (xhci.erst != 0)
		free_contig((void *)xhci.erst, XHCI_PAGE);
	if (xhci.event != 0)
		free_contig((void *)xhci.event, XHCI_PAGE);
	xhci_ring_free(&xhci.cmd);
}
