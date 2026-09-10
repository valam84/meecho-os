#ifndef _XHCI_MODEL_H
#define _XHCI_MODEL_H

/*
 * A model of the machine the xHCI driver talks to: memory that is not
 * coherent, and a host controller that follows the rings.
 *
 * Written from the specification rather than from the driver, for the
 * reason port/mfs4-damage.py exists: a checker and the thing it checks
 * must not share the code that could be wrong.  What is shared is
 * xhcireg.h, and only because register numbers and field positions are
 * facts about the silicon, not decisions of either side.
 *
 * Two things this model does that a real controller also does and an
 * emulator usually does not:
 *
 *   - It refuses to see memory the driver has not cleaned, and its own
 *     writes stay invisible until the driver invalidates.  There is no
 *     coherency at all here, because on the board there is none either -
 *     that is the wall the eMMC and the GMAC drivers hit, and a stale
 *     line in a receive ring has already cost this port a day once.
 *   - It only fetches from a ring after the doorbell for that ring has
 *     been rung, and it stops fetching at the first cycle bit that is not
 *     its own.  A driver that queues work and forgets the doorbell then
 *     waits for ever - which is exactly what "no events at all, and the
 *     controller raises nothing" looks like.
 */

#include <minix/drivers.h>
#include "xhci.h"

/*===========================================================================*
 *    memory that is not coherent                                            *
 *===========================================================================*/
void *model_mem_alloc(size_t size, phys_bytes *phys);
void model_mem_free(void *v, size_t size);
void model_mem_reset(void);

/* What sys_cachectl(2) does on this machine: move bytes between views. */
void model_cache(int op, void *addr, size_t len);

/* The controller's view of a physical address; NULL if it is not memory. */
uint8_t *model_dev_ptr(phys_bytes phys, size_t len);

/*===========================================================================*
 *    the controller                                                         *
 *===========================================================================*/
#define MODEL_REGS_SIZE		0x10000
#define MODEL_CAPLENGTH		0x20
#define MODEL_RTSOFF		0x1000
#define MODEL_DBOFF		0x2000
#define MODEL_MAX_SLOTS		8
#define MODEL_MAX_DCI		32

/* Reset the modelled part and hand back its register block. */
vir_bytes model_reset(unsigned nports, unsigned nslots);

/* One step of the machine.  Every micro_delay() in the driver is one. */
void model_step(void);

/* A device on a root port, and taking it away again. */
void model_attach(unsigned port, unsigned speed);
void model_detach(unsigned port);

/*
 * What a device answers.  Bytes queued here are what an IN transfer on
 * that endpoint returns, in order; a transfer that asks for more than is
 * queued is short, which is the case that cost this driver the ISP bit.
 */
void model_ep_supply(unsigned slot, unsigned dci, const void *data,
	size_t len);
size_t model_ep_taken(unsigned slot, unsigned dci, void *out, size_t max);

/*
 * What a device answers to a control request.  The callback is given the
 * eight bytes of the setup packet and fills in the answer; returning zero
 * means the device stalls.
 */
typedef size_t (*model_control_fn)(unsigned slot, const uint8_t setup[8],
	uint8_t *out, size_t max);
void model_control_handler(model_control_fn fn);

/*===========================================================================*
 *    what the model saw                                                     *
 *===========================================================================*/
struct model_stats {
	unsigned commands;		/* commands fetched */
	unsigned transfers;		/* transfer descriptors completed */
	unsigned events;		/* events written */
	unsigned doorbells;
	unsigned cycle_stops;		/* times a ring ran out of work */
	unsigned link_follows;
	unsigned toggles;

	/*
	 * Things a real controller would do quietly and wrongly, which is
	 * why they are counted rather than printed.
	 */
	unsigned stale_reads;		/* a TRB read that was never cleaned */
	unsigned event_ring_full;	/* the driver did not keep up */
	unsigned bad_commands;		/* a command the model cannot parse */
	unsigned lost_doorbells;	/* rung at a ring with no work */
};

extern struct model_stats model_stats;

const char *model_complaint(void);	/* NULL when nothing went wrong */
void model_complain_reset(void);

#endif /* _XHCI_MODEL_H */
