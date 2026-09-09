/*
 * ARM generic timer.
 *
 * The counter is 64 bits and free running. The comparator, CNTP_CVAL_EL0,
 * fires when the counter reaches it and stays fired until moved, so each tick
 * has to push it forward. Pushing it by a fixed interval from its previous
 * value rather than from the current count is what keeps the tick from
 * drifting by however long the handler took to run.
 *
 * CNTP_* is the physical timer as seen by EL1. Reaching it from EL1 needs
 * CNTHCTL_EL2.EL1PCEN, which head.S sets on the entry path through EL2 - the
 * path real firmware uses.
 *
 * Nothing here is specific to a board. Even the interrupt is not: which PPI
 * the timer raises comes out of the device tree, so this file is the same on
 * the emulated machine and on the CB2 - which is why it stopped being
 * bsp/qemu-virt/virt_timer.c and joined the architecture layer.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>

#include "kernel/kernel.h"
#include "kernel/proto.h"
#include "kernel/glo.h"

#include "arch_proto.h"
#include <minix/fdt.h>
#include "gic.h"

#include "bsp_intr.h"
#include "bsp_timer.h"

/* CNTP_CTL_EL0 bits. */
#define CNTP_CTL_ENABLE		(1UL << 0)
#define CNTP_CTL_IMASK		(1UL << 1)	/* interrupt masked */
#define CNTP_CTL_ISTATUS	(1UL << 2)	/* condition met, read-only */

static struct virt_timer {
	u64_t interval;		/* counter ticks between system ticks */
	int intid;		/* which PPI the timer raises */
} virt_timer;

/*
 * Where each core's comparator stands, and whether it is armed at all.
 *
 * Per core and not part of the struct above, because CNTP_CVAL_EL0 and
 * CNTP_CTL_EL0 are per-CPU registers: every core arms its own timer and
 * advances its own deadline. Only the interval and the interrupt number are
 * shared, and those are properties of the machine.
 */
static u64_t timer_next[CONFIG_MAX_CPUS];
static int timer_running[CONFIG_MAX_CPUS];

