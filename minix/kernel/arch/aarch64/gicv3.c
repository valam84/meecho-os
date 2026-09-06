/*
 * GICv3: a distributor, a redistributor per core, and a CPU interface that
 * is not in memory at all.
 *
 * Three things here have no counterpart in the GICv2 driver next door, and
 * each of them is a way for the machine to go quiet rather than to complain:
 *
 *   The CPU interface is the ICC_* system registers, and using them at EL1
 *   requires ICC_SRE_EL1.SRE. On a machine entered at EL2 that in turn
 *   requires ICC_SRE_EL2.Enable, which head.S sets on the way down - without
 *   it the first mrs here takes an undefined-instruction trap.
 *
 *   SGIs and PPIs belong to this core's redistributor, not to the
 *   distributor. Enabling the timer is therefore a write to a register block
 *   that has to be found first, by matching GICR_TYPER's affinity against
 *   MPIDR_EL1. Writing it to the distributor instead is accepted and does
 *   nothing.
 *
 *   Affinity routing has to be switched on in GICD_CTLR before GICD_IROUTER
 *   exists to be written; with routing off, the same offsets are a different
 *   register. So the order below is: configure, enable the distributor, and
 *   only then say where the SPIs should go.
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
 * How long to wait for the controller to accept a change before deciding it
 * never will. The architecture gives no bound; this one is far past any real
 * one and exists so that a wedged GIC says so instead of hanging the machine
 * in a loop with no output.
 */
#define GIC_WAIT_LIMIT		1000000

/*===========================================================================*
 *				gicd_wait_rwp				     *
 *===========================================================================*/
/*
 * A disable or a clear is not in force everywhere until RWP goes low. It
 * matters for masking: the generic dispatcher masks a line, runs the hook
 * chain and unmasks it, and a mask that has not taken effect yet is not a
 * mask.
 */
static void
gicd_wait_rwp(void)
{
	int spins = GIC_WAIT_LIMIT;

	while (mmio_read(gic.dist_base + GICD_CTLR) & GICD_CTLR_RWP)
		if (--spins == 0)
			panic("GICv3 distributor never finished a write");
}

/*===========================================================================*
 *				gicr_wait_rwp				     *
 *===========================================================================*/
static void
gicr_wait_rwp(void)
{
	int spins = GIC_WAIT_LIMIT;

	while (mmio_read(gic.rd_base + GICR_CTLR) & GICR_CTLR_RWP)
		if (--spins == 0)
			panic("GICv3 redistributor never finished a write");
}

/*===========================================================================*
 *				my_affinity				     *
 *===========================================================================*/
/*
 * This core's affinity, packed the way the GIC packs it: Aff3 in the top
 * byte, then Aff2, Aff1, Aff0. MPIDR_EL1 holds the same four fields but with
 * Aff3 up at bit 32 and flag bits in between, so they have to be gathered.
 *
 * The same value is compared against GICR_TYPER's high word to find this
 * core's redistributor, and written into GICD_IROUTER to send the shared
 * interrupts here.
 */
static u32_t
my_affinity(void)
{
	u64_t mpidr;

	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

	return (u32_t)(((mpidr >> 32) & 0xff) << 24) |
	    (u32_t)(((mpidr >> 16) & 0xff) << 16) |
	    (u32_t)(((mpidr >> 8) & 0xff) << 8) |
	    (u32_t)(mpidr & 0xff);
}

/*===========================================================================*
 *				find_redistributor			     *
 *===========================================================================*/
/*
 * Which of the redistributors in the region belongs to this core.
 *
 * With one core the answer is the first one and this is three lines of
 * nothing, but the walk is written properly now because the alternative is
 * to write it during SMP bring-up, on a second core, with no console of its
 * own. The frames are laid out back to back and the last one says so.
 */
static void
find_redistributor(void)
{
	u32_t aff = my_affinity();
	vir_bytes base = gic.redist_base;
	vir_bytes end = base + gic.redist_size;
	u32_t typer_lo;
	vir_bytes stride;

	for (;;) {
		if (base + GICR_FRAME_SIZE > end)
			panic("GICv3: no redistributor for affinity %08x in "
			    "the region the device tree describes", aff);

		typer_lo = mmio_read(base + GICR_TYPER);

		if (mmio_read(base + GICR_TYPER + GICR_TYPER_AFFINITY) ==
		    aff) {
			gic.rd_base = base;
			gic.sgi_base = base + GICR_SGI_OFFSET;
			return;
		}

		if (typer_lo & GICR_TYPER_LAST)
			panic("GICv3: no redistributor for affinity %08x", aff);

		/*
		 * Two 64 KiB frames each, or four when the redistributor also
		 * carries the virtual LPI frames - unless the tree overrides
		 * the stride outright, which some silicon needs.
		 */
		stride = gic.redist_stride;
		if (stride == 0)
			stride = (typer_lo & GICR_TYPER_VLPIS) ?
			    2 * GICR_FRAME_SIZE : GICR_FRAME_SIZE;

		base += stride;
	}
}

