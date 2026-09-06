#ifndef _AARCH64_GIC_H_
#define _AARCH64_GIC_H_

/*
 * The ARM Generic Interrupt Controller, versions 2 and 3.
 *
 * This is not board support, which is why it does not live in bsp/. Every
 * AArch64 machine has a GIC; what differs between them is the version and
 * the addresses, and both of those are read out of the device tree at boot -
 * the same arrangement psci.c uses for the conduit, and for the same reason.
 * Choosing at build time would be wrong even for a single board: QEMU's virt
 * machine presents a GICv2 by default and a GICv3 with "gic-version=3", and
 * that is one machine, not two.
 *
 * IRQ numbers are raw GIC INTIDs throughout, which is also what the generic
 * kernel's tables are sized for (NR_IRQ_VECTORS in <machine/interrupt.h>):
 *
 *	0..15	SGI  software generated, used for IPIs when there is SMP
 *	16..31	PPI  per-CPU, which is where the generic timer arrives
 *	32..	SPI  shared peripherals; the device tree calls these
 *		     GIC_SPI n, where INTID = n + 32
 *
 *
 * What actually differs between the two versions
 * ----------------------------------------------
 * The distributor is nearly the same, and everything below it is not:
 *
 *   GICv2 has one memory-mapped CPU interface shared by every core. It is
 *   acknowledged and retired with loads and stores, and SGIs and PPIs are
 *   banked inside the distributor, so all four operations touch the same two
 *   register blocks.
 *
 *   GICv3 has no CPU interface in memory at all: acknowledging and retiring
 *   are reads and writes of the ICC_* system registers, and each core has a
 *   redistributor of its own that owns its SGIs and PPIs. So enabling the
 *   timer - a PPI - is a write to this core's redistributor rather than to
 *   the distributor, which is the difference between a timer that ticks and
 *   one that does not.
 *
 * The compatibility mode GICv3 defines for running GICv2 software is
 * optional, and the target board does not offer it: RK3566's node has two
 * register ranges, the distributor and the redistributors, where a GICv2
 * node has four. There is no CPU interface to point the older driver at.
 */

#include <minix/type.h>

/* Where the three classes of interrupt ID begin. */
#define GIC_SGI_BASE		0	/* software generated */
#define GIC_PPI_BASE		16	/* per-CPU, where the timer arrives */
#define GIC_SPI_BASE		32	/* shared peripherals */

/*
 * INTIDs 1020..1023 are reserved and none of them is a real interrupt; 1023
 * is what an acknowledge reads when the source went away between asserting
 * and being read. None of them is retired with an end-of-interrupt.
 */
#define GIC_FIRST_SPECIAL_INTID	1020

/* GICv2 acknowledges with 10 bits of INTID, GICv3 with 24. */
#define GICV2_INTID_MASK	0x3ff
#define GICV3_INTID_MASK	0xffffff

/* ======================================================================
 * Distributor: what is enabled, at what priority, and where it goes.
 * Common to both versions except where marked.
 * ====================================================================== */

#define GICD_CTLR		0x0000
#define GICD_TYPER		0x0004
#define GICD_IIDR		0x0008
#define GICD_IGROUPR(n)		(0x0080 + 4 * (n))
#define GICD_ISENABLER(n)	(0x0100 + 4 * (n))
#define GICD_ICENABLER(n)	(0x0180 + 4 * (n))
#define GICD_ISPENDR(n)		(0x0200 + 4 * (n))
#define GICD_ICPENDR(n)		(0x0280 + 4 * (n))
#define GICD_IPRIORITYR(n)	(0x0400 + 4 * (n))
#define GICD_ITARGETSR(n)	(0x0800 + 4 * (n))	/* GICv2 */
#define GICD_ICFGR(n)		(0x0c00 + 4 * (n))
#define GICD_IROUTER(n)		(0x6000 + 8 * (n))	/* GICv3 */

#define GICD_CTLR_ENABLE	(1 << 0)	/* GICv2 */

/*
 * GICv3, as the Non-secure world sees the register with affinity routing on:
 * two group enables and the switch that turns affinity routing on in the
 * first place. RWP is "register write pending" - a disable or a clear has
 * been accepted but is not yet in force everywhere.
 */
#define GICD_CTLR_ENABLE_G1	(1 << 0)
#define GICD_CTLR_ENABLE_G1A	(1 << 1)
#define GICD_CTLR_ARE_NS	(1 << 4)
#define GICD_CTLR_RWP		(1U << 31)

/* GICD_TYPER: ITLinesNumber is bits [4:0]; supported INTIDs = 32 * (N + 1) */
#define GICD_TYPER_ITLINES(v)	((((v) & 0x1f) + 1) * 32)

/* ======================================================================
 * GICv2 CPU interface: acknowledge and retire.
 * ====================================================================== */

#define GICC_CTLR		0x0000
#define GICC_PMR		0x0004	/* priority mask */
#define GICC_BPR		0x0008	/* binary point */
#define GICC_IAR		0x000c	/* interrupt acknowledge */
#define GICC_EOIR		0x0010	/* end of interrupt */

