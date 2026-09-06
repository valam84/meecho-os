#ifndef _AARCH64_SMP_H_
#define _AARCH64_SMP_H_

/*
 * What the generic SMP code needs from this architecture.
 *
 * The one interesting entry is cpuid. i386 keeps the number at the top of the
 * kernel stack and finds it by rounding the stack pointer down, which this
 * port cannot copy: the stack an exception from EL0 arrives on is the
 * interrupted process itself - restore_user_context() leaves SP_EL1 at the
 * end of p_reg - so there is no per-CPU stack to round to.
 *
 * AArch64 has a register for exactly this. TPIDR_EL1 is software's own
 * per-CPU word, readable only at EL1 and above, and every kernel on this
 * architecture uses it for the same purpose. It is written once, by the CPU
 * itself, as the last step of coming up.
 */

#ifndef __ASSEMBLY__

#include <minix/type.h>

/* For barrier(), which the generic SMP code uses and this port already has. */
#include "cpufunc.h"

static inline unsigned
arch_cpuid(void)
{
	u64_t id;

	__asm__ volatile("mrs %0, tpidr_el1" : "=r"(id));

	return (unsigned)id;
}

#define cpuid		arch_cpuid()

/*
 * The way back to a single-processor system when the boot parameters ask for
 * one. Nothing has been started at this point, so there is nothing to undo -
 * unlike i386, which has to fall back to the legacy interrupt controller
 * because it has already decided against it.
 */
#define smp_single_cpu_fallback() do {		\
	bsp_cpu_id = 0;				\
	ncpus = 1;				\
	bsp_finish_booting();			\
} while (0)

/* MPIDR_EL1 affinity of each CPU, from the device tree; index is the cpuid. */
extern u64_t cpu_mpidr[CONFIG_MAX_CPUS];

/* Called from head.S on a secondary, with the MMU off and a stack of its own. */
void smp_ap_start(unsigned cpu);

/* And on the same core once it is running in the upper half. */
void smp_ap_boot(unsigned cpu);

#endif /* __ASSEMBLY__ */

#endif /* _AARCH64_SMP_H_ */
