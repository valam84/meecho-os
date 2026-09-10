/*
 * The structures the controller and the driver share: the device context
 * array, the command ring, the event ring and the scratchpad.  Milestone
 * 10.2, and the first part of this driver where memory has to mean the
 * same thing to two readers.
 *
 * Three properties of this part were read off the board before any of this
 * was written, and each of them shapes the code rather than decorating it:
 *
 *   AC64 = 0   the address bus is 32 bits, so every one of these
 *              structures must live below 4 GiB.  The board has two
 *              gigabytes of RAM, so it holds by construction - but it is
 *              checked, because "true by luck" and "true by construction"
 *              look identical right up to the machine where it is neither.
 *   CSZ = 1    contexts are 64 bytes.  Nothing here lays a context out yet
 *              (that is enumeration), but the device context array is
 *              sized in pointers and the number is worth having early.
 *   1 buffer   the controller asks for one page of scratchpad, and it will
 *              not run without it.
 *
 * The cache is the hard part, not the registers.  The kernel maps all of
 * RAM cacheable and there is no uncached alias to be had - the same wall
 * the eMMC and the GMAC drivers hit - so every hand-over is explicit, with
 * sys_cachectl(2).  What that means here, direction by direction:
 *
 *   the command ring   written by the driver, read by the controller:
 *                      cleaned before the doorbell.  The controller never
 *                      writes it, so cleaning a cache line that also holds
 *                      the driver's own not-yet-issued entries is
 *                      harmless.
 *   the event ring     written by the controller, read by the driver:
 *                      invalidated before every read.  Not once per group,
 *                      before every read - a line that was read once while
 *                      the entry was still the controller's stays in the
 *                      cache for ever otherwise, and that exact mistake
 *                      cost this port a day on the GMAC's receive ring.
 *                      Invalidating is safe in this direction because the
 *                      driver has nothing of its own in those lines to
 *                      lose.
 *   the arrays and     written once by the driver: cleaned after writing.
 *   the scratchpad     The scratchpad needs more than that - see below.
 *
 * And one that is easy to miss: freshly allocated memory that the
 * controller is about to own has the driver's zeroes sitting dirty in the
 * cache.  If such a line is evicted later it lands on top of whatever the
 * controller has put there since.  So everything the controller writes -
 * the event ring and the scratchpad - is clean-invalidated after being
 * zeroed, which leaves nothing of the driver's behind to fall on it.
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
static void
cache_op(int op, void *addr, size_t len, const char *what)
{
	static int complained;
	int r;

	if ((r = sys_cachectl(op, addr, len)) != OK && !complained) {
		log_warn(&xhci_log, "cache maintenance refused for %s: %d; "
		    "nothing below this will work\n", what, r);
		complained = 1;
	}
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

/* The operational, runtime and doorbell blocks, all offsets from the base. */
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
static void *
alloc_dma(size_t size, phys_bytes *phys, const char *what)
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

int
xhci_ring_alloc(void)
{
	size_t dcbaa_size = (xhci.nslots + 1) * sizeof(uint64_t);
	void *v;

	/* The device context array, and the scratchpad that hangs off it. */
	if ((v = alloc_dma(XHCI_PAGE, &xhci.dcbaa_phys, "the device context "
	    "array")) == NULL)
		return ENOMEM;
	xhci.dcbaa = (vir_bytes)v;

	if (xhci.scratchpad_bufs != 0) {
		if ((v = alloc_dma(XHCI_PAGE, &xhci.spad_arr_phys,
		    "the scratchpad array")) == NULL)
			return ENOMEM;
		xhci.spad_arr = (vir_bytes)v;

		xhci.spad_size = (size_t)xhci.scratchpad_bufs * XHCI_PAGE;
		if ((v = alloc_dma(xhci.spad_size, &xhci.spad_phys,
		    "the scratchpad")) == NULL)
			return ENOMEM;
		xhci.spad = (vir_bytes)v;
	}

	if ((v = alloc_dma(XHCI_PAGE, &xhci.cmd_phys, "the command ring"))
	    == NULL)
		return ENOMEM;
	xhci.cmd = (vir_bytes)v;

	if ((v = alloc_dma(XHCI_PAGE, &xhci.erst_phys, "the event ring "
	    "segment table")) == NULL)
		return ENOMEM;
	xhci.erst = (vir_bytes)v;

	if ((v = alloc_dma(XHCI_PAGE, &xhci.event_phys, "the event ring"))
	    == NULL)
		return ENOMEM;
	xhci.event = (vir_bytes)v;

	log_debug(&xhci_log, "dcbaa 0x%lx (%u entries), cmd 0x%lx, "
	    "event 0x%lx, erst 0x%lx, scratchpad 0x%lx\n",
	    (unsigned long)xhci.dcbaa_phys, xhci.nslots + 1,
	    (unsigned long)xhci.cmd_phys, (unsigned long)xhci.event_phys,
	    (unsigned long)xhci.erst_phys, (unsigned long)xhci.spad_phys);

	(void)dcbaa_size;
	return OK;
}

