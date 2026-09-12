/*
 * The machine the driver talks to: memory without coherency, and a host
 * controller that follows the rings.  See model.h for why it exists and
 * what it deliberately does that an emulator would not.
 *
 * One limitation, stated here rather than discovered later.  The driver
 * reaches its registers with ordinary loads and stores, so this model
 * cannot see a write happen; it notices that a register has changed the
 * next time it runs.  Everything the driver writes and then waits on -
 * doorbells, the run bit, the reset bit - is therefore exact.  Two writes
 * to the SAME register with no wait between them are not: the model sees
 * only the second.  That affects one sequence, the acknowledge-then-reset
 * dance on PORTSC, and it is why the checks here are about rings and
 * events rather than about that register's write-one-to-clear bits.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/log.h>

#include "xhci.h"
#include "xhcireg.h"
#include "model.h"

struct model_stats model_stats;

static char complaint[256];

static void
complain(const char *fmt, ...)
{
	va_list ap;

	if (complaint[0] != '\0')
		return;			/* the first one is the useful one */

	va_start(ap, fmt);
	vsnprintf(complaint, sizeof(complaint), fmt, ap);
	va_end(ap);
}

const char *
model_complaint(void)
{
	return complaint[0] != '\0' ? complaint : NULL;
}

void
model_complain_reset(void)
{
	complaint[0] = '\0';
}

/*===========================================================================*
 *    memory without coherency                                               *
 *===========================================================================*/
/*
 * Two copies of every region: what the driver sees, and what the
 * controller sees.  Nothing moves between them except through
 * sys_cachectl(2), which is the whole point - on the board there is no
 * uncached alias to be had, so every hand-over is a deliberate act, and a
 * forgotten one has to be visible here rather than three milestones later
 * on hardware.
 *
 * The controller's copy starts as a pattern rather than as zeroes.  Fresh
 * memory on the board is whatever was there before, with the driver's
 * zeroes sitting dirty in the cache; a model that started both copies at
 * zero would agree with a driver that never cleaned anything.
 */
#define LINE		64
#define FILL		0x5a
#define NREGIONS	64

struct region {
	int used;
	phys_bytes phys;
	size_t size;
	uint8_t *drv;
	uint8_t *dev;
};

static struct region regions[NREGIONS];
static phys_bytes next_phys = 0x40000000;

void *
model_mem_alloc(size_t size, phys_bytes *phys)
{
	struct region *r = NULL;
	unsigned i;

	size = (size + 4095) & ~(size_t)4095;

	for (i = 0; i < NREGIONS; i++)
		if (!regions[i].used) {
			r = &regions[i];
			break;
		}
	if (r == NULL) {
		complain("the model ran out of regions");
		return NULL;
	}

	if (posix_memalign((void **)&r->drv, 4096, size) != 0 ||
	    posix_memalign((void **)&r->dev, 4096, size) != 0) {
		complain("out of memory in the model");
		return NULL;
	}

	memset(r->drv, FILL, size);
	memset(r->dev, FILL, size);

	r->used = 1;
	r->size = size;
	r->phys = next_phys;
	next_phys += size + 4096;	/* a gap, so a run-off is not silent */

	*phys = r->phys;
	return r->drv;
}

void
model_mem_free(void *v, size_t size)
{
	unsigned i;

	for (i = 0; i < NREGIONS; i++)
		if (regions[i].used && regions[i].drv == v) {
			free(regions[i].drv);
			free(regions[i].dev);
			memset(&regions[i], 0, sizeof(regions[i]));
			return;
		}
	complain("the driver freed something the model never gave it");
}

void
model_mem_reset(void)
{
	unsigned i;

	for (i = 0; i < NREGIONS; i++)
		if (regions[i].used) {
			free(regions[i].drv);
			free(regions[i].dev);
		}
	memset(regions, 0, sizeof(regions));
	next_phys = 0x40000000;
}

static struct region *
region_of_drv(const void *addr)
{
	const uint8_t *p = addr;
	unsigned i;

	for (i = 0; i < NREGIONS; i++)
		if (regions[i].used && p >= regions[i].drv &&
		    p < regions[i].drv + regions[i].size)
			return &regions[i];
	return NULL;
}

static struct region *
region_of_phys(phys_bytes phys, size_t len)
{
	unsigned i;

	for (i = 0; i < NREGIONS; i++)
		if (regions[i].used && phys >= regions[i].phys &&
		    phys + len <= regions[i].phys + regions[i].size)
			return &regions[i];
	return NULL;
}

uint8_t *
model_dev_ptr(phys_bytes phys, size_t len)
{
	struct region *r = region_of_phys(phys, len);

	if (r == NULL)
		return NULL;
	return r->dev + (phys - r->phys);
}

/*
 * A cache line is the unit, not a byte, and that is not pedantry: the
 * rounding is what makes a clean of one entry also publish its
 * neighbours, and what makes a clean of a half-used line able to land on
 * top of what the controller wrote into the other half.  NetBSD's
 * bus_dma writes the partly covered line back for exactly this reason,
 * and the model has to be able to punish a driver that does not.
 */