/*===========================================================================*
 *				wake_redistributor			     *
 *===========================================================================*/
/*
 * A redistributor comes out of reset asleep and delivers nothing until it is
 * woken. Clearing ProcessorSleep asks; ChildrenAsleep going low is the
 * answer.
 */
static void
wake_redistributor(void)
{
	int spins = GIC_WAIT_LIMIT;
	u32_t waker;

	waker = mmio_read(gic.rd_base + GICR_WAKER);
	mmio_write(gic.rd_base + GICR_WAKER, waker & ~GICR_WAKER_SLEEP);

	while (mmio_read(gic.rd_base + GICR_WAKER) & GICR_WAKER_ASLEEP)
		if (--spins == 0)
			panic("GICv3 redistributor stayed asleep");
}

/*===========================================================================*
 *				dist_init				     *
 *===========================================================================*/
static void
dist_init(void)
{
	u32_t aff = my_affinity();
	int i;

	/* Take the distributor down while it is being reprogrammed. */
	mmio_write(gic.dist_base + GICD_CTLR, 0);
	gicd_wait_rwp();

	/*
	 * The shared interrupts only. The first 32 INTIDs have registers at
	 * these offsets too, but on GICv3 they are the redistributor's copies
	 * that matter, and writes here are ignored - see cpu_init().
	 *
	 * Group 1 Non-secure, because that is the group this kernel enables
	 * on the CPU interface. A line left in group 0 is a secure interrupt
	 * and would never be delivered to us.
	 */
	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i += 32) {
		mmio_write(gic.dist_base + GICD_IGROUPR(i / 32), 0xffffffff);
		mmio_write(gic.dist_base + GICD_ICENABLER(i / 32), 0xffffffff);
		mmio_write(gic.dist_base + GICD_ICPENDR(i / 32), 0xffffffff);
	}

	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i += 4)
		mmio_write(gic.dist_base + GICD_IPRIORITYR(i / 4),
		    0xa0a0a0a0);

	/* Level-triggered, active high: two bits each, 0b00 is level. */
	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i += 16)
		mmio_write(gic.dist_base + GICD_ICFGR(i / 16), 0);

	gicd_wait_rwp();

	/*
	 * Affinity routing on, both Non-secure groups enabled. This has to
	 * happen before the loop below: GICD_IROUTER only exists while
	 * affinity routing is on, and with it off the same offsets are other
	 * registers entirely.
	 */
	mmio_write(gic.dist_base + GICD_CTLR,
	    GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A | GICD_CTLR_ENABLE_G1);
	gicd_wait_rwp();

	/*
	 * Send every shared interrupt to this core. GICD_IROUTER is 64 bits
	 * wide and the architecture allows a 32-bit access to each half; the
	 * halves are written low first, and the intermediate value cannot
	 * route anything anywhere because every one of these lines is still
	 * disabled.
	 */
	for (i = GIC_SPI_BASE; i < gic.nr_irqs; i++) {
		mmio_write(gic.dist_base + GICD_IROUTER(i), aff & 0x00ffffff);
		mmio_write(gic.dist_base + GICD_IROUTER(i) + 4, aff >> 24);
	}
}

/*===========================================================================*
 *				cpu_init				     *
 *===========================================================================*/