/*
 * Lay the structures out.  The command ring's last entry is a Link back to
 * its own beginning with the toggle-cycle bit set, which is how a ring of
 * a fixed number of entries becomes endless: the controller flips the
 * cycle state it is looking for every time it follows that link, so an
 * entry the driver has not written yet never looks ready by accident.
 */
static void
rings_init(void)
{
	struct xhci_trb *link;
	uint64_t *dcbaa, *spad;
	uint32_t *erst;
	unsigned i;

	xhci.cmd_slots = XHCI_PAGE / XHCI_TRB_SIZE;
	xhci.event_slots = XHCI_PAGE / XHCI_TRB_SIZE;
	xhci.cmd_enq = 0;
	xhci.cmd_cycle = 1;
	xhci.event_deq = 0;
	xhci.event_cycle = 1;

	link = trb_at(xhci.cmd, xhci.cmd_slots - 1);
	link->p0 = (uint32_t)xhci.cmd_phys;
	link->p1 = 0;
	link->status = 0;
	link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC;

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

	/*
	 * Hand them over.  The two the controller writes are
	 * clean-invalidated rather than cleaned: that leaves none of the
	 * driver's zeroes dirty in the cache to be evicted later on top of
	 * what the controller has put there.
	 */
	cache_op(CACHE_CLEAN, (void *)xhci.dcbaa, XHCI_PAGE, "the dcbaa");
	cache_op(CACHE_CLEAN, (void *)xhci.cmd, XHCI_PAGE, "the command ring");
	cache_op(CACHE_CLEAN, (void *)xhci.erst, XHCI_PAGE, "the erst");
	cache_op(CACHE_CLEAN_INVALIDATE, (void *)xhci.event, XHCI_PAGE,
	    "the event ring");
	if (xhci.scratchpad_bufs != 0) {
		cache_op(CACHE_CLEAN, (void *)xhci.spad_arr, XHCI_PAGE,
		    "the scratchpad array");
		cache_op(CACHE_CLEAN_INVALIDATE, (void *)xhci.spad,
		    xhci.spad_size, "the scratchpad");
	}
}

/*
 * Bring the controller from wherever it was to halted-and-reset.
 *
 * "Wherever it was" is not hypothetical here: this driver is started by
 * hand into a live system, and on this board the loader before it does not
 * touch USB at all - but a second start of the driver would find its own
 * previous state, and the vendor system leaves the part running when the
 * machine is warm-rebooted into MEECHO.  So the sequence is written to be
 * the same either way rather than assuming a fresh part.
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

	rings_init();

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
	    (uint32_t)xhci.cmd_phys | XHCI_CRCR_RCS);
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
	 * No interrupt yet: this milestone reads the event ring by polling,
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

	cache_op(CACHE_INVALIDATE, trb, XHCI_TRB_SIZE, "an event");

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
static void
event_done(void)
{
	xhci_wr(xhci.regs, rt() + XHCI_IR(0) + XHCI_IR_ERDP,
	    (uint32_t)trb_phys(xhci.event_phys, xhci.event_deq) |
	    XHCI_ERDP_EHB);
}

/*
 * Drain the event ring for up to this many microseconds, handing each
 * event to the caller's business.  Returns how many were seen.
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

			seen++;

			switch (type) {
			case XHCI_TRB_PORT_STATUS:
				log_info(&xhci_log, "event: port %u changed "
				    "(completion %u)\n",
				    XHCI_EVENT_PORT_ID(ev.p0),
				    XHCI_CC_OF(ev.status));
				break;
			case XHCI_TRB_CMD_COMPLETION:
				log_info(&xhci_log, "event: command at 0x%08x "
				    "completed %u\n", ev.p0,
				    XHCI_CC_OF(ev.status));
				break;
			case XHCI_TRB_TRANSFER_EVENT:
				log_info(&xhci_log, "event: transfer, "
				    "completion %u\n", XHCI_CC_OF(ev.status));
				break;
			default:
				log_info(&xhci_log, "event: type %u, "
				    "completion %u\n", type,
				    XHCI_CC_OF(ev.status));
				break;
			}

			if (want != NULL && type == want_type) {
				*want = ev;
				got_wanted = 1;
			}

			event_done();
		}

		if (got_wanted || waited >= usec)
			break;

		micro_delay(100);
		waited += 100;
	}

	return seen;
}

/*
 * Put one command on the ring and ring the doorbell.
 *
 * The cycle bit of the new entry is written last, and that is not style:
 * until it flips, the entry is not the controller's, and the clean that
 * carries it to memory carries the rest of the entry with it.  The order
 * within one cache line does not survive anyway - what makes this correct
 * is that the whole entry reaches memory in one maintenance operation,
 * after which the doorbell is what invites the controller to look.
 */