void
model_cache(int op, void *addr, size_t len)
{
	struct region *r = region_of_drv(addr);
	size_t off, end;

	if (r == NULL) {
		complain("cache maintenance on memory the model does not own");
		return;
	}

	off = (uint8_t *)addr - r->drv;
	end = off + len;

	off &= ~(size_t)(LINE - 1);
	end = (end + LINE - 1) & ~(size_t)(LINE - 1);
	if (end > r->size)
		end = r->size;

	if (op == CACHE_CLEAN || op == CACHE_CLEAN_INVALIDATE)
		memcpy(r->dev + off, r->drv + off, end - off);
	if (op == CACHE_INVALIDATE || op == CACHE_CLEAN_INVALIDATE)
		memcpy(r->drv + off, r->dev + off, end - off);
}

/*===========================================================================*
 *    the controller                                                         *
 *===========================================================================*/
#define DB_IDLE		0xffffffffu

/*
 * How many steps a transfer takes before its event appears.
 *
 * Not decoration.  With a controller that finishes within the same call
 * that started the transfer, the driver never has to wait at all, and
 * every defect that lives in the waiting - the lost wake-up above all -
 * is unreachable.  A real transfer takes microseconds to milliseconds and
 * the driver goes to sleep in between; the model has to make it do that.
 */
#define MODEL_LATENCY	2

struct mep {
	int valid;
	int active;			/* the doorbell has been rung */
	unsigned busy;			/* steps still to go */
	phys_bytes ptr;			/* where the controller reads next */
	unsigned ccs;			/* and the cycle bit it looks for */
	int dir_in;

	/* What the device has to give, and what it has been given. */
	uint8_t supply[XHCI_DEV_BUF];
	size_t supply_len, supply_pos;
	uint8_t sink[XHCI_DEV_BUF];
	size_t sink_len;

	/* The transfer descriptor being assembled. */
	unsigned td_len, td_done;
	unsigned trb_len, trb_done;	/* of the entry being run */
	int td_short;
};

struct mslot {
	int used;
	phys_bytes out_ctx;
	unsigned address;
	struct mep ep[MODEL_MAX_DCI];
};

static uint8_t *regs;
static unsigned nports, nslots;
static int halted = 1;

static phys_bytes cmd_ptr;
static unsigned cmd_ccs;
static int cmd_active;
static unsigned cmd_busy;

static phys_bytes event_base;
static unsigned event_slots, event_enq, event_ccs;

static uint32_t portsc_shadow[MODEL_MAX_SLOTS];
static unsigned port_reset_left[MODEL_MAX_SLOTS];
static unsigned port_speed[MODEL_MAX_SLOTS];

static struct mslot slots[MODEL_MAX_SLOTS + 1];
static model_control_fn control_fn;
static unsigned ctx_size = 64;

static uint32_t
rd(unsigned off)
{
	return *(volatile uint32_t *)(regs + off);
}

static void
wr(unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(regs + off) = v;
}

#define OPR	MODEL_CAPLENGTH
#define RTR	MODEL_RTSOFF

/*
 * Two registers are write-one-to-clear, and on real hardware that is how
 * the driver says "I have seen this": the interrupt-pending flag and the
 * event-interrupt flag.  Both are level-driven, so a model that let a
 * write set them instead of clearing them would report an interrupt that
 * never goes away - which is not a harmless inaccuracy but the live-lock
 * this port already met once on the UART.
 *
 * The model keeps its own copy of each and reconciles at the top of every
 * step, which is the same trick the port register uses and comes with the
 * same limitation: two writes with no wait between them collapse into one.
 */
static uint32_t iman_shadow, usbsts_shadow;

/*
 * Event handler busy, and it is the whole reason interrupts stop.
 *
 * The part sets this flag when it raises the line and will not raise it
 * again while it stands.  The driver clears it by writing the dequeue
 * pointer register with bit 3 set - which it does when it takes an event
 * off the ring.  A driver that is woken, finds the ring empty and goes
 * back to sleep never writes that register, the flag stays set, and the
 * controller is silent from then on however much it has to say.
 *
 * The model can see this write even though it only polls registers,
 * because the flag never stands in the model's own copy: a write with the
 * bit set always differs from what the model last put there.
 */
static int ehb;
static uint32_t erdp_shadow;
static int irq_pending;			/* an event written, its line not yet raised */

static unsigned event_deq_index(void);
static void sts_clr(uint32_t bits);

/*
 * The driver has acknowledged the interrupt and asked for the line again.
 *
 * The stand calls this from sys_irqenable(), which is the one point in
 * the sequence it can see: the register write that clears the flag is
 * invisible to anything polling, but the call that follows it is not.
 */
void
model_ack_interrupt(void)
{
	/* The busy flag is NOT cleared here: it is a separate write. */
	iman_shadow &= ~(uint32_t)XHCI_IMAN_IP;
	wr(RTR + XHCI_IR(0) + XHCI_IR_IMAN, iman_shadow);
	sts_clr(XHCI_USBSTS_EINT);
}

