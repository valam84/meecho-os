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

	int irq_hook;

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

	vir_bytes cmd;			/* the command ring */
	phys_bytes cmd_phys;
	unsigned cmd_slots;
	unsigned cmd_enq;		/* where the driver writes next */
	unsigned cmd_cycle;		/* and with which cycle bit */

	vir_bytes erst;			/* the event ring segment table */
	phys_bytes erst_phys;
	vir_bytes event;		/* and its one segment */
	phys_bytes event_phys;
	unsigned event_slots;
	unsigned event_deq;		/* where the driver reads next */
	unsigned event_cycle;		/* and which cycle bit means "mine" */
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
int xhci_ring_alloc(void);
void xhci_ring_free(void);
int xhci_start(void);
int xhci_cmd_noop(void);
int xhci_cmd_noop_quiet(void);
int xhci_port_reset(unsigned port);
int xhci_events_drain(unsigned usec, struct xhci_trb *want,
	unsigned want_type);

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
