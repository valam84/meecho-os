/*
 * GICv2 interrupt controller.
 *
 * IRQ numbers are raw GIC INTIDs throughout, which is also what the generic
 * kernel's tables are sized for (NR_IRQ_VECTORS in <machine/interrupt.h>):
 *
 *	0..15	SGI  software generated, unused here
 *	16..31	PPI  per-CPU, which is where the generic timer arrives
 *	32..	SPI  shared peripherals; the device tree calls these
 *		     GIC_SPI n, where INTID = n + 32
 *
 * The GIC is two register blocks on separate pages: the distributor decides
 * what is enabled and where it goes, the CPU interface acknowledges and
 * retires. They are mapped separately, and where they are comes from the
 * device tree - see virt_registers.h for why this and the console differ.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/vm.h"
#include "kernel/proto.h"

#include "arch_proto.h"
#include "hw_intr.h"
#include "fdt.h"

#include "bsp_intr.h"
#include "virt_registers.h"

static struct virt_gic {
	vir_bytes dist_base;
	vir_bytes cpu_base;
	int nr_irqs;
} virt_gic;

static kern_phys_map gicd_phys_map, gicc_phys_map;

/*===========================================================================*
 *				find_gic				     *
 *===========================================================================*/
/*
 * The GIC node, and the two ranges in its reg property.
 *
 * Matched on compatible rather than on the node's name, and on the generic
 * string "arm,cortex-a15-gic" that every GICv2 binding carries - a GIC-400
 * lists "arm,gic-400" first and that one second, so one comparison covers
 * both the emulated machine and the real board.
 *
 * Only two things are read here, and everything the kernel needs beyond that
 * it asks the hardware for; see GICD_TYPER below.
 */
struct gic_scan {
	unsigned addr_cells;
	unsigned size_cells;
	phys_bytes dist, dist_size;
	phys_bytes cpu, cpu_size;
	int found;
};

static int
compatible_with(const struct fdt_node *node, const char *want)
{
	const char *list;
	unsigned len, off;

	if ((list = fdt_getprop(node, "compatible", &len)) == NULL)
		return 0;

	for (off = 0; off < len; off += strlen(list + off) + 1)
		if (strcmp(list + off, want) == 0)
			return 1;

	return 0;
}

static int
find_gic(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct gic_scan *s = cookie;
	const char *reg;
	unsigned len, step;
	const void *p;

	if (depth == 0) {
		/*
		 * How wide an address and a length are in a child's reg
		 * property. The specification's defaults are 2 and 1; every
		 * AArch64 tree in practice says so explicitly, but a silent
		 * tree means the defaults.
		 */
		s->addr_cells = 2;
		s->size_cells = 1;
		if ((p = fdt_getprop(node, "#address-cells", &len)) != NULL &&
		    len == 4)
			s->addr_cells = (unsigned)fdt_read_cells(p, 1);
		if ((p = fdt_getprop(node, "#size-cells", &len)) != NULL &&
		    len == 4)
			s->size_cells = (unsigned)fdt_read_cells(p, 1);
		return 0;
	}

	if (depth != 1 || !compatible_with(node, "arm,cortex-a15-gic"))
		return 0;

	if ((reg = fdt_getprop(node, "reg", &len)) == NULL)
		return 0;

	step = 4 * (s->addr_cells + s->size_cells);

	/*
	 * The binding orders the ranges: distributor first, CPU interface
	 * second. Anything after them - the virtual interface control and CPU
	 * blocks - belongs to a hypervisor and is not ours.
	 */
	if (step == 0 || len < 2 * step)
		return 0;

	s->dist = (phys_bytes)fdt_read_cells(reg, s->addr_cells);
	s->dist_size = (phys_bytes)fdt_read_cells(reg + 4 * s->addr_cells,
	    s->size_cells);
	s->cpu = (phys_bytes)fdt_read_cells(reg + step, s->addr_cells);
	s->cpu_size = (phys_bytes)fdt_read_cells(reg + step +
	    4 * s->addr_cells, s->size_cells);

	s->found = 1;

	return 1;	/* stop the walk: this was the node */
}

/*===========================================================================*
 *				bsp_intr_pre_init			     *
 *===========================================================================*/
void
bsp_intr_pre_init(void)
{
	struct gic_scan scan;
	const void *dtb;

	memset(&scan, 0, sizeof(scan));

	/*
	 * Runs inside pre_init(), so the tree is still at its physical
	 * address and so is everything else; phys2vir() is not usable yet and
	 * is not needed.
	 */
	assert(boot_dtb != 0);
	dtb = (const void *)boot_dtb;

	if (!fdt_valid(dtb))
		panic("no device tree: nothing can say where the GIC is");

	(void)fdt_walk(dtb, find_gic, &scan);

	if (!scan.found)
		panic("device tree describes no GICv2");

	virt_gic.dist_base = (vir_bytes)scan.dist;
	virt_gic.cpu_base = (vir_bytes)scan.cpu;

	/*
	 * Register both ranges the way every driver does: the base lives in a
	 * variable, and the address in it is rewritten once the mapping is in
	 * force. Until then it holds the physical address, which is the right
	 * answer while the MMU is off.
	 */
	kern_phys_map_ptr(scan.dist, scan.dist_size,
	    VMMF_UNCACHED | VMMF_WRITE, &gicd_phys_map,
	    (vir_bytes)&virt_gic.dist_base);
	kern_phys_map_ptr(scan.cpu, scan.cpu_size,
	    VMMF_UNCACHED | VMMF_WRITE, &gicc_phys_map,
	    (vir_bytes)&virt_gic.cpu_base);
}

