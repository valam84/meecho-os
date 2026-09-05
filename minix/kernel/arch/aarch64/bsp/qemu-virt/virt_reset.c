/*
 * Reset and power-off for the QEMU virt machine.
 *
 * Both go through PSCI, which QEMU implements for the guest and advertises in
 * the device tree it hands over. There is no board-specific register to poke
 * here and no watchdog to disable: the virt machine has neither, which is
 * exactly why it is the machine to bring a port up on.
 *
 * The BCM2711 BSP will have more to do. A Compute Module 4 booted without ARM
 * Trusted Firmware has no PSCI, and resetting it means writing the watchdog
 * registers in the power management block - which is why this contract has a
 * bsp_disable_watchdog() at all.
 */

#include "kernel/kernel.h"

#include "arch_proto.h"
#include "psci.h"

#include "bsp_reset.h"

void
bsp_reset_init(void)
{
	/* Nothing to prepare: PSCI needs no setup, only a conduit. */
}

void
bsp_reset(void)
{
	psci_system_reset();
	/* Returns only on failure; the caller says so and hangs. */
}

void
bsp_poweroff(void)
{
	psci_system_off();
}

void
bsp_disable_watchdog(void)
{
	/* The virt machine has no watchdog running at boot. */
}
