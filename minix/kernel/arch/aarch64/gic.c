/*
 * Finding the GIC in the device tree, and handing the six calls of
 * <bsp_intr.h> to whichever version is there.
 *
 * The version is decided once, at probe time, and remembered as a number
 * rather than as a table of function pointers. That is not a style
 * preference. The probe runs inside pre_init(), before the MMU is on, and a
 * pointer stored there would hold the physical address of the table; after
 * the move to the upper half, calling through it would jump to an address
 * that no longer exists. An int survives the move because it is not an
 * address. See PORTING-LOG.md, stage 2.3, for the general rule.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "kernel/debug.h"

#include "arch_proto.h"
#include "hw_intr.h"
#include <minix/fdt.h>
#include "gic.h"

#include "bsp_intr.h"

struct gic gic;

static kern_phys_map gicd_phys_map, gicr_phys_map;

#define compatible_with(node, want)	fdt_node_is_compatible(node, want)

/*===========================================================================*
 *				find_gic				     *
 *===========================================================================*/
/*
 * The GIC node, its version, and the two ranges in its reg property.
 *
 * Matched on compatible rather than on the node's name, and on the generic
 * strings rather than on a particular part: every GICv2 binding carries
 * "arm,cortex-a15-gic" - a GIC-400 lists "arm,gic-400" first and that one
 * second - and every GICv3 carries "arm,gic-v3", whether it is a GIC-500, a
 * GIC-600 or what QEMU emulates.
 *
 * The two ranges are ordered by the binding and mean different things per
 * version: distributor first in both, then the CPU interface on GICv2 and
 * the redistributors on GICv3. Anything after them belongs to a hypervisor
 * and is not ours.
 *
 * Everything else the kernel needs it asks the hardware for.
 */
struct gic_scan {
	unsigned addr_cells;
	unsigned size_cells;
	int version;
	phys_bytes dist, dist_size;
	phys_bytes second, second_size;
	vir_bytes stride;
	int regions;
	int found;
};

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

	if (depth != 1)
		return 0;

	if (compatible_with(node, "arm,gic-v3"))
		s->version = 3;
	else if (compatible_with(node, "arm,cortex-a15-gic"))
		s->version = 2;
	else
		return 0;

	if ((reg = fdt_getprop(node, "reg", &len)) == NULL)
		return 0;

	step = 4 * (s->addr_cells + s->size_cells);

	if (step == 0 || len < 2 * step)
		return 0;

	s->dist = (phys_bytes)fdt_read_cells(reg, s->addr_cells);
	s->dist_size = (phys_bytes)fdt_read_cells(reg + 4 * s->addr_cells,
	    s->size_cells);
	s->second = (phys_bytes)fdt_read_cells(reg + step, s->addr_cells);
	s->second_size = (phys_bytes)fdt_read_cells(reg + step +
	    4 * s->addr_cells, s->size_cells);

	/*
	 * GICv3 may split its redistributors across several ranges, one per
	 * discontiguous block of cores. Neither QEMU's virt nor RK3566 does -
	 * both describe one - and handling more would mean carrying a list
	 * through pre_init() with nothing to allocate it from. Say so rather
	 * than silently using the first.
	 */
	s->regions = 1;
	if ((p = fdt_getprop(node, "#redistributor-regions", &len)) != NULL &&
	    len == 4)
		s->regions = (int)fdt_read_cells(p, 1);

	/*
	 * The stride between redistributors, when the tree overrides the
	 * architectural one. Rare, and read here so that it is the tree that
	 * decides rather than an assumption.
	 */
	s->stride = 0;
	if ((p = fdt_getprop(node, "redistributor-stride", &len)) != NULL &&
	    len == 8)
		s->stride = (vir_bytes)fdt_read_cells(p, 2);

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
		panic("device tree describes no GIC this kernel can drive");

	if (scan.version == 3 && scan.regions != 1)
		panic("GICv3 with %d redistributor regions; this kernel "
		    "handles one", scan.regions);

	gic.version = scan.version;
	gic.dist_base = (vir_bytes)scan.dist;

	/*
	 * Register both ranges the way every driver does: the base lives in a
	 * variable, and the address in it is rewritten once the mapping is in
	 * force. Until then it holds the physical address, which is the right
	 * answer while the MMU is off.
	 */
	kern_phys_map_ptr(scan.dist, scan.dist_size,
	    VMMF_UNCACHED | VMMF_WRITE, &gicd_phys_map,
	    (vir_bytes)&gic.dist_base);

	if (gic.version == 2) {
		gic.cpu_base = (vir_bytes)scan.second;
		kern_phys_map_ptr(scan.second, scan.second_size,
		    VMMF_UNCACHED | VMMF_WRITE, &gicr_phys_map,
		    (vir_bytes)&gic.cpu_base);
	} else {
		gic.redist_base = (vir_bytes)scan.second;
		gic.redist_size = (vir_bytes)scan.second_size;
		gic.redist_stride = scan.stride;
		kern_phys_map_ptr(scan.second, scan.second_size,
		    VMMF_UNCACHED | VMMF_WRITE, &gicr_phys_map,
		    (vir_bytes)&gic.redist_base);
	}
}