/*===========================================================================*
 *				intr_init				     *
 *===========================================================================*/
int
intr_init(int auto_eoi)
{
	u32_t typer;
	int i;

	/*
	 * The generic kernel passes 0. Auto-EOI is a mode where the CPU
	 * interface retires an interrupt as it is acknowledged; MINIX does
	 * not use it, and bsp_irq_handle() writes EOIR explicitly.
	 */
	(void)auto_eoi;

	assert(virt_gic.dist_base != 0);
	assert(virt_gic.cpu_base != 0);

	/* Take the distributor down while it is being reprogrammed. */
	mmio_write(virt_gic.dist_base + GICD_CTLR, 0);

	/*
	 * GICD_TYPER says how many lines this GIC implements, in blocks of
	 * 32. Asked of the hardware rather than assumed: virt and the BCM2711
	 * report different counts, and so does the same machine with a
	 * different -smp.
	 */
	typer = mmio_read(virt_gic.dist_base + GICD_TYPER);
	virt_gic.nr_irqs = GICD_TYPER_ITLINES(typer);
	if (virt_gic.nr_irqs > NR_IRQ_VECTORS)
		virt_gic.nr_irqs = NR_IRQ_VECTORS;

	/* Nothing enabled, nothing pending, banked SGIs and PPIs included. */
	for (i = 0; i < virt_gic.nr_irqs; i += 32) {
		mmio_write(virt_gic.dist_base + GICD_ICENABLER(i / 32),
		    0xffffffff);
		mmio_write(virt_gic.dist_base + GICD_ICPENDR(i / 32),
		    0xffffffff);
	}

	/*
	 * Middling priority for every line, PPIs included. Interrupt
	 * priorities are not used - masking happens at the controller - but
	 * the value still has to be numerically below the CPU interface's
	 * priority mask or nothing is ever delivered. This loop starting at
	 * zero rather than at the first SPI is the difference between a timer
	 * that ticks and one that does not; it cost an evening once.
	 */
	for (i = 0; i < virt_gic.nr_irqs; i += 4)
		mmio_write(virt_gic.dist_base + GICD_IPRIORITYR(i / 4),
		    0xa0a0a0a0);

	/*
	 * Route every SPI to CPU 0. ITARGETSR for the first 32 INTIDs is
	 * read-only - those are banked per CPU and go to the core that took
	 * them - so start at the SPIs.
	 */
	for (i = GIC_SPI_BASE; i < virt_gic.nr_irqs; i += 4)
		mmio_write(virt_gic.dist_base + GICD_ITARGETSR(i / 4),
		    0x01010101);

	/* Level-triggered, active high: two bits each, 0b00 is level. */
	for (i = GIC_SPI_BASE; i < virt_gic.nr_irqs; i += 16)
		mmio_write(virt_gic.dist_base + GICD_ICFGR(i / 16), 0);

	mmio_write(virt_gic.dist_base + GICD_CTLR, GICD_CTLR_ENABLE);

	/*
	 * CPU interface. A priority mask of 0xf0 passes everything with a
	 * priority numerically below it, which covers the 0xa0 set above.
	 * Binary point 0 turns off preemption grouping, which is unused.
	 */
	mmio_write(virt_gic.cpu_base + GICC_PMR, 0xf0);
	mmio_write(virt_gic.cpu_base + GICC_BPR, 0);
	mmio_write(virt_gic.cpu_base + GICC_CTLR, GICC_CTLR_ENABLE);

	return 0;
}

/*===========================================================================*
 *				bsp_irq_lines				     *
 *===========================================================================*/
int
bsp_irq_lines(void)
{
	return virt_gic.nr_irqs;
}

/*===========================================================================*
 *				bsp_irq_handle				     *
 *===========================================================================*/
void
bsp_irq_handle(void)
{
	u32_t iar;
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
	mmio_write(virt_gic.cpu_base + GICC_EOIR, iar);
}

/*===========================================================================*
 *				bsp_irq_unmask				     *
 *===========================================================================*/
void
bsp_irq_unmask(int irq)
{
	mmio_write(virt_gic.dist_base + GICD_ISENABLER(irq / 32),
	    1U << (irq & 0x1f));
}

/*===========================================================================*
 *				bsp_irq_mask				     *
 *===========================================================================*/
void
bsp_irq_mask(int irq)
{
	mmio_write(virt_gic.dist_base + GICD_ICENABLER(irq / 32),
	    1U << (irq & 0x1f));
}
