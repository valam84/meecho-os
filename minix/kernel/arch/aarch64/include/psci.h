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
 * PSCI_FN_CPU_ON is the reason this file is architecture code and not part of
 * the console BSP: bringing the secondary cores up is the same call through
 * the same conduit as turning the machine off.
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
 * The affinity fields of MPIDR_EL1, which are what names a core:
 * Aff0 [7:0], Aff1 [15:8], Aff2 [23:16], Aff3 [39:32]. The bits between and
 * above them are flags - U, MT, and one that reads as one - and are not part
 * of the name.
 */
#define MPIDR_AFF_MASK		0x000000ff00ffffffULL

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

/*
 * Start a secondary core.
 *
 * mpidr names the core the way the device tree and MPIDR_EL1 do; entry is a
 * physical address, because the core arrives with its MMU off; context is
 * handed to it in x0 and is how it learns which core it is. Returns
 * PSCI_SUCCESS or one of the negative PSCI errors - the caller is expected to
 * carry on with the cores that did start.
 */
long psci_cpu_on(u64_t mpidr, phys_bytes entry, unsigned long context);

#endif /* _AARCH64_PSCI_H */
