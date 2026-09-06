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
 * the emulated machine and on the Compute Module.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>

#include "kernel/kernel.h"
#include "kernel/proto.h"
#include "kernel/glo.h"

#include "arch_proto.h"
#include "fdt.h"
#include "gic.h"

#include "bsp_intr.h"
#include "bsp_timer.h"
#include "virt_registers.h"

/* CNTP_CTL_EL0 bits. */
#define CNTP_CTL_ENABLE		(1UL << 0)
#define CNTP_CTL_IMASK		(1UL << 1)	/* interrupt masked */
#define CNTP_CTL_ISTATUS	(1UL << 2)	/* condition met, read-only */

static struct virt_timer {
	u64_t interval;		/* counter ticks between system ticks */
	u64_t next;		/* value the comparator is set to */
	int intid;		/* which PPI the timer raises */
	int running;
} virt_timer;

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

	memset(&scan, 0, sizeof(scan));
	dtb = (const void *)phys2vir(boot_dtb);

	if (!fdt_valid(dtb) || fdt_walk(dtb, find_timer, &scan) == 0 ||
	    !scan.found)
		panic("device tree does not say which interrupt the timer "
		    "raises");

	virt_timer.intid = scan.intid;
	virt_timer.interval = hz / freq;

	/* Off while it is programmed. */
	write_ctl(0);

	virt_timer.next = read_cntpct() + virt_timer.interval;
	write_cval(virt_timer.next);
	write_ctl(CNTP_CTL_ENABLE);

	virt_timer.running = 1;
}

/*===========================================================================*
 *				bsp_timer_stop				     *
 *===========================================================================*/
void
bsp_timer_stop(void)
{
	write_ctl(0);
	bsp_irq_mask(virt_timer.intid);
	virt_timer.running = 0;
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

	/* Only let the line through once there is something behind it. */
	bsp_irq_unmask(virt_timer.intid);

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
	 */
	virt_timer.next += virt_timer.interval;
	write_cval(virt_timer.next);
}