static void
sts_set(uint32_t bits)
{
	usbsts_shadow |= bits;
	wr(OPR + XHCI_USBSTS, usbsts_shadow);
}

static void
sts_clr(uint32_t bits)
{
	usbsts_shadow &= ~bits;
	wr(OPR + XHCI_USBSTS, usbsts_shadow);
}

static void
acknowledgements(void)
{
	uint32_t w;

	/*
	 * The interrupt-pending flag is remembered, and cleared only where
	 * the driver really clears it - see model_ack_interrupt().
	 *
	 * Deriving it instead ("pending means there is an unread event")
	 * was tried, because the model cannot see the driver's own
	 * acknowledgement: that write puts back the value already in the
	 * register.  It made the stand pass and hid a real defect.  On the
	 * part, clearing the flag while events are still on the ring does
	 * NOT re-raise the line: the controller asserts again only for the
	 * next event.  A driver that acknowledges and then sleeps without
	 * looking at the ring once more sleeps through an event that is
	 * already lying there.  That is what the board did, and a model
	 * that re-derives the flag can never show it.
	 *
	 * The enable bit is the driver's, and is taken as written.
	 */
	w = rd(RTR + XHCI_IR(0) + XHCI_IR_IMAN);
	iman_shadow = (iman_shadow & ~(uint32_t)XHCI_IMAN_IE) |
	    (w & XHCI_IMAN_IE);
	wr(RTR + XHCI_IR(0) + XHCI_IR_IMAN, iman_shadow);

	w = rd(RTR + XHCI_IR(0) + XHCI_IR_ERDP);
	if (w != erdp_shadow) {
		if (w & XHCI_ERDP_EHB)
			ehb = 0;
		erdp_shadow = w & ~(uint32_t)XHCI_ERDP_EHB;
		wr(RTR + XHCI_IR(0) + XHCI_IR_ERDP, erdp_shadow);
	}

	w = rd(OPR + XHCI_USBSTS);
	if (w != usbsts_shadow)
		sts_clr(w & (XHCI_USBSTS_EINT | XHCI_USBSTS_PCD |
		    XHCI_USBSTS_HSE));
}

void
model_control_handler(model_control_fn fn)
{
	control_fn = fn;
}

vir_bytes
model_reset(unsigned ports, unsigned nsl)
{
	unsigned i;

	if (regs == NULL)
		regs = calloc(1, MODEL_REGS_SIZE);
	memset(regs, 0, MODEL_REGS_SIZE);
	memset(&model_stats, 0, sizeof(model_stats));
	memset(slots, 0, sizeof(slots));
	memset(portsc_shadow, 0, sizeof(portsc_shadow));
	memset(port_reset_left, 0, sizeof(port_reset_left));
	memset(port_speed, 0, sizeof(port_speed));
	complaint[0] = '\0';

	nports = ports;
	nslots = nsl;
	halted = 1;
	cmd_active = 0;
	event_base = 0;

	/*
	 * What this part says about itself, taken from the board: 64-byte
	 * contexts, no 64-bit addressing, one interrupter, one scratchpad
	 * buffer.  The numbers are in port/cb2-usb/reference-regs.txt.
	 */
	wr(XHCI_CAPLENGTH, MODEL_CAPLENGTH | (0x0110u << 16));
	wr(XHCI_HCSPARAMS1, (nslots & 0xff) | (1u << 8) |
	    ((nports & 0xff) << 24));
	wr(XHCI_HCSPARAMS2, (1u << 27) | (0xfu << 4));
	wr(XHCI_HCCPARAMS1, (1u << 2) /* CSZ: contexts are 64 bytes */);
	wr(XHCI_DBOFF, MODEL_DBOFF);
	wr(XHCI_RTSOFF, MODEL_RTSOFF);

	wr(OPR + XHCI_PAGESIZE, 1);
	iman_shadow = 0;
	usbsts_shadow = 0;
	sts_set(XHCI_USBSTS_HCH);

	for (i = 0; i < MODEL_MAX_SLOTS; i++)
		wr(MODEL_DBOFF + XHCI_DB(i), DB_IDLE);

	return (vir_bytes)regs;
}

/*---------------------------------------------------------------------------*
 *    the event ring                                                          *
 *---------------------------------------------------------------------------*/
static unsigned
event_deq_index(void)
{
	uint32_t erdp = rd(RTR + XHCI_IR(0) + XHCI_IR_ERDP) & ~0xfu;

	if (event_base == 0 || event_slots == 0)
		return 0;
	return (unsigned)((erdp - event_base) / XHCI_TRB_SIZE) % event_slots;
}

