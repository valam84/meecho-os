#ifndef _XHCI_H
#define _XHCI_H

#include <minix/drivers.h>
#include <minix/log.h>

/*
 * The USB host controller of the RK3566/RK3568, and so of the BIGTREETECH
 * CB2: an xHCI 1.1 inside a Synopsys DWC3 3.0a, with Rockchip glue around
 * that.  One driver for all speeds, which is what xHCI is for - the SoC
 * also has two EHCI and two OHCI, and this driver does not touch them.
 *
 * Why a new driver rather than the USB stack already in the tree.  The
 * client half of that stack speaks URBs and does not know what controller
 * is underneath, so usb_hub and usb_storage are kept and will be served
 * from here.  The server half does not fit at all: its HCD interface is
 * per-transaction (setup_stage, rx_stage, in_data_stage), which is the
 * MUSB model where the processor drives every phase, while xHCI runs the
 * transaction itself off a ring and hands out addresses with its own
 * command.  Its own header says "only one port for each driver".  So this
 * is written next to it, the way GICv3 was written next to GICv2.
 *
 * The parts, split by what each one talks to:
 *
 *   xhci_find.c  the device tree: what this machine has and where
 *   xhci_rk.c    the glue: power domain, clocks, resets, PHY, DWC3
 *   xhci_ring.c  the structures the controller and the driver share
 *   xhci.c       the controller and the driver's own life
 *
 * The glue is in no Synopsys or xHCI document and was read out of a
 * working system before it was written; the recipe and the numbers are in
 * port/cb2-usb/registers.md, the snapshots in port/cb2-usb/reference*.txt.
 */

#define XHCI_MAX_RESETS		4
#define XHCI_MAX_PORTS		8

/*
 * The page the controller counts in.  PAGESIZE in its own register says 4
 * KiB on this part, and every array and ring below is allocated one page:
 * that satisfies the 64-byte alignment and the 64 KiB boundary rule at
 * once, since a page-aligned 4 KiB page can straddle neither.
 */
#define XHCI_PAGE		4096

/*
 * A transfer request block: four words, and the last one carries both the
 * type and the cycle bit that says whose turn it is.
 */
struct xhci_trb {
	uint32_t p0;
	uint32_t p1;
	uint32_t status;
	uint32_t control;
};

/*
 * A ring of TRBs: one page, the last entry a Link back to the start.  The
 * command ring and every transfer ring are the same shape, and share the
 * code that walks them - which is not tidiness but insurance, since the
 * wrap is where this driver's first real defect was.
 */
struct xhci_ring {
	vir_bytes v;
	phys_bytes p;
	unsigned slots;
	unsigned enq;			/* where the driver writes next */
	unsigned cycle;			/* and with which cycle bit */
};

/*
 * One endpoint of a device, and the ring the driver hands it work on.
 *
 * The controller numbers endpoints differently from USB itself: what USB
 * calls endpoint N in a direction is "device context index" 2N (out) or
 * 2N+1 (in), with the control endpoint at 1.  That number is the index
 * into the device context, the doorbell to ring, and the bit to set when
 * asking for the endpoint to be configured - so it is worked out once and
 * kept, rather than recomputed at three sites that could disagree.
 */
struct xhci_ep {
	unsigned dci;			/* 0 when this entry is unused */
	unsigned num;			/* the number USB gives it */
	int dir_in;
	unsigned type;			/* the xHCI endpoint type */
	unsigned max_packet;
	unsigned interval;
	struct xhci_ring ring;
};

#define XHCI_MAX_EPS		8

/*
 * The buffer a device's transfers go through.  Mass storage asks for
 * thirty-two kilobytes at a time (sixty-four sectors), so anything
 * smaller would make every read two transfers instead of one.
 */
#define XHCI_DEV_BUF		(64 * 1024)

/*
 * A device the controller has given a slot to - on a root port, or behind
 * a hub.  The difference is written in two fields: the route string, which
 * says which port of which hub to walk, and the transaction translator,
 * which a slow device behind a fast hub is reached through.
 */
struct xhci_device {
	unsigned slot;			/* 0 when this entry is unused */
	unsigned port;			/* the root hub port, counting from 0 */
	unsigned speed;			/* the PORTSC speed identifier */
	unsigned max_packet0;
	unsigned config;		/* the configuration that was set */
	unsigned interfaces;		/* a bit per interface number it has */
	unsigned class;			/* what the device says it is */
	unsigned iface_class;		/* and what its first interface says */
	int announced;			/* the drivers have been told about it */

