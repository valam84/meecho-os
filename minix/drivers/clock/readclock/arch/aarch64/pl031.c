/*
 * The ARM PrimeCell PL031 real-time clock, as QEMU's virt machine has it.
 *
 * The device is found in the device tree by arch_readclock.c and the
 * registers are granted by RS from the same tree ("devicetree "arm,pl031";"
 * in system.conf); here there is no address of anything. The counter is
 * seconds since the epoch, so reading the time is one register and a
 * calendar conversion, and setting it is the conversion the other way and
 * one register.
 *
 * The conversion is done here rather than through gmtime(3): the driver is
 * linked against libminc, and a calendar is thirty lines, which is less
 * than the question of what a time-zone library does in a driver.
 */

#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/log.h>
#include <minix/type.h>

#include <sys/mman.h>
#include <sys/types.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "pl031.h"

static struct log log = {
	.name = "pl031",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* Where the tree says the registers are, and where they are mapped. */
static phys_bytes pl031_base;
static size_t pl031_size;
static vir_bytes pl031_regs;

static u32_t
reg_read(unsigned off)
{
	return *(volatile u32_t *)(pl031_regs + off);
}

static void
reg_write(unsigned off, u32_t val)
{
	*(volatile u32_t *)(pl031_regs + off) = val;
}

/*
 * The proleptic Gregorian calendar, both ways, on days since 1970-01-01.
 * The algorithms are Howard Hinnant's: exact for every date the counter
 * can name, and with no table of month lengths to get wrong.
 */
static int64_t
days_from_civil(int64_t y, unsigned m, unsigned d)
{
	int64_t era;
	unsigned yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

static void
civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
	int64_t era;
	unsigned doe, yoe, doy, mp;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned)(z - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = (int64_t)yoe + era * 400 + (*m <= 2);
}

static void
seconds_to_tm(uint64_t secs, struct tm *t)
{
	int64_t days, year;
	unsigned mon, mday, rem;

	days = (int64_t)(secs / 86400);
	rem = (unsigned)(secs % 86400);
	civil_from_days(days, &year, &mon, &mday);

	memset(t, 0, sizeof(*t));
	t->tm_sec = rem % 60;
	t->tm_min = (rem / 60) % 60;
	t->tm_hour = rem / 3600;
	t->tm_mday = mday;
	t->tm_mon = mon - 1;
	t->tm_year = (int)(year - 1900);
	t->tm_wday = (int)((days + 4) % 7);	/* the epoch was a Thursday */
	t->tm_yday = (int)(days - days_from_civil(year, 1, 1));
	t->tm_isdst = 0;
}

static int
tm_to_seconds(const struct tm *t, uint64_t *secs)
{
	int64_t days, s;

	if (t->tm_mon < 0 || t->tm_mon > 11 || t->tm_mday < 1 ||
	    t->tm_mday > 31 || t->tm_hour < 0 || t->tm_hour > 23 ||
	    t->tm_min < 0 || t->tm_min > 59 || t->tm_sec < 0 || t->tm_sec > 60)
		return EINVAL;

	days = days_from_civil((int64_t)t->tm_year + 1900, t->tm_mon + 1,
	    t->tm_mday);
	s = days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;

	/* The counter is 32 bits wide; 2106 is as far as it goes. */
	if (s < 0 || s > 0xffffffffLL)
		return ERANGE;

	*secs = (uint64_t)s;
	return OK;
}

static int
pl031_init(void)
{
	void *v;
	u32_t part;

	if (pl031_regs != 0)
		return OK;	/* a restart; already mapped */

	v = vm_map_phys(SELF, (void *)pl031_base, pl031_size);
	if (v == MAP_FAILED) {
		log_warn(&log, "cannot map the registers at 0x%lx; "
		    "not granted by RS?\n", (unsigned long)pl031_base);
		return EPERM;
	}
	pl031_regs = (vir_bytes)v;

	/*
	 * The tree said PL031; the part answers for itself. A mismatch is
	 * not a clock that runs wrong, it is a register block that is
	 * something else, and that is worth not touching.
	 */
	part = (reg_read(PL031_PERIPHID0) & 0xff) |
	    ((reg_read(PL031_PERIPHID1) & 0x0f) << 8);
	if (part != PL031_PART_NUMBER) {
		log_warn(&log, "the node claims a PL031, the part says 0x%x\n",
		    part);
		vm_unmap_phys(SELF, (void *)pl031_regs, pl031_size);
		pl031_regs = 0;
		return ENXIO;
	}

	/* Once started it cannot be stopped, so starting it is idempotent. */
	if ((reg_read(PL031_RTCCR) & PL031_RTCCR_START) == 0)
		reg_write(PL031_RTCCR, PL031_RTCCR_START);

	log_debug(&log, "PL031 at 0x%lx\n", (unsigned long)pl031_base);
	return OK;
}

static int
pl031_get_time(struct tm *t, int UNUSED(flags))
{
	seconds_to_tm(reg_read(PL031_RTCDR), t);
	return OK;
}

static int
pl031_set_time(struct tm *t, int UNUSED(flags))
{
	uint64_t secs;
	int r;

	if ((r = tm_to_seconds(t, &secs)) != OK)
		return r;

	reg_write(PL031_RTCLR, (u32_t)secs);
	return OK;
}

static int
pl031_pwr_off(void)
{
	/* An alarm it has; a hand on the power it has not. */
	return ENOSYS;
}

static void
pl031_exit(void)
{
	if (pl031_regs != 0)
		vm_unmap_phys(SELF, (void *)pl031_regs, pl031_size);
	pl031_regs = 0;
}

/*
 * Called for a node whose "compatible" names the PL031. Records where the
 * registers are and fills in the operations; the mapping waits for init.
 */
int
pl031_probe(const struct fdt_node *node, struct rtc *r)
{
	u64_t base, size;

	if (fdt_node_reg(node, 0, &base, &size) != 0)
		return ENXIO;
	if (size < PL031_MIN_SIZE)
		return ENXIO;

	pl031_base = (phys_bytes)base;
	pl031_size = (size_t)size;

	r->init = pl031_init;
	r->get_time = pl031_get_time;
	r->set_time = pl031_set_time;
	r->pwr_off = pl031_pwr_off;
	r->exit = pl031_exit;
	return OK;
}