static void
event_push(uint32_t p0, uint32_t p1, uint32_t status, uint32_t control)
{
	uint8_t *dev;
	uint32_t words[4];
	unsigned next;

	if (event_base == 0) {
		complain("the model has no event ring to write to");
		return;
	}

	/*
	 * The ring is full when the next entry to write is the one the
	 * driver has said it has not read yet.  A real controller reports
	 * this as an event ring full error; here it is counted, because a
	 * driver that stops acknowledging is a driver that stops hearing,
	 * and that is a stall with no error message anywhere.
	 */
	next = (event_enq + 1) % event_slots;
	if (next == event_deq_index()) {
		model_stats.event_ring_full++;
		return;
	}

	dev = model_dev_ptr(event_base + event_enq * XHCI_TRB_SIZE,
	    XHCI_TRB_SIZE);
	if (dev == NULL) {
		complain("the event ring is not in memory the model owns");
		return;
	}

	words[0] = p0;
	words[1] = p1;
	words[2] = status;
	words[3] = (control & ~XHCI_TRB_C) | (event_ccs ? XHCI_TRB_C : 0);
	memcpy(dev, words, sizeof(words));

	if (++event_enq == event_slots) {
		event_enq = 0;
		event_ccs ^= 1;
	}

	model_stats.events++;

	sts_set(XHCI_USBSTS_EINT);

	/*
	 * The interrupt follows the event, it does not accompany it.  The
	 * write of the TRB and the assertion of the line are two things
	 * the controller does one after the other, and a driver that is
	 * polling the ring can take the event in between - after which the
	 * line is raised, and the busy flag set, for an event that is
	 * already gone.  Raised three steps from now rather than here, so
	 * that the stand can be that driver: one step for it to see the
	 * event, one for it to write the dequeue pointer, one for that
	 * write to land before the line does.  Nothing in the specification
	 * says the line comes sooner, and the board says it does not.
	 */
	irq_pending = 3;
}

/*
 * The controller raising its line for what it has written - or not, when
 * the driver has not yet said it finished with the last one.
 */
static void
irq_raise(void)
{
	if (irq_pending == 0)
		return;
	if (irq_pending > 1) {
		irq_pending--;
		return;
	}

	/*
	 * The driver has not said it finished handling the last one, so
	 * the line stays down: the event waits for the dequeue pointer
	 * write that clears the busy flag, and a driver that never makes
	 * it waits for ever.
	 */
	if (ehb) {
		model_stats.events_unannounced++;
		return;
	}

	irq_pending = 0;
	ehb = 1;
	iman_shadow |= XHCI_IMAN_IP;
	wr(RTR + XHCI_IR(0) + XHCI_IR_IMAN, iman_shadow);
	model_stats.interrupts++;
}

/*---------------------------------------------------------------------------*
 *    reading a ring                                                          *
 *---------------------------------------------------------------------------*/
/*
 * Answers 1 and fills in the TRB when there is one whose cycle bit is the
 * controller's; 0 when the ring has run out of work, which is not an
 * error but the normal end of a burst.
 */
static int
trb_fetch(phys_bytes at, unsigned ccs, struct xhci_trb *out)
{
	static const uint8_t fill[XHCI_TRB_SIZE] = {
		FILL, FILL, FILL, FILL, FILL, FILL, FILL, FILL,
		FILL, FILL, FILL, FILL, FILL, FILL, FILL, FILL
	};
	uint8_t *dev = model_dev_ptr(at, XHCI_TRB_SIZE);

	if (dev == NULL) {
		complain("a ring points at 0x%lx, which is not memory",
		    (unsigned long)at);
		return 0;
	}

	if (memcmp(dev, fill, XHCI_TRB_SIZE) == 0) {
		/*
		 * Never cleaned: the driver's writes are still in its own
		 * cache and this is what RAM holds.  Telling this apart
		 * from an empty ring is the whole reason fresh memory is
		 * filled with a pattern rather than with zeroes.
		 */
		model_stats.stale_reads++;
		return 0;
	}

	memcpy(out, dev, XHCI_TRB_SIZE);
	return ((out->control & XHCI_TRB_C) != 0) == (ccs != 0);
}

/*---------------------------------------------------------------------------*
 *    contexts                                                                *
 *---------------------------------------------------------------------------*/
static uint32_t
ctx_word(phys_bytes base, unsigned ctx, unsigned word)
{
	uint8_t *p = model_dev_ptr(base + ctx * ctx_size + word * 4, 4);
	uint32_t v;

	if (p == NULL)
		return 0;
	memcpy(&v, p, 4);
	return v;
}

/*
 * Take the endpoints an input context adds, and remember where their
 * rings are.  This is the only way the model ever learns about a transfer
 * ring, which is deliberate: a driver that moves a ring without saying so
 * with a command leaves the model reading the old one, and its transfers
 * stop - the same way they would on the board, and with the same silence.
 */
static void
take_input_context(unsigned slot, phys_bytes in_ctx)
{
	uint32_t add = ctx_word(in_ctx, 0, 1);
	unsigned dci;

	for (dci = 1; dci < MODEL_MAX_DCI; dci++) {
		struct mep *ep;
		uint32_t w2, w3;

		if (!(add & (1u << dci)))
			continue;

		ep = &slots[slot].ep[dci];
		w2 = ctx_word(in_ctx, dci + 1, 2);
		w3 = ctx_word(in_ctx, dci + 1, 3);

		ep->valid = 1;
		ep->ptr = (phys_bytes)(w2 & ~0xfu) |
		    ((phys_bytes)w3 << 32);
		ep->ccs = (w2 & XHCI_EP_DCS) ? 1 : 0;
		ep->dir_in = (dci == 1) || (dci & 1);
		ep->supply_pos = 0;
		ep->td_len = ep->td_done = 0;
		ep->td_short = 0;
	}
}