	vir_bytes in_ctx;		/* the input context for commands */
	phys_bytes in_ctx_phys;
	vir_bytes dev_ctx;		/* the context the controller keeps */
	phys_bytes dev_ctx_phys;
	struct xhci_ring ep0;		/* the control endpoint's ring */

	vir_bytes buf;			/* the transfer buffer */
	phys_bytes buf_phys;

	/*
	 * Where this device hangs.  A device on a root port has route 0 and
	 * tier 0; one behind a hub carries that hub's route plus its own
	 * port number, four bits per tier, which is how the controller
	 * finds it without the driver addressing anything.
	 */
	unsigned route;
	unsigned tier;
	unsigned parent_slot;		/* the hub, for a slow device */
	unsigned parent_port;

	struct xhci_ep ep[XHCI_MAX_EPS];
	unsigned neps;
};

/* Everything the device tree said about this controller. */
struct xhci_devinfo {
	phys_bytes base;		/* the controller: xHCI and DWC3 */
	size_t size;
	int irq;			/* -1 when the tree names none */

	phys_bytes cru_base;		/* clocks and resets */
	size_t cru_size;
	unsigned reset_id[XHCI_MAX_RESETS];
	unsigned nresets;

	phys_bytes pmu_base;		/* the power domain the xHCI is in */
	size_t pmu_size;
	int power_domain;		/* -1 when the tree names none */

	/*
	 * The PHY, which is three things in two register blocks: the
	 * analogue part at the node's own address, the logical part in a
	 * separate syscon the node points at with "rockchip,usbgrf", and a
	 * reference clock that lives in the PMU's clock controller rather
	 * than in the main one.  Two blocks with similar offsets is the
	 * trap here; see port/cb2-usb/registers.md.
	 */
	phys_bytes phy_base;
	size_t phy_size;
	phys_bytes usbgrf_base;
	size_t usbgrf_size;
	phys_bytes pmucru_base;
	size_t pmucru_size;
	int phy_port;			/* 0 the OTG port, 1 the host port */
	int phy_unit;			/* 0 or 1: which of the SoC's two */
};

struct xhci {
	struct xhci_devinfo info;

	vir_bytes regs;			/* mapped register blocks */
	vir_bytes cru;
	vir_bytes pmucru;
	vir_bytes pmu;
	vir_bytes phy;
	vir_bytes usbgrf;

	/*
	 * The interrupt, and what is known about whether it works.  Kept
	 * the way sdmmc keeps it, and for the reason written there: a line
	 * that misbehaves is a thing to notice once and route around, not
	 * to rediscover on every transfer.
	 */
	int irq_line;
	int irq_hook;
	int irq_ok;			/* the kernel armed it */
	int irq_dead;			/* ...and it then proved useless */

	/* What the controller said about itself, read once at init. */
	unsigned caplength;		/* where the operational block is */
	unsigned rtsoff;		/* and the runtime one */
	unsigned dboff;			/* and the doorbells */
	unsigned hciversion;
	unsigned nslots;
	unsigned nports;
	unsigned nintrs;
	unsigned context_size;		/* 32 or 64 bytes */
	unsigned scratchpad_bufs;
	int ac64;			/* 64-bit addressing; 0 on this part */
	unsigned xecp;			/* extended capabilities, byte offset */
	uint32_t dwc3_id;		/* GSNPSID */

	/*
	 * Which USB version each port speaks, from the extended
	 * capabilities - the only place the tree or the registers state it.
	 * Zero means the capability named no protocol for that port, and a
	 * port nobody claims is one this driver leaves alone.
	 */
	unsigned char port_major[XHCI_MAX_PORTS];

	/* The structures shared with the controller (xhci_ring.c). */
	vir_bytes dcbaa;		/* device context base address array */
	phys_bytes dcbaa_phys;
	vir_bytes spad_arr;		/* scratchpad buffer array */
	phys_bytes spad_arr_phys;
	vir_bytes spad;			/* and the scratchpad itself */
	phys_bytes spad_phys;
	size_t spad_size;

	struct xhci_ring cmd;		/* the command ring */

