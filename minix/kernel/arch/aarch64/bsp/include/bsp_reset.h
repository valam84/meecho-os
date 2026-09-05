#ifndef _BSP_RESET_H_
#define _BSP_RESET_H_

/*
 * Turning the machine off and turning it back on, as the board provides it.
 * The same four calls ARM has, so that the contract does not change shape
 * between the two architectures.
 *
 * On AArch64 most boards answer all of this through PSCI, which is
 * architecture code rather than board code (see include/psci.h) - so a BSP
 * here is usually a few lines delegating to it, and only a board without
 * firmware needs to poke a watchdog itself. The layer stays because the
 * board without firmware is a real case: a Compute Module 4 booted without
 * ARM Trusted Firmware has no PSCI at all.
 */
void bsp_reset_init(void);
void bsp_reset(void);
void bsp_poweroff(void);
void bsp_disable_watchdog(void);

#endif /* _BSP_RESET_H_ */