/*---------------------------------------------------------------------------*
 *    commands                                                                *
 *---------------------------------------------------------------------------*/
static void
command_run(void)
{
	struct xhci_trb trb;

	if (cmd_busy != 0) {
		cmd_busy--;
		return;
	}

	while (cmd_active) {
		unsigned type, slot, cc = XHCI_CC_SUCCESS, ev_slot = 0;
		phys_bytes where = cmd_ptr;

		if (!trb_fetch(cmd_ptr, cmd_ccs, &trb)) {
			cmd_active = 0;
			model_stats.cycle_stops++;
			return;
		}

		type = XHCI_TRB_TYPE_OF(trb.control);

		if (type == XHCI_TRB_LINK) {
			cmd_ptr = trb.p0;
			if (trb.control & XHCI_TRB_TC) {
				cmd_ccs ^= 1;
				model_stats.toggles++;
			}
			model_stats.link_follows++;
			continue;
		}

		model_stats.commands++;
		slot = XHCI_EVENT_SLOT_ID(trb.control);

		switch (type) {
		case XHCI_TRB_NOOP_CMD:
			break;

		case XHCI_TRB_ENABLE_SLOT:
			for (ev_slot = 1; ev_slot <= nslots &&
			    ev_slot < MODEL_MAX_SLOTS; ev_slot++)
				if (!slots[ev_slot].used)
					break;
			if (ev_slot > nslots || ev_slot >= MODEL_MAX_SLOTS) {
				cc = XHCI_CC_PARAMETER_ERROR;
				ev_slot = 0;
			} else {
				memset(&slots[ev_slot], 0,
				    sizeof(slots[ev_slot]));
				slots[ev_slot].used = 1;
			}
			break;

		case XHCI_TRB_ADDRESS_DEVICE:
		case XHCI_TRB_CONFIGURE_EP: {
			uint8_t *dcbaa;
			uint32_t out_ctx = 0;

			if (slot == 0 || slot >= MODEL_MAX_SLOTS ||
			    !slots[slot].used) {
				cc = XHCI_CC_PARAMETER_ERROR;
				break;
			}

			/*
			 * Where the controller keeps this device's context
			 * is not in the command: it is entry N of the array
			 * the driver pointed at with DCBAAP.  A driver that
			 * enables a slot and does not fill that entry gets
			 * a parameter error here rather than silence.
			 */
			dcbaa = model_dev_ptr(rd(OPR + XHCI_DCBAAP) & ~0x3fu,
			    (nslots + 1) * 8);
			if (dcbaa != NULL)
				memcpy(&out_ctx, dcbaa + slot * 8, 4);
			if (out_ctx == 0) {
				cc = XHCI_CC_PARAMETER_ERROR;
				break;
			}
			slots[slot].out_ctx = out_ctx;

			take_input_context(slot, trb.p0);
			if (type == XHCI_TRB_ADDRESS_DEVICE)
				slots[slot].address = slot;
			ev_slot = slot;
			break;
		}

		default:
			model_stats.bad_commands++;
			cc = XHCI_CC_TRB_ERROR;
			break;
		}

		event_push((uint32_t)where, 0, cc << 24,
		    XHCI_TRB_TYPE(XHCI_TRB_CMD_COMPLETION) |
		    XHCI_TRB_SLOT_ID(ev_slot != 0 ? ev_slot : slot));

		cmd_ptr += XHCI_TRB_SIZE;
	}
}

/*---------------------------------------------------------------------------*
 *    transfers                                                               *
 *---------------------------------------------------------------------------*/
/*
 * A transfer event, and the one number in it that is easy to model
 * wrongly: the length field is the residue of the TRB the event names,
 * not of the descriptor.  The specification says so (6.4.2.1, "the
 * residual number of bytes not transferred for the TRB"), and the
 * distinction only shows on a descriptor of several entries - which is
 * exactly what a bulk transfer whose buffer crosses 64 KiB is.  The
 * first version of this model reported the descriptor's residue, which
 * agrees with a driver that adds up nothing and disagrees with the
 * controller.
 */
static void
trb_event(struct mep *ep, unsigned slot, unsigned dci, phys_bytes where,
	unsigned cc, unsigned residue)
{
	event_push((uint32_t)where, 0, (cc << 24) | (residue & 0xffffff),
	    XHCI_TRB_TYPE(XHCI_TRB_TRANSFER_EVENT) | XHCI_TRB_SLOT_ID(slot) |
	    ((dci & 0x1f) << 16));
}