static inline u64_t
read_cntfrq(void)
{
	u64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static inline u64_t
read_cntpct(void)
{
	u64_t v;

	/*
	 * The counter read has to be ordered against what came before it, or
	 * it can be speculated backwards and report a time before the event
	 * being timed.
	 */
	__asm__ volatile("isb" ::: "memory");
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static inline void
write_cval(u64_t v)
{
	__asm__ volatile("msr cntp_cval_el0, %0" :: "r"(v));
}

static inline void
write_ctl(u64_t v)
{
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(v));
	__asm__ volatile("isb" ::: "memory");
}

/*===========================================================================*
 *				find_timer_intid			     *
 *===========================================================================*/
/*
 * Which interrupt the non-secure physical timer raises, from the device tree.
 *
 * The armv8-timer binding lists four interrupts in a fixed order: secure
 * physical, non-secure physical, virtual, hypervisor. EL1 uses the second.
 * Each is a triplet <type number flags>, where type 1 means PPI and the INTID
 * is the number plus 16.
 *
 * QEMU's virt and a Raspberry Pi both happen to say PPI 14 here, so a
 * constant would work on the two boards in front of us. It is read anyway,
 * because the triplet costs a dozen lines and a wrong hardcoded interrupt is
 * a machine that boots and then never schedules - a symptom that points
 * nowhere near its cause.
 */
struct timer_scan {
	int intid;
	int found;
};

#define compatible_with(node, want)	fdt_node_is_compatible(node, want)

#define IRQ_CELLS	3		/* <type number flags> */
#define IRQ_TYPE_PPI	1
#define PPI_TO_INTID(n)	((n) + GIC_PPI_BASE)

static int
find_timer(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct timer_scan *s = cookie;
	const char *irqs;
	unsigned len;
	u64_t type, number;

	if (depth != 1 || !compatible_with(node, "arm,armv8-timer"))
		return 0;

	if ((irqs = fdt_getprop(node, "interrupts", &len)) == NULL)
		return 0;

	/* The second triplet, so at least two of them have to be there. */
	if (len < 2 * IRQ_CELLS * 4)
		return 0;

	irqs += IRQ_CELLS * 4;
	type = fdt_read_cells(irqs, 1);
	number = fdt_read_cells(irqs + 4, 1);

	if (type != IRQ_TYPE_PPI)
		return 0;

	s->intid = (int)PPI_TO_INTID(number);
	s->found = 1;

	return 1;
}

/*===========================================================================*
 *				arm_this_cpu				     *
 *===========================================================================*/
/*
 * Set this core's comparator one interval ahead and let the line through.
 *
 * All of it is per core. The comparator is a system register, and the timer
 * is a PPI, so its enable bit belongs to this core too - on GICv3 it is not
 * even in the same block of registers as a shared interrupt, and no other
 * core can set it.
 */
static void
arm_this_cpu(void)
{
	/* Off while it is programmed. */
	write_ctl(0);

	timer_next[cpuid] = read_cntpct() + virt_timer.interval;
	write_cval(timer_next[cpuid]);
	write_ctl(CNTP_CTL_ENABLE);

	bsp_irq_unmask(virt_timer.intid);

	timer_running[cpuid] = 1;
}

/*===========================================================================*
 *				bsp_timer_init				     *
 *===========================================================================*/
void
bsp_timer_init(unsigned freq)
{
	struct timer_scan scan;
	const void *dtb;
	u64_t hz = read_cntfrq();

	/*
	 * CNTFRQ_EL0 is not hardware, it is a note the firmware leaves. A
	 * zero here would divide by zero and then tick either never or
	 * constantly. cycles_accounting_init() has already refused to
	 * continue in that case; this is the second reader of the same
	 * register and says so in its own right.
	 */
	if (hz == 0)
		panic("CNTFRQ_EL0 is zero: firmware did not set the timer "
		    "frequency");

	/*
	 * Which interrupt and how long a tick is are the same on every core,
	 * so they are worked out once. Everything below this is per core, and
	 * every core runs it: a secondary calls in here through
	 * app_cpu_init_timer() to arm its own comparator and let its own copy
	 * of the line through.
	 */
	if (virt_timer.intid == 0) {
		memset(&scan, 0, sizeof(scan));
		dtb = (const void *)phys2vir(boot_dtb);

		if (!fdt_valid(dtb) || fdt_walk(dtb, find_timer, &scan) == 0 ||
		    !scan.found)
			panic("device tree does not say which interrupt the "
			    "timer raises");

		virt_timer.intid = scan.intid;
		virt_timer.interval = hz / freq;
	}

	arm_this_cpu();
}

/*===========================================================================*
 *				bsp_timer_restart			     *
 *===========================================================================*/
void
bsp_timer_restart(void)
{
	/*
	 * Called on every return to user, so the common case is a core whose
	 * timer never stopped and there is nothing to do. The case that
	 * matters is a secondary coming back from idle, where stop_local_timer()
	 * really did switch it off.
	 */
	if (timer_running[cpuid])
		return;

	arm_this_cpu();
}

/*===========================================================================*
 *				bsp_timer_stop				     *
 *===========================================================================*/
void
bsp_timer_stop(void)
{
	write_ctl(0);
	bsp_irq_mask(virt_timer.intid);
	timer_running[cpuid] = 0;
}

/*===========================================================================*
 *			    bsp_register_timer_handler			     *
 *===========================================================================*/
int
bsp_register_timer_handler(irq_handler_t handler)
{
	static irq_hook_t timer_hook;

	assert(virt_timer.intid != 0);

	/*
	 * Through the generic hook table, which is what bsp_irq_handle() ends
	 * up walking. The hook is a static because it has to outlive this
	 * call and there is nothing to allocate from yet - the same reason
	 * the kern_phys_map entries are statics in their drivers.
	 */
	put_irq_handler(&timer_hook, virt_timer.intid, handler);

	return 0;
}

/*===========================================================================*
 *				bsp_timer_int_handler			     *
 *===========================================================================*/
void
bsp_timer_int_handler(void)
{
	/*
	 * Moving the comparator is what clears the condition; there is no
	 * separate acknowledge register. Advancing from the previous value
	 * keeps the tick period exact even when a tick is serviced late.
	 *
	 * The deadline is this core's: the interrupt is a PPI, so whichever
	 * core is running this is the core whose timer fired.
	 */
	timer_next[cpuid] += virt_timer.interval;
	write_cval(timer_next[cpuid]);
}
