/*
 * ARM generic timer.
 *
 * Nothing here is specific to the QEMU virt machine except which interrupt
 * the timer raises, so this file carries over to the CM4 with one constant
 * changed. That is the reason to prefer it over the BCM2711 System Timer as
 * the tick source: the System Timer is a memory-mapped peripheral with
 * board-specific compare channels, two of which belong to the VideoCore
 * firmware, while the generic timer is part of the architecture and is
 * reached through system registers.
 *
 * The counter is 64 bits and free running. The comparator, CNTP_CVAL_EL0,
 * fires when the counter reaches it and stays fired until moved, so each tick
 * has to push it forward. Pushing it by a fixed interval from its previous
 * value rather than from the current count is what keeps the tick from
 * drifting by however long the handler took to run.
 *
 * CNTP_* is the physical timer as seen by EL1. Reaching it from EL1 needs
 * CNTHCTL_EL2.EL1PCEN, which head.S sets on the entry path that comes through
 * EL2 - the path real firmware uses.
 */

#include <stdint.h>

#include "bsp_intr.h"
#include "bsp_timer.h"
#include "intr.h"
#include "kprint.h"

#include "virt_registers.h"

/* CNTP_CTL_EL0 bits. */
#define CNTP_CTL_ENABLE		(1UL << 0)
#define CNTP_CTL_IMASK		(1UL << 1)	/* interrupt masked */
#define CNTP_CTL_ISTATUS	(1UL << 2)	/* condition met, read-only */

static struct virt_timer {
	uint64_t interval;	/* counter ticks between system ticks */
	uint64_t next;		/* value the comparator is set to */
	unsigned hz;		/* counter frequency */
	int running;
} virt_timer;

static inline uint64_t
read_cntfrq(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static inline uint64_t
read_cntpct(void)
{
	uint64_t v;

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
write_cval(uint64_t v)
{
	__asm__ volatile("msr cntp_cval_el0, %0" :: "r"(v));
}

static inline void
write_ctl(uint64_t v)
{
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(v));
	__asm__ volatile("isb" ::: "memory");
}

void
bsp_timer_init(unsigned freq)
{
	uint64_t hz = read_cntfrq();

	/*
	 * CNTFRQ_EL0 is not hardware, it is a note firmware leaves for the
	 * kernel. QEMU fills it in and so does U-Boot, but a board that boots
	 * something else may not, and a zero here would divide by zero and
	 * then tick never or constantly. Say so rather than guess.
	 */
	if (hz == 0) {
		kputs("timer: CNTFRQ_EL0 is zero, firmware did not set it\n");
		return;
	}

	virt_timer.hz = (unsigned)hz;
	virt_timer.interval = hz / freq;

	/* Off while it is programmed. */
	write_ctl(0);

	virt_timer.next = read_cntpct() + virt_timer.interval;
	write_cval(virt_timer.next);
	write_ctl(CNTP_CTL_ENABLE);

	virt_timer.running = 1;
}

void
bsp_timer_stop(void)
{
	write_ctl(0);
	bsp_irq_mask(GIC_PPI_TIMER_NONSEC);
	virt_timer.running = 0;
}

int
bsp_register_timer_handler(const irq_handler_t handler)
{
	irq_register(GIC_PPI_TIMER_NONSEC, handler);

	/* Only let the line through once there is something behind it. */
	bsp_irq_unmask(GIC_PPI_TIMER_NONSEC);

	return 0;
}

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

uint64_t
bsp_timer_counter(void)
{
	return read_cntpct();
}

unsigned
bsp_timer_counter_hz(void)
{
	return virt_timer.hz;
}
