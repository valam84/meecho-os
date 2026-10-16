/*
 * The DWC3 core in device mode, shared by dwc3_dev.c (the controller, its
 * commands, endpoints and events), dwc3_ep0.c (control transfers on ep0)
 * and ecm.c (the CDC Ethernet function on the other endpoints).
 */
#ifndef _DWC3_DEV_H
#define _DWC3_DEV_H

#include "dwc3_ecm.h"

#include <minix/netdriver.h>

#define DWC3_EVBUF_SIZE		4096
#define DWC3_EP0_BUF_SIZE	512
#define DWC3_LINE		64	/* a cache line: CTR_EL0 on both cores */
#define DWC3_NEP		8	/* physical endpoints this driver uses */

/* Physical endpoint numbers: (USB number << 1) | IN. */
#define DWC3_EP0_OUT		0
#define DWC3_EP0_IN		1
#define DWC3_EP1_OUT		2	/* ECM data, host to device */
#define DWC3_EP1_IN		3	/* ECM data, device to host */
#define DWC3_EP2_IN		5	/* ECM notifications */

/* Endpoint types, as SETEPCONFIG and the descriptors number them. */
#define DWC3_EP_CONTROL		0
#define DWC3_EP_BULK		2
#define DWC3_EP_INTR		3

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

/*
 * DMA memory, physically contiguous and below 4 GiB, every part on cache
 * lines of its own.  Page 0: ep0's one TRB (both directions take turns
 * with it, as Linux's ep0_trb), the 8-byte SETUP buffer, the ep0 data
 * buffer, the TRBs of the receive, transmit (two: a frame and its
 * zero-length packet) and notification endpoints, the notification.
 * Page 1: the event buffer.  Page 2: one frame each way.
 */
#define DWC3_DMA_SIZE		(3 * 4096)
#define DWC3_OFF_EP0_TRB	0
#define DWC3_OFF_SETUP		(1 * DWC3_LINE)
#define DWC3_OFF_EP0_BUF	(2 * DWC3_LINE)
#define DWC3_OFF_RX_TRB		(10 * DWC3_LINE)
#define DWC3_OFF_TX_TRB		(11 * DWC3_LINE)
#define DWC3_OFF_NOTIFY_TRB	(12 * DWC3_LINE)
#define DWC3_OFF_NOTIFY		(13 * DWC3_LINE)
#define DWC3_OFF_EVENTS		4096
#define DWC3_OFF_RX		8192
#define DWC3_OFF_TX		(8192 + 2048)
#define DWC3_FRAME_BUF		2048	/* a multiple of maxpacket, > 1514 */

struct dwc3_dev {
	volatile uint8_t *core;

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
	int active_ep;		/* ep0 OUT/IN with a started transfer, -1 */
	int busy[DWC3_NEP];	/* a transfer runs on this endpoint */
	unsigned rsc[DWC3_NEP];	/* its transfer resource index */
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

static inline phys_bytes
dwc3_pa(const void *p)
{
	return dwc.dma_pa + (phys_bytes)((const uint8_t *)p - dwc.dma);
}

int dwc3_ep_cmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1,
	uint32_t p2, uint32_t *done);
void dwc3_give(void *p, size_t len);	/* to the device: clean, invalidate */
void dwc3_take(void *p, size_t len);	/* from the device: invalidate */

/* Endpoints other than ep0 (dwc3_dev.c). */
int dwc3_ep_enable(unsigned ep, unsigned type, unsigned mps,
	unsigned interval);
void dwc3_ep_disable(unsigned ep);
int dwc3_ep_start(unsigned ep, struct dwc3_trb *trb);
void dwc3_ep_end(unsigned ep);
int dwc3_restart_config(void);		/* SET_CONFIGURATION's DEPSTARTCFG */

/* dwc3_ep0.c */
void dwc3_ep0_setup_start(void);
void dwc3_ep0_reset(void);
void dwc3_ep0_xfer_complete(unsigned ep);
void dwc3_ep0_xfer_notready(unsigned ep, unsigned status);

/*
 * ecm.c: the function behind ep0's standard requests and on endpoints 2,
 * 3 and 5.  descriptor() fills buf and returns the length, or -1;
 * class_request() takes a request with no data phase, 0 or -1 to stall;
 * set_config() and set_interface() 0 or -1; get_interface() the alternate
 * setting or -1.  reset() is a bus reset or disconnect.
 */
int ecm_descriptor(unsigned type, unsigned idx, uint8_t *buf, size_t max);
int ecm_class_request(unsigned type, unsigned req, unsigned value,
	unsigned index);
int ecm_set_config(unsigned config);
int ecm_set_interface(unsigned intf, unsigned alt);
int ecm_get_interface(unsigned intf);
void ecm_reset(void);
void ecm_xfer_complete(unsigned ep);

/* ecm.c, for libnetdriver (dwc3_ecm.c's table). */
void ecm_hwaddr(netdriver_addr_t *addr);
ssize_t ecm_recv(struct netdriver_data *data, size_t max);
int ecm_send(struct netdriver_data *data, size_t size);
unsigned int ecm_get_link(uint32_t *media);

#endif /* _DWC3_DEV_H */