/*===========================================================================*
 *				intr_init				     *
 *===========================================================================*/
int
intr_init(int auto_eoi)
{
	u32_t typer;

	/*
	 * The generic kernel passes 0. Auto-EOI is a mode where the CPU
	 * interface retires an interrupt as it is acknowledged; MINIX does
	 * not use it, and bsp_irq_handle() writes the end-of-interrupt
	 * explicitly.
	 */
	(void)auto_eoi;

	assert(gic.dist_base != 0);

	/*
	 * How many lines this GIC implements, in blocks of 32. Asked of the
	 * hardware rather than assumed: two machines report different counts,
	 * and so does the same machine with a different -smp. Shared, because
	 * GICD_TYPER means the same thing in both versions.
	 */
	typer = mmio_read(gic.dist_base + GICD_TYPER);
	gic.nr_irqs = GICD_TYPER_ITLINES(typer);
	if (gic.nr_irqs > NR_IRQ_VECTORS)
		gic.nr_irqs = NR_IRQ_VECTORS;

	switch (gic.version) {
	case 2:
		gicv2_init();
		break;
	case 3:
		gicv3_init();
		break;
	default:
		panic("intr_init: no GIC version, so nothing probed it");
	}

	return 0;
}

/*===========================================================================*
 *				gic_dispatch				     *
 *===========================================================================*/
/*
 * Where an acknowledged interrupt goes, whichever version acknowledged it.
 *
 * The two IPIs are recognised here rather than in the hook table because they
 * are not device interrupts: nothing registers a handler for them, and the
 * generic SMP code expects to be called directly. On i386 they never reach
 * this path at all - they have interrupt vectors of their own - which is why
 * the generic code has no idea this test exists.
 */