#define GICC_CTLR_ENABLE	(1 << 0)

/*
 * GICv2 software generated interrupts: the distributor sends them, and the
 * target is a bitmap of CPU interfaces in the upper half of the word.
 */
#define GICD_SGIR		0x0f00
#define GICD_SGIR_TARGET_SHIFT	16

/* ======================================================================
 * GICv3 redistributor: one per core, two 64 KiB frames.
 *
 * The first frame answers for the core itself - is it awake, is a write
 * still pending - and the second owns its SGIs and PPIs, with the same
 * register layout the distributor uses for SPIs.
 * ====================================================================== */

#define GICR_FRAME_SIZE		0x20000		/* both frames together */
#define GICR_SGI_OFFSET		0x10000		/* second frame */

/* First frame. */
#define GICR_CTLR		0x0000
#define GICR_TYPER		0x0008		/* 64-bit */
#define GICR_WAKER		0x0014

#define GICR_CTLR_RWP		(1 << 3)

/*
 * GICR_TYPER, low word: VLPIS says the redistributor has two more frames for
 * virtual LPIs, which changes the stride; Last says this is the final
 * redistributor in the region.
 */
#define GICR_TYPER_VLPIS	(1 << 1)
#define GICR_TYPER_LAST		(1 << 4)

/* The high word is the affinity of the core this redistributor belongs to. */
#define GICR_TYPER_AFFINITY	4

#define GICR_WAKER_SLEEP	(1 << 1)	/* ProcessorSleep */
#define GICR_WAKER_ASLEEP	(1 << 2)	/* ChildrenAsleep */

/*
 * Second frame, for INTIDs 0..31. These are offsets from the start of that
 * frame, not from the redistributor, because that is what gic.sgi_base
 * points at - the same layout the distributor uses for the shared
 * interrupts, which is the point of them having the same names.
 */
#define GICR_IGROUPR0		0x0080
#define GICR_ISENABLER0		0x0100
#define GICR_ICENABLER0		0x0180
#define GICR_ICPENDR0		0x0280
#define GICR_IPRIORITYR(n)	(0x0400 + 4 * (n))
#define GICR_ICFGR0		0x0c00
#define GICR_ICFGR1		0x0c04

/* ICC_SRE_EL1.SRE: use the system registers rather than a memory interface. */
#define ICC_SRE_EL1_SRE		(1 << 0)

/* ICC_CTLR_EL1.EOImode: 0 means one write both drops priority and retires. */
#define ICC_CTLR_EL1_EOIMODE	(1 << 1)

/*
 * Priorities are not used for anything - masking happens at the controller -
 * but a line still has to be numerically below the CPU interface's priority
 * mask or it is never delivered.
 */
#define GIC_PRIORITY_DEFAULT	0xa0
#define GIC_PRIORITY_MASK	0xf0

/* ======================================================================
 * What the probe found, and the two halves that act on it.
 * ====================================================================== */

struct gic {
	int version;			/* 2 or 3, from the device tree */
	int nr_irqs;			/* INTIDs this GIC implements */

	vir_bytes dist_base;		/* distributor, both versions */
	vir_bytes cpu_base;		/* GICv2 CPU interface */

	vir_bytes redist_base;		/* GICv3 redistributor region */
	vir_bytes redist_size;
	vir_bytes redist_stride;	/* 0 when the tree does not say */

	/*
	 * This core's redistributor frames. Worked out in intr_init() rather
	 * than in the probe, because finding them means reading GICR_TYPER,
	 * and that needs the mapping the probe is only registering.
	 */
	vir_bytes rd_base;
	vir_bytes sgi_base;
};

extern struct gic gic;

/*
 * The two software generated interrupts this kernel uses, matching the two
 * IPIs the generic SMP code knows about. Numbers rather than names in the
 * hardware: an SGI is just an INTID below 16, so these arrive through
 * bsp_irq_handle() like everything else and are recognised there.
 */
#define GIC_IPI_SCHED		0
#define GIC_IPI_HALT		1

/* gicv2.c */
void gicv2_init(void);
void gicv2_cpu_init(void);
void gicv2_handle(void);
void gicv2_mask(int irq);
void gicv2_unmask(int irq);
void gicv2_send_ipi(unsigned cpu, int sgi);

/* gicv3.c */
void gicv3_init(void);
void gicv3_cpu_init(void);
void gicv3_handle(void);
void gicv3_mask(int irq);
void gicv3_unmask(int irq);
void gicv3_send_ipi(unsigned cpu, int sgi);

/*
 * What every core has to do for itself. The distributor is set up once, by
 * the boot core; the CPU interface, the redistributor and the banked SGI and
 * PPI registers are per-core and nobody can do them on another core's behalf.
 */
void gic_cpu_init(void);

/* Poke another core. */
void gic_send_ipi(unsigned cpu, int sgi);

/* Where an acknowledged interrupt goes, IPIs included. */
void gic_dispatch(int irq);

#endif /* _AARCH64_GIC_H_ */
