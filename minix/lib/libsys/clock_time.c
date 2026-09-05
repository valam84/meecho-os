
#include "sysutil.h"
#include <stdint.h>
#include <sys/time.h>

/*
 * This routine returns the time in seconds since 1.1.1970.  MINIX is an
 * astrophysically naive system that assumes the earth rotates at a constant
 * rate and that such things as leap seconds do not exist.  If a non-NULL
 * pointer to a timespec structure is given, that structure is filled with
 * the current time in subsecond precision.
 */
time_t
clock_time(struct timespec *tv)
{
	struct minix_kerninfo *minix_kerninfo;
	uint32_t system_hz;
	clock_t realtime;
	time_t boottime, sec;

	minix_kerninfo = get_minix_kerninfo();

	/* We assume atomic 32-bit field retrieval.  TODO: 64-bit support. */
	boottime = minix_kerninfo->kclockinfo->boottime;
	realtime = minix_kerninfo->kclockinfo->realtime;
	system_hz = minix_kerninfo->kclockinfo->hz;

	sec = boottime + realtime / system_hz;

	if (tv != NULL) {
		tv->tv_sec = sec;

		/*
		 * Ticks to nanoseconds, in one step and without an
		 * intermediate that can overflow: system_hz can be as high as
		 * 50kHz and the numerator is a billion, so the product needs
		 * 64 bits and is given them.
		 *
		 * What this replaced multiplied by 40000 and then by 25000 to
		 * stay inside 32 bits, and guarded itself with LONG_MAX /
		 * 40000 - a limit belonging to a type the arithmetic was not
		 * done in. Where long is 64 bits that test is always true, so
		 * the guard protected nothing and the fallback that returned
		 * no subsecond time at all was unreachable.
		 */
		tv->tv_nsec = (long)((uint64_t)(realtime % system_hz) *
		    1000000000ULL / system_hz);
	}

	return sec;
}
