/*
 * Reset, poweroff and watchdog control for BCM2711.
 *
 * Everything here goes through the power management block. The Pi has no
 * dedicated reset line reachable from the ARM side: you arm the watchdog with
 * a short timeout and let it fire.
 *
 * Every write to a PM register must carry PM_PASSWORD in the top 8 bits or
 * the hardware silently discards it. This is the single most common mistake
 * in Pi reset code, and it fails quietly.
 *
 * Like bsp/ti/omap_reset.c this file is compiled into the unpaged part of the
 * kernel as well, so that a panic before the MMU is up can still reboot.
 */

#include <assert.h>
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
#include "bsp_reset.h"

#include "bcm2711_registers.h"

struct bcm2711_reset
{
	vir_bytes base;
	vir_bytes size;
};

static struct bcm2711_reset bcm2711_reset = {
	.base = 0,
};

static kern_phys_map reset_phys_map;

void
bsp_reset_init(void)
{
	bcm2711_reset.base = BCM2711_PM_BASE;
	bcm2711_reset.size = BCM2711_PM_SIZE;

	kern_phys_map_ptr(bcm2711_reset.base, bcm2711_reset.size,
	    VMMF_UNCACHED | VMMF_WRITE, &reset_phys_map,
	    (vir_bytes) & bcm2711_reset.base);
}

/*
 * Arm the watchdog for the given number of ticks and configure the reset
 * controller to perform a full reset when it expires. The watchdog runs at
 * roughly 65536 Hz, so a handful of ticks is a fraction of a millisecond.
 */
static void
bcm2711_trigger_watchdog(u32_t ticks)
{
	u32_t rstc;

	mmio_write(bcm2711_reset.base + PM_WDOG,
	    PM_PASSWORD | (ticks & PM_WDOG_MASK));

	rstc = mmio_read(bcm2711_reset.base + PM_RSTC);
	rstc &= ~PM_RSTC_WRCFG_MASK;
	rstc |= PM_RSTC_WRCFG_FULL_RESET;

	mmio_write(bcm2711_reset.base + PM_RSTC, PM_PASSWORD | rstc);
}

void
bsp_reset(void)
{
	assert(bcm2711_reset.base);

	bcm2711_trigger_watchdog(10);

	/* The watchdog fires in well under a millisecond. */
	for (;;)
		;
}

void
bsp_poweroff(void)
{
	u32_t rsts;

	assert(bcm2711_reset.base);

	/*
	 * The Pi cannot cut its own power. What it can do is tell the
	 * VideoCore firmware not to come back: the firmware reads a boot
	 * partition number out of PM_RSTS after a reset, and partition 63
	 * means halt instead of boot.
	 *
	 * The partition number is stored in interleaved bits, which is why
	 * 63 is written as 0x555 rather than 63.
	 */
	rsts = mmio_read(bcm2711_reset.base + PM_RSTS);
	rsts &= ~PM_RSTS_PARTITION_MASK;
	rsts |= PM_RSTS_HALT_PARTITION;

	mmio_write(bcm2711_reset.base + PM_RSTS, PM_PASSWORD | rsts);

	bcm2711_trigger_watchdog(10);

	for (;;)
		;
}

void
bsp_disable_watchdog(void)
{
	assert(bcm2711_reset.base);

	/*
	 * Return the reset controller to its idle configuration. U-Boot may
	 * have left a watchdog running; if we do not clear it the board
	 * reboots partway through kernel initialisation, which looks
	 * exactly like a hang in whatever code happened to be executing.
	 */
	mmio_write(bcm2711_reset.base + PM_RSTC,
	    PM_PASSWORD | PM_RSTC_RESET);
}
