#ifndef MEECHOWM_TOUCHSPLIT_H
#define MEECHOWM_TOUCHSPLIT_H

/* No wlroots here: the host stand (port/gfx/test/touchtest.sh) builds
 * touchsplit.c too. */
#include <stddef.h>
#include <stdint.h>

struct input_event;

#define MC_TOUCH_MAX	16	/* contacts followed; a number above is not */

/*
 * A touch screen's frames (<minix/input.h>, the digitizer page), as
 * contacts that go down, move and go up; x and y are fractions of the
 * screen, 0 to 1.  A frame after every read that had a touch in it.
 */
struct mc_touch_ops {
	void (*down)(unsigned id, double x, double y, void *arg);
	void (*motion)(unsigned id, double x, double y, void *arg);
	void (*up)(unsigned id, void *arg);
	void (*frame)(void *arg);
};

/* What is known between reads: a frame may be cut by one. */
struct mc_touch {
	unsigned id;			/* the contact the frame is about */
	int32_t x, y;			/* where, in INPUT_DIGI_MAX */
	int32_t at_x[MC_TOUCH_MAX], at_y[MC_TOUCH_MAX];
	uint8_t down[MC_TOUCH_MAX];	/* the contact touches */
};

/*
 * Takes the touch screen's events out of a read of the mice and acts on
 * them; the mice's are left, in their order, at the start of ev - their
 * count is returned.
 */
size_t mc_touch_split(struct mc_touch *t, struct input_event *ev, size_t n,
	const struct mc_touch_ops *ops, void *arg);

#endif /* MEECHOWM_TOUCHSPLIT_H */
