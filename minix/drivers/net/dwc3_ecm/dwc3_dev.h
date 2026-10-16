/*
 * The DWC3 core in device mode, shared by dwc3_dev.c (the controller, its
 * commands and events) and dwc3_ep0.c (control transfers on ep0).
 */
#ifndef _DWC3_DEV_H
#define _DWC3_DEV_H

#include "dwc3_ecm.h"

#define DWC3_EVBUF_SIZE		4096
#define DWC3_EP0_BUF_SIZE	512
#define DWC3_LINE		64	/* a cache line: CTR_EL0 on both cores */

struct dwc3_trb {
	uint32_t bpl, bph, size, ctrl;
};

/* ep0's phases (Linux ep0.c): where the next event belongs. */
enum dwc3_ep0_state {
	EP0_SETUP,		/* a SETUP TRB on ep0 OUT */
	EP0_DATA,		/* a DATA TRB on the data phase's endpoint */
	EP0_WAIT_STATUS,	/* waiting for XferNotReady(status) */
	EP0_STATUS		/* a STATUS TRB */
};

struct dwc3_dev {
	volatile uint8_t *core;

	/*
	 * DMA memory, physically contiguous and below 4 GiB, every part on
	 * its own cache lines: the one ep0 TRB both directions take turns
	 * with (as Linux's ep0_trb), the 8-byte SETUP buffer, the ep0 data
	 * buffer, and on the second page the event buffer.
	 */
	uint8_t *dma;
	phys_bytes dma_pa;
	struct dwc3_trb *trb;
	phys_bytes trb_pa;
	uint8_t *setup;
	phys_bytes setup_pa;
	uint8_t *buf;
	phys_bytes buf_pa;
	uint8_t *ev;
	phys_bytes ev_pa;
	unsigned ev_pos;

	enum dwc3_ep0_state ep0;
	int three_stage;	/* the request has a data phase */
	int expect_in;		/* ... and it goes to the host */
	int active_ep;		/* the endpoint with a started transfer, -1 */
	unsigned rsc[2];	/* their transfer resource indices */
	unsigned address, config;

	unsigned long polls, resets, setups, stalls;
	int connected, said_no_reset;
};

extern struct dwc3_dev dwc;

/*
 * The core's registers.  Inline, so that a host test bed can put its
 * model of the core behind these two names (port/test/dwc3_ecm).
 */
static inline uint32_t
dwc3_rd(unsigned off)
{
	return *(volatile uint32_t *)(dwc.core + off);
}

static inline void
dwc3_wr(unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(dwc.core + off) = v;
}

int dwc3_ep_cmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1,
	uint32_t p2, uint32_t *done);
void dwc3_give(void *p, size_t len);	/* to the device: clean, invalidate */
void dwc3_take(void *p, size_t len);	/* from the device: invalidate */

/* dwc3_ep0.c */
void dwc3_ep0_setup_start(void);
void dwc3_ep0_reset(void);
void dwc3_ep0_xfer_complete(unsigned ep);
void dwc3_ep0_xfer_notready(unsigned ep, unsigned status);

#endif /* _DWC3_DEV_H */
