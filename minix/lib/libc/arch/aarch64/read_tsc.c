#include <sys/types.h>
#include <minix/minlib.h>

void
read_tsc(u32_t *hi, u32_t *lo)
{
/* Read the counter of the ARM generic timer. Intel calls its counterpart the
 * Time Stamp Counter (TSC); earm reads the PMU cycle counter.
 *
 * On AArch64 the cycle counter (PMCCNTR_EL0) is the wrong choice for what
 * read_tsc is used for: it counts at the core's own clock, stops when the
 * core sleeps and is not synchronised between cores, so two CPUs would give
 * two unrelated timelines. CNTVCT_EL0 is the system counter: one fixed
 * frequency (CNTFRQ_EL0), shared by every core, always running. That is what
 * an invariant TSC means on x86, and it is the one that stays true under
 * SMP.
 *
 * User access needs CNTKCTL_EL1.EL0VCTEN set by the kernel.
 */
	u64_t cnt;

	asm volatile("mrs %0, cntvct_el0" : "=r" (cnt));

	*hi = (u32_t)(cnt >> 32);
	*lo = (u32_t)cnt;
}
