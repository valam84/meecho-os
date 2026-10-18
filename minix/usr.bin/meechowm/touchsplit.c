/*
 * A touch screen's frames out of a read of the mice (touchsplit.h).
 *
 * The input server merges a touch screen into /dev/mousemux with the mice:
 * its events are those of the digitizer page and an absolute X or Y - a
 * mouse's X and Y are always relative.  A contact's frame is its number,
 * where it is, and whether it touches (<minix/input.h>); the tip switch
 * ends it, and is what acts: pressed, a contact that was up goes down and
 * one that was down moves (if it did); released, it goes up.
 */
#include <stdint.h>

#include <minix/input.h>

#include "touchsplit.h"

static int
is_touch(const struct input_event *e)
{
	return e->page == INPUT_PAGE_DIGITIZER || (e->page == INPUT_PAGE_GD &&
	    (e->code == INPUT_GD_X || e->code == INPUT_GD_Y) &&
	    !(e->flags & INPUT_FLAG_REL));
}

static double
frac(int32_t v)
{
	if (v < 0)
		v = 0;
	if (v > INPUT_DIGI_MAX)
		v = INPUT_DIGI_MAX;
	return (double)v / INPUT_DIGI_MAX;
}

size_t
mc_touch_split(struct mc_touch *t, struct input_event *ev, size_t n,
	const struct mc_touch_ops *ops, void *arg)
{
	size_t i, left = 0;
	int acted = 0;
	unsigned id;

	for (i = 0; i < n; i++) {
		if (!is_touch(&ev[i])) {
			ev[left++] = ev[i];
			continue;
		}
		if (ev[i].page == INPUT_PAGE_GD) {
			if (ev[i].code == INPUT_GD_X)
				t->x = ev[i].value;
			else
				t->y = ev[i].value;
			continue;
		}
		if (ev[i].code == INPUT_DIGI_CONTACT_ID) {
			t->id = (unsigned)ev[i].value;
			continue;
		}
		if (ev[i].code != INPUT_DIGI_TIP_SWITCH ||
		    (id = t->id) >= MC_TOUCH_MAX)
			continue;
		if (ev[i].value == INPUT_RELEASE) {
			if (t->down[id]) {
				t->down[id] = 0;
				ops->up(id, arg);
				acted = 1;
			}
		} else if (!t->down[id]) {
			t->down[id] = 1;
			t->at_x[id] = t->x;
			t->at_y[id] = t->y;
			ops->down(id, frac(t->x), frac(t->y), arg);
			acted = 1;
		} else if (t->at_x[id] != t->x || t->at_y[id] != t->y) {
			t->at_x[id] = t->x;
			t->at_y[id] = t->y;
			ops->motion(id, frac(t->x), frac(t->y), arg);
			acted = 1;
		}
	}
	if (acted)
		ops->frame(arg);
	return left;
}
