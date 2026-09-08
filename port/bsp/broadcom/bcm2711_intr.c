/*
 * GIC-400 (ARM GICv2) interrupt controller driver for BCM2711.
 *
 * Modelled on bsp/ti/omap_intr.c, which drives the TI INTCPS. The interface
 * the kernel expects is the same; only the hardware differs.
 *
 * IRQ numbers used throughout MINIX are raw GIC INTIDs:
 *	0..15	SGI  (software generated, unused here)
 *	16..31	PPI  (per-CPU, e.g. the ARM generic timer)
 *	32..	SPI  (shared peripherals; device tree calls these GIC_SPI n
 *		      where INTID = n + 32)
 *
 * Unlike the TI controller, the GIC splits into two register blocks: the
 * distributor (global routing and masking) and the CPU interface (per-core
 * acknowledge and end-of-interrupt). They live on different pages and are
 * mapped separately.
 */

#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "hw_intr.h"

#include "bcm2711_registers.h"

static struct bcm2711_gic
{
	vir_bytes dist_base;
	vir_bytes dist_size;
	vir_bytes cpu_base;
	vir_bytes cpu_size;
	int nr_irqs;
} bcm2711_gic;

static kern_phys_map gicd_phys_map;
static kern_phys_map gicc_phys_map;

int
intr_init(const int auto_eoi)
{
	u32_t typer;
	int i;

	if (!BOARD_IS_RPI4(machine.board_id)) {
		panic("Can not do the interrupt setup. "
		    "machine (0x%08x) is unknown\n", machine.board_id);
	}

	bcm2711_gic.dist_base = BCM2711_GICD_BASE;
	bcm2711_gic.dist_size = BCM2711_GICD_SIZE;
	bcm2711_gic.cpu_base = BCM2711_GICC_BASE;
	bcm2711_gic.cpu_size = BCM2711_GICC_SIZE;

	kern_phys_map_ptr(bcm2711_gic.dist_base, bcm2711_gic.dist_size,
	    VMMF_UNCACHED | VMMF_WRITE, &gicd_phys_map,
	    (vir_bytes) & bcm2711_gic.dist_base);

	kern_phys_map_ptr(bcm2711_gic.cpu_base, bcm2711_gic.cpu_size,
	    VMMF_UNCACHED | VMMF_WRITE, &gicc_phys_map,
	    (vir_bytes) & bcm2711_gic.cpu_base);

	/* Take the distributor down while we reprogram it. */
	mmio_write(bcm2711_gic.dist_base + GICD_CTLR, 0);

	/*
	 * GICD_TYPER tells us how many interrupt lines this GIC implements,
	 * in blocks of 32. Trust the hardware rather than a constant: the
	 * QEMU virt machine and the real BCM2711 report different counts.
	 */
	typer = mmio_read(bcm2711_gic.dist_base + GICD_TYPER);
	bcm2711_gic.nr_irqs = GICD_TYPER_ITLINES(typer) * 32;
	if (bcm2711_gic.nr_irqs > GIC_MAX_INTID)
		bcm2711_gic.nr_irqs = GIC_MAX_INTID;

	/*
	 * Disable and clear every SPI. The first 32 INTIDs (SGI and PPI) are
	 * banked per CPU and their enable/config registers are read-only or
	 * reserved here, so they are skipped.
	 */
	for (i = GIC_SPI_BASE; i < bcm2711_gic.nr_irqs; i += 32) {
		mmio_write(bcm2711_gic.dist_base + GICD_ICENABLER(i / 32),
		    0xffffffff);
		mmio_write(bcm2711_gic.dist_base + GICD_ICPENDR(i / 32),
		    0xffffffff);
	}

	/*
	 * Middling priority for everything. We do not use interrupt
	 * priorities: MINIX masks at the controller instead. The value must
	 * still be below the priority mask set on the CPU interface or
	 * nothing is ever delivered.
	 */
	for (i = GIC_SPI_BASE; i < bcm2711_gic.nr_irqs; i += 4) {
		mmio_write(bcm2711_gic.dist_base + GICD_IPRIORITYR(i / 4),
		    0xa0a0a0a0);
	}

	/* Route every SPI to CPU 0. MINIX on ARM is uniprocessor. */
	for (i = GIC_SPI_BASE; i < bcm2711_gic.nr_irqs; i += 4) {
		mmio_write(bcm2711_gic.dist_base + GICD_ITARGETSR(i / 4),
		    0x01010101);
	}

	/*
	 * Level-triggered, active high. Two bits per interrupt; 0b00 is
	 * level-sensitive. Every peripheral we care about on the BCM2711 is
	 * declared IRQ_TYPE_LEVEL_HIGH in the device tree.
	 */
	for (i = GIC_SPI_BASE; i < bcm2711_gic.nr_irqs; i += 16) {
		mmio_write(bcm2711_gic.dist_base + GICD_ICFGR(i / 16), 0);
	}

	mmio_write(bcm2711_gic.dist_base + GICD_CTLR, GICD_CTLR_ENABLE);

	/*
	 * CPU interface. PMR of 0xf0 lets through everything with a priority
	 * numerically below it, which covers the 0xa0 set above. BPR of 0
	 * disables preemption grouping, which we do not use.
	 */
	mmio_write(bcm2711_gic.cpu_base + GICC_PMR, 0xf0);
	mmio_write(bcm2711_gic.cpu_base + GICC_BPR, 0);
	mmio_write(bcm2711_gic.cpu_base + GICC_CTLR, GICC_CTLR_ENABLE);

	return 0;
}

void
bsp_irq_handle(void)
{
	/* Called from assembly to handle an interrupt. */
	u32_t iar;
	int irq;

	iar = mmio_read(bcm2711_gic.cpu_base + GICC_IAR);
	irq = iar & GICC_IAR_INTID_MASK;

	/*
	 * The GIC reports 1023 when there was nothing to acknowledge, which
	 * happens if the source went away between assertion and read. There
	 * is no EOI to write for a spurious interrupt.
	 */
	if (irq == GIC_SPURIOUS_INTID)
		return;

	irq_handle(irq);

	/*
	 * Write back the full IAR value, not just the INTID. For SGIs the
	 * upper bits carry the source CPU and the GIC needs them to retire
	 * the correct interrupt.
	 */
	mmio_write(bcm2711_gic.cpu_base + GICC_EOIR, iar);
}

void
bsp_irq_unmask(int irq)
{
	mmio_write(bcm2711_gic.dist_base + GICD_ISENABLER(irq / 32),
	    1 << (irq & 0x1f));
}

void
bsp_irq_mask(const int irq)
{
	mmio_write(bcm2711_gic.dist_base + GICD_ICENABLER(irq / 32),
	    1 << (irq & 0x1f));
}