	vir_bytes erst;			/* the event ring segment table */
	phys_bytes erst_phys;
	vir_bytes event;		/* and its one segment */
	phys_bytes event_phys;
	unsigned event_slots;
	unsigned event_deq;		/* where the driver reads next */
	unsigned event_cycle;		/* and which cycle bit means "mine" */

	struct xhci_device dev[XHCI_MAX_PORTS];
};

extern struct xhci xhci;
extern struct log xhci_log;

/* xhci_find.c */
int xhci_find(struct xhci_devinfo *info, int skip);

/* xhci_rk.c */
int xhci_rk_map(void);
int xhci_rk_power_domain(void);
void xhci_rk_clocks_on(void);
void xhci_rk_phy_init(void);
int xhci_rk_dwc3_init(void);
void xhci_rk_report(void);

/* xhci_ring.c */
int xhci_dma_alloc(void);
void xhci_dma_free(void);
void *xhci_alloc_dma(size_t size, phys_bytes *phys, const char *what);
void xhci_cache(int op, void *addr, size_t len, const char *what);
extern unsigned long xhci_t_cache, xhci_n_cache;
extern unsigned long xhci_t_poll, xhci_n_poll;
extern unsigned long xhci_t_setup, xhci_t_wire, xhci_n_wire;
extern unsigned long xhci_t_small, xhci_n_small;
extern unsigned long xhci_n_irq, xhci_n_alarm;
int xhci_ring_setup(struct xhci_ring *r, const char *what);
phys_bytes xhci_ring_push(struct xhci_ring *r, uint32_t p0, uint32_t p1,
	uint32_t status, uint32_t control);
void xhci_ring_free(struct xhci_ring *r);
int xhci_start(void);
void xhci_halt(void);
int xhci_cmd(uint32_t p0, uint32_t p1, uint32_t status, uint32_t control,
	struct xhci_trb *ev);
int xhci_cmd_noop(void);
int xhci_cmd_noop_quiet(void);
int xhci_port_reset(unsigned port);
int xhci_events_drain(unsigned usec, struct xhci_trb *want,
	unsigned want_type);

/* xhci_dev.c */
int xhci_device_attach(unsigned port, struct xhci_device *dev);
void xhci_device_free(struct xhci_device *dev);
int xhci_control(struct xhci_device *dev, uint8_t request_type,
	uint8_t request, uint16_t value, uint16_t index, uint16_t length,
	unsigned *actual);
int xhci_device_attach_hub(struct xhci_device *hub, unsigned hubport,
	unsigned speed, struct xhci_device *dev);
struct xhci_ep *xhci_device_ep(struct xhci_device *dev, unsigned num,
	int dir_in);
int xhci_transfer(struct xhci_device *dev, struct xhci_ep *ep, size_t length,
	unsigned *actual);
phys_bytes xhci_transfer_start(struct xhci_device *dev, struct xhci_ep *ep,
	size_t length);
phys_bytes xhci_control_start(struct xhci_device *dev, uint8_t request_type,
	uint8_t request, uint16_t value, uint16_t index, uint16_t length);
void xhci_doorbell(struct xhci_device *dev, unsigned target);

/* xhci_urb.c */
void xhci_urb_message(message *m);
void xhci_urb_announce(struct xhci_device *dev);
void xhci_urb_stats(unsigned every);
int xhci_urb_transfer_event(const struct xhci_trb *ev);
int xhci_urb_busy(void);
void xhci_urb_tick(void);

/* xhci.c */
void xhci_defer(message *m, int ipc_status);
struct xhci_device *xhci_device_by_id(unsigned id);

/* Register access; every block is reached the same way. */
static inline uint32_t
xhci_rd(vir_bytes block, unsigned off)
{
	return *(volatile uint32_t *)(block + off);
}

static inline void
xhci_wr(vir_bytes block, unsigned off, uint32_t val)
{
	*(volatile uint32_t *)(block + off) = val;
}

/*
 * Rockchip's control registers carry their own write mask in the top half:
 * the write says which bits of the bottom half it means.  So none of the
 * glue is read-modify-write, and none of it can lose a neighbouring field
 * to whoever else writes the same register.
 */
static inline uint32_t
rk_hiword(uint32_t value, uint32_t mask, unsigned shift)
{
	return ((mask << shift) << 16) | ((value & mask) << shift);
}

#endif /* _XHCI_H */