static void
cpu_init(void)
{
	u64_t val;
	int i;

	/*
	 * SGIs and PPIs, in this core's redistributor. Same registers as the
	 * distributor has for the shared interrupts, and the reason the timer
	 * needs this file at all: it arrives as a PPI, so nothing the
	 * distributor is told about it has any effect.
	 */
	mmio_write(gic.sgi_base + GICR_IGROUPR0, 0xffffffff);
	mmio_write(gic.sgi_base + GICR_ICENABLER0, 0xffffffff);
	mmio_write(gic.sgi_base + GICR_ICPENDR0, 0xffffffff);

	for (i = 0; i < GIC_SPI_BASE; i += 4)
		mmio_write(gic.sgi_base + GICR_IPRIORITYR(i / 4), 0xa0a0a0a0);

	gicr_wait_rwp();

	/*
	 * From here on the CPU interface, which is not in memory. SRE first,
	 * because without it the rest of these registers are not accessible.
	 *
	 * On a machine with no legacy support SRE reads as one and the write
	 * is ignored, which is the normal case and not an error. A machine
	 * where it will not stick is one that only offers the memory-mapped
	 * interface of GICv2, and there is nothing to fall back to: this
	 * driver was chosen because the device tree said gic-v3.
	 */
	__asm__ volatile("mrs %0, icc_sre_el1" : "=r"(val));
	if (!(val & ICC_SRE_EL1_SRE)) {
		val |= ICC_SRE_EL1_SRE;
		__asm__ volatile("msr icc_sre_el1, %0" :: "r"(val));
		__asm__ volatile("isb");
		__asm__ volatile("mrs %0, icc_sre_el1" : "=r"(val));
	}
	if (!(val & ICC_SRE_EL1_SRE))
		panic("GICv3: ICC_SRE_EL1.SRE will not set, so this CPU "
		    "interface cannot be reached from EL1");

	/* Pass everything below the priority every line was given. */
	val = GIC_PRIORITY_MASK;
	__asm__ volatile("msr icc_pmr_el1, %0" :: "r"(val));

	/* No preemption grouping. */
	val = 0;
	__asm__ volatile("msr icc_bpr1_el1, %0" :: "r"(val));

	/*
	 * EOImode 0: one write to ICC_EOIR1_EL1 both drops the running
	 * priority and deactivates the interrupt. The split mode exists for
	 * hypervisors handing an interrupt to a guest, which is not this.
	 */
	__asm__ volatile("mrs %0, icc_ctlr_el1" : "=r"(val));
	val &= ~(u64_t)ICC_CTLR_EL1_EOIMODE;
	__asm__ volatile("msr icc_ctlr_el1, %0" :: "r"(val));

	/* Group 1 Non-secure on, which is the group everything was put in. */
	val = 1;
	__asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(val));
	__asm__ volatile("isb");
}

/*===========================================================================*
 *				gicv3_init				     *
 *===========================================================================*/
void
gicv3_init(void)
{
	assert(gic.redist_base != 0);
	assert(gic.redist_size >= GICR_FRAME_SIZE);

	find_redistributor();
	wake_redistributor();
	dist_init();
	cpu_init();
}

/*===========================================================================*
 *				gicv3_handle				     *
 *===========================================================================*/
void
gicv3_handle(void)
{
	u64_t iar;
	int irq;

	__asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(iar));

	/*
	 * The acknowledge has to be complete before anything the handler does
	 * can be reordered ahead of it - including the writes that quiet the
	 * device.
	 */
	__asm__ volatile("dsb sy" ::: "memory");

	irq = (int)(iar & GICV3_INTID_MASK);

	/*
	 * 1020..1023 are not interrupts: 1023 is what an acknowledge reads
	 * when the source went away between asserting and being read. None of
	 * them is retired.
	 */
	if (irq >= GIC_FIRST_SPECIAL_INTID)
		return;

	/*
	 * The generic dispatcher asserts the number is inside its tables, so
	 * a GIC reporting more lines than NR_IRQ_VECTORS must not reach it -
	 * intr_init() clamped nr_irqs, but a controller can still report an
	 * INTID above that.
	 */
	if (irq < NR_IRQ_VECTORS)
		irq_handle(irq);

	__asm__ volatile("msr icc_eoir1_el1, %0" :: "r"(iar));
	__asm__ volatile("isb");
}

/*===========================================================================*
 *				gicv3_unmask				     *
 *===========================================================================*/
/*
 * An SGI or a PPI is this core's, and lives in its redistributor; a shared
 * interrupt lives in the distributor. Getting this wrong is silent - the
 * write lands in a register that is there but ignored.
 *
 * No wait for RWP: it tracks disables and clears becoming visible, and an
 * enable arriving late costs nothing.
 */
void
gicv3_unmask(int irq)
{
	if (irq < GIC_SPI_BASE)
		mmio_write(gic.sgi_base + GICR_ISENABLER0, 1U << irq);
	else
		mmio_write(gic.dist_base + GICD_ISENABLER(irq / 32),
		    1U << (irq & 0x1f));
}

/*===========================================================================*
 *				gicv3_mask				     *
 *===========================================================================*/
void
gicv3_mask(int irq)
{
	if (irq < GIC_SPI_BASE) {
		mmio_write(gic.sgi_base + GICR_ICENABLER0, 1U << irq);
		gicr_wait_rwp();
	} else {
		mmio_write(gic.dist_base + GICD_ICENABLER(irq / 32),
		    1U << (irq & 0x1f));
		gicd_wait_rwp();
	}
}