static int
cmd_submit(uint32_t p0, uint32_t p1, uint32_t status, uint32_t control)
{
	struct xhci_trb *trb;

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
	if (xhci.cmd_enq == xhci.cmd_slots - 1) {
		struct xhci_trb *link = trb_at(xhci.cmd, xhci.cmd_slots - 1);

		link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
		    (xhci.cmd_cycle ? XHCI_TRB_C : 0);
		cache_op(CACHE_CLEAN, link, XHCI_TRB_SIZE, "the link");

		xhci.cmd_enq = 0;
		xhci.cmd_cycle ^= 1;
	}

	trb = trb_at(xhci.cmd, xhci.cmd_enq);
	trb->p0 = p0;
	trb->p1 = p1;
	trb->status = status;
	trb->control = control | (xhci.cmd_cycle ? XHCI_TRB_C : 0);

	cache_op(CACHE_CLEAN, trb, XHCI_TRB_SIZE, "a command");

	xhci.cmd_enq++;

	xhci_wr(xhci.regs, xhci.dboff + XHCI_DB(XHCI_DB_CMD), 0);

	return OK;
}

/*
 * The proof of this milestone: a command that does nothing.
 *
 * No Op Command exists for exactly this - it asks the controller to fetch
 * a command, understand it and report completion, and nothing else.  A
 * Success back means the command ring is where the controller thinks it
 * is, the cycle bit is the right way round, the doorbell reaches it, the
 * event ring segment table is readable, the event landed in memory and the
 * driver's cache maintenance let it be seen.  Six things, one number.
 */
int
xhci_cmd_noop_quiet(void)
{
	struct xhci_trb ev;
	unsigned cc;

	memset(&ev, 0, sizeof(ev));

	cmd_submit(0, 0, 0, XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD));

	if (xhci_events_drain(100000, &ev, XHCI_TRB_CMD_COMPLETION) == 0 ||
	    XHCI_TRB_TYPE_OF(ev.control) != XHCI_TRB_CMD_COMPLETION) {
		log_warn(&xhci_log, "no completion event for the no-op "
		    "command at ring slot %u; USBSTS 0x%08x, CRCR 0x%08x\n",
		    xhci.cmd_enq, xhci_rd(xhci.regs, op() + XHCI_USBSTS),
		    xhci_rd(xhci.regs, op() + XHCI_CRCR));
		return EIO;
	}

	cc = XHCI_CC_OF(ev.status);
	if (cc != XHCI_CC_SUCCESS) {
		log_warn(&xhci_log, "the no-op command at ring slot %u "
		    "completed with %u, not success\n", xhci.cmd_enq, cc);
		return EIO;
	}

	return OK;
}

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

void
xhci_ring_free(void)
{
	if (xhci.dcbaa != 0)
		free_contig((void *)xhci.dcbaa, XHCI_PAGE);
	if (xhci.spad_arr != 0)
		free_contig((void *)xhci.spad_arr, XHCI_PAGE);
	if (xhci.spad != 0)
		free_contig((void *)xhci.spad, xhci.spad_size);
	if (xhci.cmd != 0)
		free_contig((void *)xhci.cmd, XHCI_PAGE);
	if (xhci.erst != 0)
		free_contig((void *)xhci.erst, XHCI_PAGE);
	if (xhci.event != 0)
		free_contig((void *)xhci.event, XHCI_PAGE);
}
