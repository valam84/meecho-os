/*
 * Touches as the desktop's (desk.h): a frame per contact - its number,
 * where it is in fractions of the screen, and whether it touches - the way
 * <minix/input.h> says a touch screen speaks through the mice.  A contact
 * that was not down sends nothing when it lifts.
 */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <minix/input.h>

#include "desk.h"

void
desk_init(struct desk *d, unsigned w, unsigned h,
	void (*send)(unsigned page, unsigned code, int value, unsigned flags))
{
	d->w = w;
	d->h = h;
	d->down = 0;
	d->send = send;
}

static unsigned
frac(unsigned v, unsigned size)
{
	if (size < 2)
		return 0;
	if (v >= size)
		v = size - 1;
	return (unsigned)((uint64_t)v * INPUT_DIGI_MAX / (size - 1));
}

void
desk_touch_frac(struct desk *d, unsigned id, unsigned fx, unsigned fy)
{
	if (id >= DESK_TOUCHES)
		return;
	if (fx > INPUT_DIGI_MAX)
		fx = INPUT_DIGI_MAX;
	if (fy > INPUT_DIGI_MAX)
		fy = INPUT_DIGI_MAX;
	d->send(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID, (int)id, 0);
	d->send(INPUT_PAGE_GD, INPUT_GD_X, (int)fx, INPUT_FLAG_ABS);
	d->send(INPUT_PAGE_GD, INPUT_GD_Y, (int)fy, INPUT_FLAG_ABS);
	d->send(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH, INPUT_PRESS, 0);
	d->down |= 1u << id;
}

void
desk_touch(struct desk *d, unsigned id, unsigned x, unsigned y)
{
	desk_touch_frac(d, id, frac(x, d->w), frac(y, d->h));
}

void
desk_up(struct desk *d, unsigned id)
{
	if (id >= DESK_TOUCHES || !(d->down & (1u << id)))
		return;
	d->down &= ~(1u << id);
	d->send(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID, (int)id, 0);
	d->send(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH, INPUT_RELEASE, 0);
}

void
desk_lift(struct desk *d)
{
	unsigned i;

	for (i = 0; i < DESK_TOUCHES; i++)
		desk_up(d, i);
}
