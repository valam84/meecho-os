/*
 * System Timer driver for BCM2711.
 *
 * The BSP timer contract (see bsp/ti/omap_timer.c) has two halves:
 *
 *  1. A periodic tick at system_hz, delivered as an interrupt.
 *  2. A free-running counter behind read_tsc_64(), whose physical address is
 *     also mapped into user space so that libc can read time without a system
 *     call. That mapping is published through arm_frclock.
 *
 * The BCM2711 System Timer fits this better than the OMAP hardware does. It
 * is a genuine 64-bit counter ticking at a fixed 1 MHz, so there is no
 * prescaler to program and no software overflow accounting: the OMAP driver
 * carries a high_frc counter and a frc_overflow_check() precisely because its
 * timer is only 32 bits wide. We can drop all of that.
 *
 * The catch is the compare side. The System Timer has four compare registers
 * that match against the LOW 32 bits of the counter and fire once. There is
 * no auto-reload, so the handler must re-arm on every tick.
 *
 * Channels 0 and 2 belong to the VideoCore firmware. Touching them breaks the
 * GPU and, on some firmware revisions, hangs the board. We use channel 3.
 */

#include "kernel/kernel.h"
#include "kernel/clock.h"
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/board.h>
#include <minix/mmio.h>
#include <assert.h>
#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#include "arch_proto.h"
#include "bsp_timer.h"
#include "bsp_intr.h"

#include "bcm2711_registers.h"

/* Interrupt handler hook, as required by put_irq_handler. */
static irq_hook_t bcm2711_timer_hook;

static struct bcm2711_timer
{
	vir_bytes base;
	vir_bytes size;
	u32_t interval;		/* counter ticks between system ticks */
	int running;
} bcm2711_timer = {
	.base = 0,
	.running = 0,
};

/* Kernel mapping of the timer registers. */
static kern_phys_map timer_phys_map;

/* Second mapping of the same page, this one visible to user space. */
static kern_phys_map timer_user_phys_map;

/*
 * Callback from VM once the user-visible mapping exists. All we have to do is
 * record where user space can find the counter, and how fast it runs.
 */
int
kern_phys_fr_user_mapped(vir_bytes id, phys_bytes address)
{
	arm_frclock.tcrr = address + SYSTIMER_CLO;
	arm_frclock.hz = BCM2711_SYSTIMER_HZ;
	return 0;
}

/*
 * Read the full 64-bit counter.
 *
 * CHI and CLO cannot be read atomically as a pair, so the low half may wrap
 * between the two reads. Read the high half again and retry if it changed.
 * At 1 MHz the low half wraps about every 71 minutes, so this loop runs more
 * than once only very rarely.
 */
static u64_t
bcm2711_read_frc(void)
{
	u32_t hi, lo, hi2;

	if (bcm2711_timer.base == 0)
		return 0;

	do {
		hi = mmio_read(bcm2711_timer.base + SYSTIMER_CHI);
		lo = mmio_read(bcm2711_timer.base + SYSTIMER_CLO);
		hi2 = mmio_read(bcm2711_timer.base + SYSTIMER_CHI);
	} while (hi != hi2);

	return ((u64_t) hi << 32) | lo;
}

/* Point the compare register one interval past the current counter value. */
static void
bcm2711_timer_rearm(void)
{
	u32_t now;

	now = mmio_read(bcm2711_timer.base + SYSTIMER_CLO);
	mmio_write(bcm2711_timer.base + SYSTIMER_C3,
	    now + bcm2711_timer.interval);
}

int
bsp_register_timer_handler(const irq_handler_t handler)
{
	bcm2711_timer_hook.proc_nr_e = NONE;
	bcm2711_timer_hook.irq = BCM2711_TICK_IRQ;

	put_irq_handler(&bcm2711_timer_hook, BCM2711_TICK_IRQ, handler);

	/* Only unmask once a handler is actually installed. */
	bsp_irq_unmask(BCM2711_TICK_IRQ);

	return 0;
}

void
bsp_timer_init(unsigned freq)
{
	if (!BOARD_IS_RPI4(machine.board_id)) {
		panic("Can not do the timer setup. "
		    "machine (0x%08x) is unknown\n", machine.board_id);
	}

	bcm2711_timer.base = BCM2711_SYSTIMER_BASE;
	bcm2711_timer.size = BCM2711_SYSTIMER_SIZE;

	kern_phys_map_ptr(bcm2711_timer.base, bcm2711_timer.size,
	    VMMF_UNCACHED | VMMF_WRITE, &timer_phys_map,
	    (vir_bytes) & bcm2711_timer.base);

	/*
	 * Ask for the same page a second time, this time user-readable, and
	 * get told the address through the callback. This is what makes the
	 * counter available to libc without a trap.
	 */
	kern_req_phys_map(BCM2711_SYSTIMER_BASE, ARM_PAGE_SIZE,
	    VMMF_UNCACHED | VMMF_USER, &timer_user_phys_map,
	    kern_phys_fr_user_mapped, 0);

	assert(freq != 0);
	bcm2711_timer.interval = BCM2711_SYSTIMER_HZ / freq;

	/* Acknowledge anything left over from the firmware or bootloader. */
	mmio_write(bcm2711_timer.base + SYSTIMER_CS,
	    SYSTIMER_CS_M(BCM2711_TICK_CHANNEL));

	bcm2711_timer_rearm();
	bcm2711_timer.running = 1;
}

void
bsp_timer_stop(void)
{
	/*
	 * There is no per-channel enable bit. Stop re-arming and mask the
	 * line at the GIC so a match already in flight is not delivered.
	 */
	bcm2711_timer.running = 0;
	bsp_irq_mask(BCM2711_TICK_IRQ);
}

void
bsp_timer_int_handler(void)
{
	/* Acknowledge the match, then set up the next one. */
	mmio_write(bcm2711_timer.base + SYSTIMER_CS,
	    SYSTIMER_CS_M(BCM2711_TICK_CHANNEL));

	if (bcm2711_timer.running)
		bcm2711_timer_rearm();
}

/* Use the free running counter as the TSC. */
void
read_tsc_64(u64_t * t)
{
	*t = bcm2711_read_frc();
}
