/*
 * The on-screen keyboard's side of a touch (keys.h).
 *
 * A touch that starts on a key presses it, and the key is released when
 * that touch ends - wherever it ends, so a finger that slides off does not
 * leave a key down.  Shift and Ctrl are one-shot: touching them arms them,
 * and the next key goes out between their press and release; touching an
 * armed one again disarms it.  A touch that starts off the keyboard does
 * nothing.  The layout and geometry are <minix/osk.h>, the same table the
 * frame buffer driver draws.
 */

#include <stddef.h>
#include <minix/osk.h>

#include "keys.h"

void
keys_init(struct keys *k, unsigned w, unsigned h,
	void (*emit)(unsigned usage, int down))
{
	unsigned i;

	k->w = w;
	k->h = h;
	k->emit = emit;
	k->shift = k->ctrl = 0;
	for (i = 0; i < KEYS_MAX_TOUCH; i++)
		k->t[i].active = 0;
}

static void
press(struct keys *k, unsigned id, unsigned x, unsigned y)
{
	const struct osk_key *key = osk_hit(x, y, k->w, k->h);

	k->t[id].active = 1;
	k->t[id].usage = 0;
	k->t[id].shift = k->t[id].ctrl = 0;
	if (key == NULL)
		return;
	if (key->usage == OSK_U_LSHIFT) {
		k->shift = !k->shift;
		return;
	}
	if (key->usage == OSK_U_LCTRL) {
		k->ctrl = !k->ctrl;
		return;
	}
	if (k->ctrl) {
		k->emit(OSK_U_LCTRL, 1);
		k->t[id].ctrl = 1;
	}
	if (k->shift) {
		k->emit(OSK_U_LSHIFT, 1);
		k->t[id].shift = 1;
	}
	k->shift = k->ctrl = 0;
	k->emit(key->usage, 1);
	k->t[id].usage = key->usage;
}

static void
release(struct keys *k, unsigned id)
{
	if (!k->t[id].active)
		return;
	if (k->t[id].usage != 0)
		k->emit(k->t[id].usage, 0);
	if (k->t[id].shift)
		k->emit(OSK_U_LSHIFT, 0);
	if (k->t[id].ctrl)
		k->emit(OSK_U_LCTRL, 0);
	k->t[id].active = 0;
	k->t[id].usage = 0;
	k->t[id].shift = k->t[id].ctrl = 0;
}

void
keys_touch(struct keys *k, unsigned id, int event, unsigned x, unsigned y)
{
	if (id >= KEYS_MAX_TOUCH)
		return;
	switch (event) {
	case TOUCH_DOWN:
		if (k->t[id].active)
			release(k, id);	/* a down we missed the up of */
		press(k, id, x, y);
		break;
	case TOUCH_CONTACT:
		if (!k->t[id].active)
			press(k, id, x, y);	/* the down was missed */
		break;
	case TOUCH_UP:
		release(k, id);
		break;
	}
}

void
keys_lost(struct keys *k, unsigned id)
{
	if (id < KEYS_MAX_TOUCH)
		release(k, id);
}
