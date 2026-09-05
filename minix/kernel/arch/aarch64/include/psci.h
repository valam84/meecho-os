#ifndef _AARCH64_PSCI_H
#define _AARCH64_PSCI_H

/*
 * The Power State Coordination Interface: how an AArch64 kernel asks the
 * firmware below it to turn the machine off, reset it, or start another core.
 *
 * This is the architecture's answer to a job that is board-specific
 * everywhere else. On ARMv7 each board resets itself its own way, which is
 * why the BSP contract has bsp_reset() at all; here there is one interface,
 * specified by ARM, and both QEMU's virt machine and any board with ARM
 * Trusted Firmware implement it.
 *
 * Two things come out of the device tree rather than out of a header: whether
 * PSCI is there at all, and which instruction reaches it - hvc when the
 * implementation sits in EL2, smc when it sits in EL3. QEMU's virt says
 * "hvc"; a board with ATF underneath U-Boot says "smc". Guessing would work
 * on one of them and hang on the other, and the tree is already being read.
 *
 * PSCI_FN_CPU_ON is unused so far and is here because it is the reason this
 * file is architecture code and not part of the console BSP: bringing up the
 * secondary cores at stage 6 is the same call through the same conduit.
 */

/*
 * Function identifiers, PSCI 0.2 and later. The 0xc4.. ones take 64-bit
 * arguments; the 0x84.. ones take none that matter here. Version 0.1 named
 * these in the device tree node instead, and is not supported: every AArch64
 * platform worth booting reports arm,psci-0.2 or later.
 */
#define PSCI_FN_CPU_ON		0xc4000003UL
#define PSCI_FN_SYSTEM_OFF	0x84000008UL
#define PSCI_FN_SYSTEM_RESET	0x84000009UL

/* Return values. */
#define PSCI_SUCCESS		0
#define PSCI_NOT_SUPPORTED	(-1)

/*
 * Whether the machine offers PSCI. Reads the device tree the first time it is
 * asked and remembers the answer, so it is safe to call from a panic path
 * that runs before anything has been initialised.
 */
int psci_available(void);

/* Make a call. Returns PSCI_NOT_SUPPORTED if there is no conduit. */
long psci_call(unsigned long fn, unsigned long a1, unsigned long a2,
	unsigned long a3);

/* The two that do not return when they work. */
void psci_system_off(void);
void psci_system_reset(void);

#endif /* _AARCH64_PSCI_H */
