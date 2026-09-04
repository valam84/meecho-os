
#include <stdio.h>
#include <time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <minix/u64.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/minlib.h>
#include <machine/archtypes.h>

#include "sysutil.h"

#ifndef CONFIG_MAX_CPUS
#define CONFIG_MAX_CPUS 1
#endif

#define MICROHZ		1000000		/* number of micros per second */
#define MICROSPERTICK(h)	(MICROHZ/(h))	/* number of micros per HZ tick */

/* read_tsc() on AArch64 reads CNTVCT_EL0, the system counter, whose rate
 * is CNTFRQ_EL0. No calibration is needed: the frequency is a register,
 * not a measurement, and it is the same on every core.
 */
static u32_t
tsc_hz(void)
{
	u64_t freq;

	asm volatile("mrs %0, cntfrq_el0" : "=r" (freq));
	return (u32_t) freq;
}

u32_t tsc_64_to_micros(u64_t tsc)
{
	u64_t tmp;

	tmp = tsc / (tsc_hz() / MICROHZ);
	if (ex64hi(tmp)) {
		printf("tsc_64_to_micros: more than 2^32ms\n");
		return ~0UL;
	} else {
		return ex64lo(tmp);
	}
}

u32_t tsc_to_micros(u32_t low, u32_t high)
{
	return tsc_64_to_micros(make64(low, high));
}

u32_t tsc_get_khz(void)
{
	return tsc_hz() / 1000;
}