static void
td_event(struct mep *ep, unsigned slot, unsigned dci, phys_bytes where,
	unsigned cc)
{
	unsigned residue = ep->trb_len > ep->trb_done ?
	    ep->trb_len - ep->trb_done : 0;

	trb_event(ep, slot, dci, where, cc, residue);

	model_stats.transfers++;
	ep->td_len = ep->td_done = 0;
	ep->td_short = 0;
}

static void
move_bytes(struct mep *ep, phys_bytes buf, unsigned len, int dir_in)
{
	uint8_t *dev;
	unsigned n;

	ep->td_len += len;
	ep->trb_len = len;
	ep->trb_done = 0;

	if (len == 0)
		return;

	dev = model_dev_ptr(buf, len);
	if (dev == NULL) {
		complain("a transfer points at 0x%lx, which is not memory",
		    (unsigned long)buf);
		return;
	}

	if (dir_in) {
		n = (unsigned)(ep->supply_len - ep->supply_pos);
		if (n > len)
			n = len;
		memcpy(dev, ep->supply + ep->supply_pos, n);
		ep->supply_pos += n;
		ep->td_done += n;
		ep->trb_done = n;
		if (n < len)
			ep->td_short = 1;
	} else {
		n = len;
		if (ep->sink_len + n > sizeof(ep->sink))
			n = (unsigned)(sizeof(ep->sink) - ep->sink_len);
		memcpy(ep->sink + ep->sink_len, dev, n);
		ep->sink_len += n;
		ep->td_done += len;
		ep->trb_done = len;
	}
}

static void
endpoint_run(unsigned slot, unsigned dci)
{
	struct mep *ep = &slots[slot].ep[dci];
	struct xhci_trb trb;

	if (ep->busy != 0) {
		ep->busy--;
		return;
	}

	while (ep->active) {
		unsigned type, len;
		phys_bytes where = ep->ptr;

		if (!trb_fetch(ep->ptr, ep->ccs, &trb)) {
			ep->active = 0;
			model_stats.cycle_stops++;
			return;
		}

		type = XHCI_TRB_TYPE_OF(trb.control);

		if (type == XHCI_TRB_LINK) {
			ep->ptr = trb.p0;
			if (trb.control & XHCI_TRB_TC) {
				ep->ccs ^= 1;
				model_stats.toggles++;
			}
			model_stats.link_follows++;
			continue;
		}

		len = trb.status & 0x1ffff;
		ep->trb_len = ep->trb_done = 0;	/* until move_bytes() says */

		/*
		 * TD Size (4.11.2.4): how many packets of the descriptor
		 * follow this entry.  A chained entry that says none do is a
		 * contradiction the part resolves in its own favour - on the
		 * board it ended the transfer there, and the rest of the data
		 * arrived as the answer to the next request.  The model does
		 * not imitate that; it says so, which is what the stand needs.
		 */
		if (type == XHCI_TRB_NORMAL && (trb.control & XHCI_TRB_CH) &&
		    ((trb.status >> 17) & 0x1f) == 0)
			complain("a chained entry at 0x%lx says no packets "
			    "remain after it", (unsigned long)where);

		switch (type) {
		case XHCI_TRB_SETUP: {
			uint8_t setup[8];

			memcpy(setup, &trb.p0, 4);
			memcpy(setup + 4, &trb.p1, 4);

			ep->supply_len = 0;
			ep->supply_pos = 0;
			if (control_fn != NULL)
				ep->supply_len = control_fn(slot, setup,
				    ep->supply, sizeof(ep->supply));
			break;
		}

		case XHCI_TRB_DATA:
		case XHCI_TRB_NORMAL: {
			int in;

			if (type == XHCI_TRB_DATA)
				in = (trb.control & XHCI_TRB_DIR_IN) != 0;
			else
				in = ep->dir_in;

			move_bytes(ep, trb.p0, len, in);
			break;
		}

		case XHCI_TRB_STATUS:
			break;

		default:
			complain("a transfer ring carries a TRB of type %u",
			    type);
			ep->active = 0;
			return;
		}

		ep->ptr += XHCI_TRB_SIZE;

		/*
		 * A short answer ends the descriptor there, and whether
		 * anybody is told depends on one bit.
		 *
		 * With interrupt-on-short-packet the controller reports it
		 * against this entry, carrying how much was left over.
		 * Without it the descriptor still ends - the controller
		 * simply says nothing, and goes on to the next one.  For a
		 * control transfer the next one is the status stage, whose
		 * own event has nothing left over to report, so the driver
		 * hears "all of it arrived" about a transfer that was
		 * short.  That is not a subtlety of the model: it is what
		 * the board did, and it is why the mass storage driver
		 * refused a flash drive for an invalid descriptor length.
		 */
		if (ep->td_short) {
			int chained = (trb.control & XHCI_TRB_CH) != 0;

			if (trb.control & XHCI_TRB_ISP)
				td_event(ep, slot, dci, where,
				    13 /* short packet */);
			else {
				ep->td_len = ep->td_done = 0;
				ep->td_short = 0;
			}

			/*
			 * What is left of the descriptor is skipped - retired
			 * without a transfer - and if the last of it asked
			 * for an event, it gets one: Short Packet, with the
			 * whole of that entry reported as not transferred
			 * (4.10.1.1).  So a short answer to a chained
			 * descriptor is two events, and the second says
			 * nothing true about the descriptor as a whole.
			 * A driver that believes it reports a short read
			 * as an empty one.
			 */
			while (chained) {
				phys_bytes here = ep->ptr;

				if (!trb_fetch(ep->ptr, ep->ccs, &trb))
					break;
				if (XHCI_TRB_TYPE_OF(trb.control) ==
				    XHCI_TRB_LINK) {
					ep->ptr = trb.p0;
					if (trb.control & XHCI_TRB_TC)
						ep->ccs ^= 1;
					continue;
				}
				ep->ptr += XHCI_TRB_SIZE;
				chained = (trb.control & XHCI_TRB_CH) != 0;
				if (!chained && (trb.control & XHCI_TRB_IOC))
					trb_event(ep, slot, dci, here,
					    13 /* short packet */,
					    trb.status & 0x1ffff);
			}
			continue;
		}

		if (trb.control & XHCI_TRB_IOC)
			td_event(ep, slot, dci, where, XHCI_CC_SUCCESS);
	}
}

