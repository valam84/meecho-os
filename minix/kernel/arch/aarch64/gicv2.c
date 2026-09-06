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

	/*
	 * CPU interface. A priority mask of 0xf0 passes everything with a
	 * priority numerically below it, which covers the 0xa0 set above.
	 * Binary point 0 turns off preemption grouping, which is unused.
	 */
	mmio_write(gic.cpu_base + GICC_PMR, GIC_PRIORITY_MASK);
	mmio_write(gic.cpu_base + GICC_BPR, 0);
	mmio_write(gic.cpu_base + GICC_CTLR, GICC_CTLR_ENABLE);
}

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

	/*
	 * The generic dispatcher masks the line, runs the hook chain and
	 * unmasks it again if the handlers say so. It asserts the number is
	 * inside its tables, so a GIC reporting more lines than
	 * NR_IRQ_VECTORS must not reach it - intr_init() clamped nr_irqs, but
	 * a controller can still report an INTID above that.
	 */
	if (irq < NR_IRQ_VECTORS)
		irq_handle(irq);

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