void
gic_dispatch(int irq)
{
#if DEBUG_BOOT_TRACE
	/*
	 * Whether anything arrives at all, and what - then, once, who is
	 * waiting for whom.
	 *
	 * A board that goes quiet cannot be asked. The first few of each line
	 * separate "the controller delivers nothing to EL1" from "interrupts
	 * arrive and the system is stuck for another reason". The timer
	 * having answered that on the CB2 - it ticks, and the system is still
	 * stuck - what remains is the process table, which is the dump worth
	 * having when a microkernel stops: every process is blocked on a
	 * message, and p_rts_flags with p_getfrom_e and p_sendto_e say on
	 * whose.
	 *
	 * Printed from the timer because there is no other way in: the keys
	 * that ask for this dump on other ports arrive through tty, and tty
	 * is exactly what does not work here yet.
	 */
	{
		static unsigned seen[NR_IRQ_VECTORS];
		static unsigned total;

		if (irq >= 0 && irq < NR_IRQ_VECTORS) {
			if (seen[irq] < 3 || (total % 100) == 0)
				printf("irq %d (#%u)\n", irq, total);
			seen[irq]++;
		}

		/* Ten seconds in at 100 Hz: long past the point where a boot
		 * that is going to finish has finished. */
		if (total == 1000 || total == 3000) {
			struct proc *rp;

			unsigned c, q;

			printf("--- procs at tick %u ---\n", total);
			for (rp = BEG_PROC_ADDR; rp < END_PROC_ADDR; rp++) {
				if (isemptyp(rp))
					continue;
				printf("%-8s ep=%d rts=%04x get=%d send=%d "
				    "cpu=%u pri=%d left=%u\n",
				    rp->p_name, rp->p_endpoint,
				    rp->p_rts_flags, rp->p_getfrom_e,
				    rp->p_sendto_e, rp->p_cpu,
				    (int)rp->p_priority,
				    (unsigned)rp->p_cpu_time_left);
			}

			/*
			 * And the queues themselves. A process with rts == 0
			 * is ready; whether the kernel can find it is another
			 * question, and this is the difference between the
			 * two.
			 */
			for (c = 0; c < ncpus; c++) {
				printf("cpu%u: idle=%d cur=%s\n", c,
				    get_cpu_var(c, cpu_is_idle),
				    get_cpu_var(c, proc_ptr) ?
				    get_cpu_var(c, proc_ptr)->p_name : "-");
				for (q = 0; q < NR_SCHED_QUEUES; q++) {
					struct proc *h =
					    get_cpu_var(c, run_q_head)[q];
					if (h != NULL)
						printf("  q%u: %s\n", q,
						    h->p_name);
				}
			}
			printf("--- end ---\n");
		}

		total++;
	}
#endif

#ifdef CONFIG_SMP
	switch (irq) {
	case GIC_IPI_SCHED:
		smp_ipi_sched_handler();
		return;
	case GIC_IPI_HALT:
		smp_ipi_halt_handler();	/* does not return */
		return;
	}
#endif

	/*
	 * The generic dispatcher asserts the number is inside its tables, so a
	 * GIC reporting more lines than NR_IRQ_VECTORS must not reach it -
	 * intr_init() clamped nr_irqs, but a controller can still acknowledge
	 * an INTID above that.
	 */
	if (irq < NR_IRQ_VECTORS)
		irq_handle(irq);
}

/*===========================================================================*
 *				gic_cpu_init				     *
 *===========================================================================*/
/*
 * What a core does for itself. The boot core does it as part of intr_init();
 * a secondary calls this and nothing else, because the distributor is already
 * set up and belongs to the machine rather than to any one core.
 */
void
gic_cpu_init(void)
{
	if (gic.version == 3)
		gicv3_cpu_init();
	else
		gicv2_cpu_init();
}

#ifdef CONFIG_SMP
/*===========================================================================*
 *				gic_send_ipi				     *
 *===========================================================================*/
void
gic_send_ipi(unsigned cpu, int sgi)
{
	if (gic.version == 3)
		gicv3_send_ipi(cpu, sgi);
	else
		gicv2_send_ipi(cpu, sgi);
}
#endif /* CONFIG_SMP */

/*===========================================================================*
 *				bsp_irq_lines				     *
 *===========================================================================*/
int
bsp_irq_lines(void)
{
	return gic.nr_irqs;
}

/*===========================================================================*
 *				bsp_irq_handle				     *
 *===========================================================================*/
void
bsp_irq_handle(void)
{
	if (gic.version == 3)
		gicv3_handle();
	else
		gicv2_handle();
}

/*===========================================================================*
 *				bsp_irq_unmask				     *
 *===========================================================================*/
void
bsp_irq_unmask(int irq)
{
	if (gic.version == 3)
		gicv3_unmask(irq);
	else
		gicv2_unmask(irq);
}

/*===========================================================================*
 *				bsp_irq_mask				     *
 *===========================================================================*/
void
bsp_irq_mask(int irq)
{
	if (gic.version == 3)
		gicv3_mask(irq);
	else
		gicv2_mask(irq);
}