/*---------------------------------------------------------------------------*
 *    doorbells, ports and the step                                           *
 *---------------------------------------------------------------------------*/
static void
doorbells(void)
{
	unsigned i;

	for (i = 0; i < MODEL_MAX_SLOTS; i++) {
		uint32_t v = rd(MODEL_DBOFF + XHCI_DB(i));

		if (v == DB_IDLE)
			continue;
		wr(MODEL_DBOFF + XHCI_DB(i), DB_IDLE);
		model_stats.doorbells++;

		if (i == 0) {
			cmd_active = 1;
			cmd_busy = MODEL_LATENCY;
			continue;
		}

		if (v >= MODEL_MAX_DCI || !slots[i].used ||
		    !slots[i].ep[v].valid) {
			model_stats.lost_doorbells++;
			continue;
		}
		slots[i].ep[v].active = 1;
		slots[i].ep[v].busy = MODEL_LATENCY;
	}
}

static void
ports(void)
{
	unsigned p;

	for (p = 0; p < nports && p < MODEL_MAX_SLOTS; p++) {
		unsigned off = OPR + XHCI_PORTSC(p);
		uint32_t w = rd(off), s = portsc_shadow[p];

		if (w != s) {
			uint32_t u = s;

			u &= ~(w & XHCI_PORTSC_CHANGES);
			if (w & XHCI_PORTSC_PED_W1C)
				u &= ~(uint32_t)XHCI_PORTSC_PED;
			u = (u & ~(uint32_t)XHCI_PORTSC_PP) |
			    (w & XHCI_PORTSC_PP);

			if ((w & XHCI_PORTSC_PR) && (s & XHCI_PORTSC_CCS)) {
				u |= XHCI_PORTSC_PR;
				port_reset_left[p] = 3;
			}

			portsc_shadow[p] = u;
			wr(off, u);
		}

		if (port_reset_left[p] != 0 && --port_reset_left[p] == 0) {
			uint32_t u = portsc_shadow[p];

			u &= ~(uint32_t)XHCI_PORTSC_PR;
			u |= XHCI_PORTSC_PED | XHCI_PORTSC_PRC;
			u &= ~(0xfu << 10);
			u |= (port_speed[p] & 0xf) << 10;
			u &= ~(0xfu << 5);		/* link state U0 */

			portsc_shadow[p] = u;
			wr(off, u);

			event_push(((p + 1) & 0xff) << 24, 0,
			    XHCI_CC_SUCCESS << 24,
			    XHCI_TRB_TYPE(XHCI_TRB_PORT_STATUS));
		}
	}
}

void
model_attach(unsigned port, unsigned speed)
{
	uint32_t v;

	if (port >= nports || port >= MODEL_MAX_SLOTS)
		return;

	port_speed[port] = speed;
	v = portsc_shadow[port] | XHCI_PORTSC_CCS | XHCI_PORTSC_CSC |
	    XHCI_PORTSC_PP;
	v &= ~(0xfu << 5);
	v |= (uint32_t)XHCI_PLS_POLLING << 5;
	portsc_shadow[port] = v;
	wr(OPR + XHCI_PORTSC(port), v);

	if (event_base != 0)
		event_push(((port + 1) & 0xff) << 24, 0,
		    XHCI_CC_SUCCESS << 24,
		    XHCI_TRB_TYPE(XHCI_TRB_PORT_STATUS));
}

void
model_detach(unsigned port)
{
	uint32_t v;

	if (port >= nports || port >= MODEL_MAX_SLOTS)
		return;

	v = portsc_shadow[port];
	v &= ~(uint32_t)(XHCI_PORTSC_CCS | XHCI_PORTSC_PED);
	v |= XHCI_PORTSC_CSC;
	portsc_shadow[port] = v;
	wr(OPR + XHCI_PORTSC(port), v);

	if (event_base != 0)
		event_push(((port + 1) & 0xff) << 24, 0,
		    XHCI_CC_SUCCESS << 24,
		    XHCI_TRB_TYPE(XHCI_TRB_PORT_STATUS));
}

