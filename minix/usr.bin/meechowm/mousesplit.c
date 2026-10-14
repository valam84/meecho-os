/*
 * A read of /dev/mousemux, cut back into the mouse's reports.
 *
 * A mouse report is the motion along X and Y, the wheel and the buttons
 * together.  usb_mouse hands the input server one event for each part of
 * it that is there, always in one order - X, Y, the wheel, the releases,
 * the presses (report.c there) - and the input server keeps them in a ring
 * and marks no end of a report.  One read takes whatever is in the ring:
 * a part of a report, one, or several.
 *
 * The order gives the boundaries back: an event that does not come after
 * the one before it in that order begins the next report - a second X, a
 * Y after a button, a release after a press.  Each report then becomes
 * what it is to evdev: one motion (its X and Y), its wheel and its
 * buttons, and a frame.  The motion of a whole read used to be summed
 * and sent after the read's buttons, and two things a person does with a
 * mouse went wrong:
 *
 *   a press read together with the motion before it went to where the
 *   cursor had been before that motion - a click right after a quick move
 *   landed on what the cursor had just left;
 *
 *   the cursor stops at the edge of the screen, so motions do not add up:
 *   -127, +100, +100 from the left edge ends at 200, summed it ends at 73.
 *
 * A boundary the order does not show - a report of X alone, then one of Y
 * alone, or a release and then a press of the same button - joins parts
 * that one report never has twice, so no axis is summed across it and
 * nothing changes places: two reports share a frame, and that is all.  A
 * read that ends inside a report ends that report too, and the rest comes
 * with the next read as a report of its own: the cursor is not kept
 * waiting for the rest, and its motion still goes before its buttons.
 * Two mice need nothing more: the input server may interleave their
 * reports event by event, but they move one cursor, and what matters -
 * no axis summed twice, nothing sent out of turn - holds for any stream.
 * So the cursor ends where it would if every event went alone, and every
 * button and notch of the wheel comes where it would then.
 */
#include <sys/types.h>

#include <stdint.h>
#include <string.h>

#include <minix/input.h>

#include "mousesplit.h"

/* Where an event stands in its report. */
#define PLACE_NONE	0		/* not a part of one */
#define PLACE_X		1
#define PLACE_Y		2
#define PLACE_WHEEL	3
#define PLACE_RELEASE	0x10000		/* + the button's usage */
#define PLACE_PRESS	0x20000		/* + the button's usage */

struct report {
	uint32_t last;		/* the place of its last event; NONE: empty */
	int dx, dy;		/* its motion, not sent yet */
	int sent;		/* something of it has gone */
};

static uint32_t
place(const struct input_event *e)
{
	if (e->page == INPUT_PAGE_GD) {
		switch (e->code) {
		case INPUT_GD_X:
			return PLACE_X;
		case INPUT_GD_Y:
			return PLACE_Y;
		case INPUT_GD_WHEEL:
			return PLACE_WHEEL;
		}
	} else if (e->page == INPUT_PAGE_BUTTON)
		return (e->value != INPUT_RELEASE ? PLACE_PRESS :
		    PLACE_RELEASE) + e->code;
	return PLACE_NONE;
}

static void
send_motion(struct report *r, const struct mc_mouse_ops *ops, void *arg)
{
	if (r->dx == 0 && r->dy == 0)
		return;
	ops->motion(r->dx, r->dy, arg);
	r->dx = r->dy = 0;
	r->sent = 1;
}

static void
end_report(struct report *r, const struct mc_mouse_ops *ops, void *arg)
{
	send_motion(r, ops, arg);
	if (r->sent)
		ops->frame(arg);
	memset(r, 0, sizeof(*r));
}

void
mc_mouse_split(const struct input_event *ev, size_t n,
	const struct mc_mouse_ops *ops, void *arg)
{
	struct report r;
	uint32_t p;
	size_t i;

	memset(&r, 0, sizeof(r));
	for (i = 0; i < n; i++) {
		if ((p = place(&ev[i])) == PLACE_NONE)
			continue;
		if (r.last != PLACE_NONE && p <= r.last)
			end_report(&r, ops, arg);
		r.last = p;

		switch (p) {
		case PLACE_X:
			r.dx += ev[i].value;
			break;
		case PLACE_Y:
			r.dy += ev[i].value;
			break;
		default:
			/* The report's motion comes before the rest of it. */
			send_motion(&r, ops, arg);
			if (p == PLACE_WHEEL)
				ops->wheel(ev[i].value, arg);
			else
				ops->button(ev[i].code,
				    ev[i].value != INPUT_RELEASE, arg);
			r.sent = 1;
		}
	}
	end_report(&r, ops, arg);
}
