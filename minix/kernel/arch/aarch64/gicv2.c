/*
 * GICv2: one distributor and one memory-mapped CPU interface.
 *
 * This is the half of the old bsp/qemu-virt/virt_intr.c that pokes registers;
 * the device tree half of it is now shared with GICv3 and lives in gic.c.
 * The code is unchanged - it is the driver the system has been booting with
 * since stage 4 - and it stays because QEMU's virt machine presents a GICv2
 * unless asked otherwise, which makes it the default development target.
 */

#include <assert.h>
#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proto.h"

#include "arch_proto.h"
#include "hw_intr.h"
#include "gic.h"

/*
 * Which bit in a target list means which core, learned by each core from its
 * own banked copy of ITARGETSR. Zero for a core that has not come up.
 */
static u32_t gicv2_cpu_mask[CONFIG_MAX_CPUS];

/*===========================================================================*
 *				gicv2_init				     *
 *===========================================================================*/
void
gicv2_init(void)
{
	int i;

	assert(gic.cpu_base != 0);

	/* Take the distributor down while it is being reprogrammed. */
	mmio_write(gic.dist_base + GICD_CTLR, 0);

	/* Nothing enabled, nothing pending, banked SGIs and PPIs included. */
	for (i = 0; i < gic.nr_irqs; i += 32) {
		mmio_write(gic.dist_base + GICD_ICENABLER(i / 32), 0xffffffff);
		mmio_write(gic.dist_base + GICD_ICPENDR(i / 32), 0xffffffff);
	}

	/*
	 * Middling priority for every line, PPIs included. Interrupt
	 * priorities are not used - masking happens at the controller - but
	 * the value still has to be numerically below the CPU interface's
	 * priority mask or nothing is ever delivered. This loop starting at
	 * zero rather than at the first SPI is the difference between a timer
	 * that ticks and one that does not; it cost an evening once.
	 */
	for (i = 0; i < gic.nr_irqs; i += 4)
		mmio_write(gic.dist_base + GICD_IPRIORITYR(i / 4),
		    0xa0a0a0a0);

	/*
	 * Route every SPI to CPU 0. ITARGETSR for the first 32 INTIDs is
	 * read-only - those are banked per CPU and go to the core that took
	 * them - so start at the SPIs.
	 */
	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i += 4)
		mmio_write(gic.dist_base + GICD_ITARGETSR(i / 4),
		    0x01010101);

	/* Level-triggered, active high: two bits each, 0b00 is level. */
	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i += 16)
		mmio_write(gic.dist_base + GICD_ICFGR(i / 16), 0);

	mmio_write(gic.dist_base + GICD_CTLR, GICD_CTLR_ENABLE);

	gicv2_cpu_init();
}

/*===========================================================================*
 *				gicv2_cpu_init				     *
 *===========================================================================*/
/*
 * The part every core does for itself: its own CPU interface, and the first
 * 32 INTIDs, which the distributor banks per core even though they are read
 * and written at the same addresses.
 */
void
gicv2_cpu_init(void)
{
	/*
	 * Which bit in ITARGETSR means this core. The first eight of those
	 * registers are banked and read back the reading core's own bit, which
	 * is the only way to learn it - the mapping from a logical CPU number
	 * to a CPU interface number is not written down anywhere else.
	 */
	gicv2_cpu_mask[cpuid] = mmio_read(gic.dist_base + GICD_ITARGETSR(0))
	    & 0xff;

	/* The software generated interrupts this kernel uses for its IPIs. */
	mmio_write(gic.dist_base + GICD_ISENABLER(0),
	    (1U << GIC_IPI_SCHED) | (1U << GIC_IPI_HALT));

	/*
	 * A priority mask of 0xf0 passes everything with a priority
	 * numerically below it, which covers the 0xa0 every line was given.
	 * Binary point 0 turns off preemption grouping, which is unused.
	 */
	mmio_write(gic.cpu_base + GICC_PMR, GIC_PRIORITY_MASK);
	mmio_write(gic.cpu_base + GICC_BPR, 0);
	mmio_write(gic.cpu_base + GICC_CTLR, GICC_CTLR_ENABLE);
}

#ifdef CONFIG_SMP
/*===========================================================================*
 *				gicv2_send_ipi				     *
 *===========================================================================*/
void
gicv2_send_ipi(unsigned cpu, int sgi)
{
	u32_t mask = gicv2_cpu_mask[cpu];

	assert(mask != 0);

	/*
	 * Target list filter 0 means "the cores in the list", and the list is
	 * a bitmap of CPU interfaces - not of logical CPU numbers, which is
	 * why each core recorded its own bit above.
	 */
	mmio_write(gic.dist_base + GICD_SGIR,
	    (mask << GICD_SGIR_TARGET_SHIFT) | (u32_t)sgi);
}
#endif /* CONFIG_SMP */

/*===========================================================================*
 *				gicv2_handle				     *
 *===========================================================================*/
void
gicv2_handle(void)
{
	u32_t iar;
	int irq;

	iar = mmio_read(gic.cpu_base + GICC_IAR);
	irq = (int)(iar & GICV2_INTID_MASK);

	/*
	 * 1023 means there was nothing to acknowledge, which happens when the
	 * source goes away between asserting and being read. There is no EOI
	 * to write for one of those.
	 */
	if (irq >= GIC_FIRST_SPECIAL_INTID)
		return;

	gic_dispatch(irq);

	/*
	 * Write the whole IAR value back rather than just the INTID: for SGIs
	 * the upper bits carry the source CPU, and the GIC needs them to
	 * retire the right interrupt.
	 */
	mmio_write(gic.cpu_base + GICC_EOIR, iar);
}

/*===========================================================================*
 *				gicv2_unmask				     *
 *===========================================================================*/
void
gicv2_unmask(int irq)
{
	mmio_write(gic.dist_base + GICD_ISENABLER(irq / 32),
	    1U << (irq & 0x1f));
}

/*===========================================================================*
 *				gicv2_mask				     *
 *===========================================================================*/
void
gicv2_mask(int irq)
{
	mmio_write(gic.dist_base + GICD_ICENABLER(irq / 32),
	    1U << (irq & 0x1f));
}
