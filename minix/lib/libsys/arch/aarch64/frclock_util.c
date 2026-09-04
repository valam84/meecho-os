/* The free-running clock on AArch64 is the system counter of the ARM
 * generic timer: CNTVCT_EL0, 64 bits wide, ticking at CNTFRQ_EL0 on every
 * core alike. Both registers are readable from user mode once the kernel
 * has set CNTKCTL_EL1.EL0VCTEN, so unlike earm there is nothing to map and
 * nothing to ask the kernel for: the counter is a system register, and the
 * 32-bit wrap handling of the earm version has no counterpart here.
 */

#include <minix/minlib.h>
#include <minix/sysutil.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <lib.h>
#include <assert.h>

#define MICROHZ         1000000ULL	/* number of micros per second */
#define MICROSPERTICK(h)	(MICROHZ/(h)) /* number of micros per HZ tick */

static u64_t Hz;

static u64_t
frclock_hz(void)
{
	u64_t freq;

	asm volatile("mrs %0, cntfrq_el0" : "=r" (freq));
	assert(freq != 0);
	return freq;
}

int
micro_delay(u32_t micros)
{
	u64_t start, delta, delta_end;

	Hz = sys_hz();

	/* Start of delay. */
	read_frclock_64(&start);
	delta_end = (frclock_hz() * micros) / MICROHZ;

	/* If we have to wait for at least one HZ tick, use the regular
	 * tickdelay first. Round downwards on purpose, so the average
	 * half-tick we wait short (depending on where in the current tick
	 * we call tickdelay). We can correct for both overhead of tickdelay
	 * itself and the short wait in the busywait later.
	 */
	if (micros >= MICROSPERTICK(Hz))
		tickdelay(micros*Hz/MICROHZ);

	/* Wait (the rest) of the delay time using busywait. */
	do {
		read_frclock_64(&delta);
	} while (delta_frclock_64(start, delta) < delta_end);

	return 0;
}

u32_t
frclock_64_to_micros(u64_t tsc)
{
	return (u32_t) (tsc / (frclock_hz() / MICROHZ));
}

void
read_frclock_64(u64_t *frclk)
{
	assert(frclk);
	asm volatile("mrs %0, cntvct_el0" : "=r" (*frclk));
}

u64_t
delta_frclock_64(u64_t base, u64_t cur)
{
	/* A 64-bit counter at any plausible frequency does not wrap in the
	 * lifetime of the hardware. */
	return cur - base;
}