void
model_ep_supply(unsigned slot, unsigned dci, const void *data, size_t len)
{
	struct mep *ep;

	if (slot >= MODEL_MAX_SLOTS || dci >= MODEL_MAX_DCI)
		return;
	ep = &slots[slot].ep[dci];
	if (len > sizeof(ep->supply))
		len = sizeof(ep->supply);
	memcpy(ep->supply, data, len);
	ep->supply_len = len;
	ep->supply_pos = 0;
}

size_t
model_ep_taken(unsigned slot, unsigned dci, void *out, size_t max)
{
	struct mep *ep;
	size_t n;

	if (slot >= MODEL_MAX_SLOTS || dci >= MODEL_MAX_DCI)
		return 0;
	ep = &slots[slot].ep[dci];
	n = ep->sink_len < max ? ep->sink_len : max;
	memcpy(out, ep->sink, n);
	return n;
}

int
model_event_pending(phys_bytes deq, unsigned cycle)
{
	uint8_t *dev = model_dev_ptr(deq, XHCI_TRB_SIZE);
	uint32_t control;

	if (dev == NULL)
		return 0;
	memcpy(&control, dev + 12, 4);
	return ((control & XHCI_TRB_C) != 0) == (cycle != 0);
}

static void
start(void)
{
	uint32_t crcr = rd(OPR + XHCI_CRCR);
	uint32_t erstba = rd(RTR + XHCI_IR(0) + XHCI_IR_ERSTBA) & ~0x3fu;
	uint8_t *erst;

	cmd_ptr = crcr & ~0x3fu;
	cmd_ccs = (crcr & XHCI_CRCR_RCS) ? 1 : 0;
	cmd_active = 0;

	erst = model_dev_ptr(erstba, 16);
	if (erst == NULL) {
		complain("the event ring segment table is not in memory");
		sts_set(XHCI_USBSTS_HSE);
		return;
	}
	memcpy(&event_base, erst, 4);
	memcpy(&event_slots, erst + 8, 4);
	event_slots &= 0xffff;
	event_enq = 0;
	event_ccs = 1;
	ehb = 0;
	erdp_shadow = rd(RTR + XHCI_IR(0) + XHCI_IR_ERDP) &
	    ~(uint32_t)XHCI_ERDP_EHB;

	if (event_slots == 0 ||
	    model_dev_ptr(event_base, event_slots * XHCI_TRB_SIZE) == NULL) {
		complain("the event ring segment is not in memory");
		sts_set(XHCI_USBSTS_HSE);
		return;
	}

	halted = 0;
	sts_clr(XHCI_USBSTS_HCH);
	wr(OPR + XHCI_CRCR, XHCI_CRCR_CRR);
}

void
model_step(void)
{
	uint32_t cmd;
	unsigned slot, dci;

	if (regs == NULL)
		return;

	cmd = rd(OPR + XHCI_USBCMD);

	if (cmd & XHCI_USBCMD_HCRST) {
		unsigned p;

		memset(slots, 0, sizeof(slots));
		cmd_active = 0;
		event_base = 0;
		halted = 1;
		wr(OPR + XHCI_USBCMD, cmd & ~(uint32_t)XHCI_USBCMD_HCRST);
		iman_shadow = 0;
		irq_pending = 0;
		wr(RTR + XHCI_IR(0) + XHCI_IR_IMAN, 0);
		usbsts_shadow = 0;
		sts_set(XHCI_USBSTS_HCH);
		wr(OPR + XHCI_CONFIG, 0);

		/*
		 * A reset does not unplug anything, and the port registers
		 * come back saying what is attached - which is how the
		 * driver finds a device that was there before it started.
		 */
		for (p = 0; p < nports && p < MODEL_MAX_SLOTS; p++) {
			portsc_shadow[p] &= XHCI_PORTSC_CCS;
			if (portsc_shadow[p] != 0)
				portsc_shadow[p] |= XHCI_PORTSC_CSC |
				    XHCI_PORTSC_PP;
			wr(OPR + XHCI_PORTSC(p), portsc_shadow[p]);
		}
		return;
	}

	if ((cmd & XHCI_USBCMD_RS) && halted) {
		start();
		return;
	}
	if (!(cmd & XHCI_USBCMD_RS) && !halted) {
		halted = 1;
		sts_set(XHCI_USBSTS_HCH);
	}

	acknowledgements();
	ports();
	irq_raise();

	if (halted)
		return;

	doorbells();
	command_run();

	for (slot = 1; slot < MODEL_MAX_SLOTS; slot++) {
		if (!slots[slot].used)
			continue;
		for (dci = 1; dci < MODEL_MAX_DCI; dci++)
			if (slots[slot].ep[dci].active)
				endpoint_run(slot, dci);
	}
}
