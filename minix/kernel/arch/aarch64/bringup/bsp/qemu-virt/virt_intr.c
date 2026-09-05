/*
 * GICv2 interrupt controller for the QEMU virt machine.
 *
 * Modelled on port/bsp/broadcom/bcm2711_intr.c, which drives the CM4's
 * GIC-400. That is the same GICv2 architecture, so the two differ only in
 * base addresses and in how many lines they implement - which this reads out
 * of the hardware rather than assuming.
 *
 * IRQ numbers are raw GIC INTIDs throughout:
 *	0..15	SGI  software generated, unused here
 *	16..31	PPI  per-CPU, which is where the generic timer arrives
 *	32..	SPI  shared peripherals; the device tree calls these
 *		     GIC_SPI n, where INTID = n + 32
 *
 * The GIC is two register blocks on separate pages: the distributor decides
 * what is enabled and where it goes, the CPU interface acknowledges and
 * retires. They are mapped separately.
 */

#include <stdint.h>

#include "bsp_intr.h"
#include "intr.h"
#include "kprint.h"
#include "mmio.h"
#include "mmu.h"

#include "virt_registers.h"

static struct virt_gic {
	uint64_t dist_base;
	uint64_t cpu_base;
	int nr_irqs;
} virt_gic;

int
intr_init(const int auto_eoi)
{
	uint32_t typer;
	int i;

	/*
	 * The generic kernel passes 0 here. Auto-EOI is a mode where the CPU
	 * interface retires an interrupt as it is acknowledged; MINIX does
	 * not use it, and bsp_irq_handle() below writes EOIR explicitly.
	 */
	(void)auto_eoi;

	mmu_map_device(VIRT_GICD_BASE, VIRT_GICD_SIZE, &virt_gic.dist_base);
	mmu_map_device(VIRT_GICC_BASE, VIRT_GICC_SIZE, &virt_gic.cpu_base);

	/* Take the distributor down while it is being reprogrammed. */
	mmio_write(virt_gic.dist_base + GICD_CTLR, 0);

	/*
	 * GICD_TYPER says how many lines this GIC implements, in blocks of
	 * 32. Trust the hardware: virt and the BCM2711 report different
	 * counts, and so does the same machine with a different -smp.
	 */
	typer = mmio_read(virt_gic.dist_base + GICD_TYPER);
	virt_gic.nr_irqs = GICD_TYPER_ITLINES(typer);
	if (virt_gic.nr_irqs > GIC_MAX_INTID)
		virt_gic.nr_irqs = GIC_MAX_INTID;

	/* Nothing enabled, nothing pending, including the banked SGIs/PPIs. */
	for (i = 0; i < virt_gic.nr_irqs; i += 32) {
		mmio_write(virt_gic.dist_base + GICD_ICENABLER(i / 32),
		    0xffffffff);
		mmio_write(virt_gic.dist_base + GICD_ICPENDR(i / 32),
		    0xffffffff);
	}

	/*
	 * Middling priority for every line, PPIs included. We do not use
	 * interrupt priorities - masking happens at the controller - but the
	 * value still has to be numerically below the CPU interface's
	 * priority mask or nothing is ever delivered. This loop starting at
	 * zero rather than at the first SPI is the difference between a timer
	 * that ticks and one that does not.
	 */
	for (i = 0; i < virt_gic.nr_irqs; i += 4) {
		mmio_write(virt_gic.dist_base + GICD_IPRIORITYR(i / 4),
		    0xa0a0a0a0);
	}

	/*
	 * Route every SPI to CPU 0; the kernel is uniprocessor for now.
	 * ITARGETSR for the first 32 INTIDs is read-only - they are banked
	 * per CPU and go to the core that took them - so start at the SPIs.
	 */
	for (i = GIC_SPI_BASE; i < virt_gic.nr_irqs; i += 4) {
		mmio_write(virt_gic.dist_base + GICD_ITARGETSR(i / 4),
		    0x01010101);
	}

	/* Level-triggered, active high: two bits each, 0b00 is level. */
	for (i = GIC_SPI_BASE; i < virt_gic.nr_irqs; i += 16)
		mmio_write(virt_gic.dist_base + GICD_ICFGR(i / 16), 0);

	mmio_write(virt_gic.dist_base + GICD_CTLR, GICD_CTLR_ENABLE);

	/*
	 * CPU interface. A priority mask of 0xf0 passes everything with a
	 * priority numerically below it, which covers the 0xa0 set above.
	 * Binary point 0 turns off preemption grouping, which we do not use.
	 */
	mmio_write(virt_gic.cpu_base + GICC_PMR, 0xf0);
	mmio_write(virt_gic.cpu_base + GICC_BPR, 0);
	mmio_write(virt_gic.cpu_base + GICC_CTLR, GICC_CTLR_ENABLE);

	return 0;
}

int
bsp_irq_lines(void)
{
	return virt_gic.nr_irqs;
}

void
bsp_irq_handle(void)
{
	uint32_t iar;
	int irq;

	iar = mmio_read(virt_gic.cpu_base + GICC_IAR);
	irq = (int)(iar & GICC_IAR_INTID_MASK);

	/*
	 * 1023 means there was nothing to acknowledge, which happens when the
	 * source goes away between asserting and being read. There is no EOI
	 * to write for one of those.
	 */
	if (irq == GIC_SPURIOUS_INTID)
		return;

	irq_handle(irq);

	/*
	 * Write back the whole IAR value rather than just the INTID: for SGIs
	 * the upper bits carry the source CPU, and the GIC needs them to
	 * retire the right interrupt.
	 */
	mmio_write(virt_gic.cpu_base + GICC_EOIR, iar);
}

void
bsp_irq_unmask(int irq)
{
	mmio_write(virt_gic.dist_base + GICD_ISENABLER(irq / 32),
	    1U << (irq & 0x1f));
}

void
bsp_irq_mask(const int irq)
{
	mmio_write(virt_gic.dist_base + GICD_ICENABLER(irq / 32),
	    1U << (irq & 0x1f));
}
