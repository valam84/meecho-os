/*
 * Which real-time clock this machine has: asked of the device tree, not of
 * a board table. The 32-bit ports pick a clock by board ID; here a board
 * is a tree, and the clock is whichever node names a driver this file
 * knows. QEMU's virt has a PL031; the target board may have nothing at
 * all, and "no clock" is a machine this driver runs on, not one it
 * refuses. On such a machine it stays up and answers ENODEV, so that the
 * readclock command fails quietly and rc falls back to a fixed date - the
 * same path as a machine whose clock has lost its battery, and one that
 * leaves no time in the system at zero.
 *
 * The list below is of devices, not boards: one line per clock the driver
 * can drive, matched by the compatible string the tree gives it. A second
 * clock is a second line and a second file beside pl031.c.
 */

#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/log.h>
#include <minix/fdt.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "readclock.h"
#include "pl031.h"

static struct log log = {
	.name = "readclock.arch",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

struct rtc_driver {
	const char *compatible;
	int (*probe)(const struct fdt_node *node, struct rtc *r);
};

static const struct rtc_driver drivers[] = {
	{ "arm,pl031",	pl031_probe },
};

struct search {
	struct rtc *r;
	const struct rtc_driver *found;
};

static int
find_rtc(void *cookie, int depth, const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct search *s = cookie;
	unsigned i;

	if (depth == 0)
		return 0;

	for (i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
		if (!fdt_node_is_compatible(node, drivers[i].compatible))
			continue;
		if (drivers[i].probe(node, s->r) != OK)
			continue;
		s->found = &drivers[i];
		return 1;	/* stops the walk */
	}
	return 0;
}

/* The clock a machine without one has. */
static int
none_init(void)
{
	log_info(&log, "no real-time clock on this machine\n");
	return OK;
}

static int
none_get_time(struct tm *UNUSED(t), int UNUSED(flags))
{
	return ENODEV;
}

static int
none_set_time(struct tm *UNUSED(t), int UNUSED(flags))
{
	return ENODEV;
}

static int
none_pwr_off(void)
{
	return ENOSYS;
}

static void
none_exit(void)
{
}

int
arch_setup(struct rtc *r)
{
	struct search s;
	void *dtb;

	memset(&s, 0, sizeof(s));
	s.r = r;

	if ((dtb = fdt_fetch()) != NULL) {
		(void)fdt_walk(dtb, find_rtc, &s);
		free(dtb);
	}

	if (s.found != NULL) {
		log_debug(&log, "clock: %s\n", s.found->compatible);
		return OK;
	}

	r->init = none_init;
	r->get_time = none_get_time;
	r->set_time = none_set_time;
	r->pwr_off = none_pwr_off;
	r->exit = none_exit;
	return OK;
}
