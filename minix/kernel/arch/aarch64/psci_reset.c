/*
 * Reset and power-off through PSCI.
 *
 * This carried a board's name once - it was bsp/qemu-virt/virt_reset.c - and
 * it never had a board's code in it. Both calls go to firmware, and which
 * instruction reaches that firmware comes out of the device tree, so the
 * same object serves the emulated machine and the CB2: on the board, reboot
 * and poweroff are what stage 8.0 proved, and this is the file that does
 * them.
 *
 * A board without firmware would have more to do. A Compute Module 4 booted
 * without ARM Trusted Firmware has no PSCI, and resetting it means writing
 * the watchdog registers in the power management block - which is why the
 * contract has a bsp_disable_watchdog() at all. Such a board gets its own
 * file here beside this one, chosen the way every other part is chosen, by
 * what the tree says; it does not get a build-time package.
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
	/* Neither machine this kernel runs on leaves a watchdog running. */
}
