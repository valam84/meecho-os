#ifndef _BSP_RESET_H_
#define _BSP_RESET_H_

/*
 * Turning the machine off and turning it back on, as the board provides it.
 * The same four calls ARM has, so that the contract does not change shape
 * between the two architectures.
 *
 * On AArch64 boards answer all of this through PSCI, which is architecture
 * code rather than board code (see include/psci.h), and which instruction
 * reaches the firmware comes out of the device tree - so the implementation
 * is one file for every board, psci_reset.c, and not a board package.
 *
 * The contract keeps bsp_disable_watchdog() because a board without firmware
 * is a real case: a Compute Module 4 booted without ARM Trusted Firmware has
 * no PSCI at all, and resetting it means writing the watchdog registers
 * itself. That board gets a second file beside psci_reset.c, picked by what
 * the tree says, the way every other part here is picked.
 */
void bsp_reset_init(void);
void bsp_reset(void);
void bsp_poweroff(void);
void bsp_disable_watchdog(void);

#endif /* _BSP_RESET_H_ */
