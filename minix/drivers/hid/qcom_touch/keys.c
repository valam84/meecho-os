/*
 * The console keyboard's side of a touch (keys.h).
 *
 * The keyboard itself - its layers, the modifiers that stick, which key a
 * touch is on and when that key goes down and up - is libosk's, and this
 * driver is its owner: fb only draws what the owner publishes.  What is
 * left here is the screen around it.  The keyboard is the bottom
 * osk_console_height() lines of the screen, the same rectangle fb draws it
 * in; a touch above it is a touch on no key, so a finger that starts on the
 * text and slides down presses nothing.
 *
 * And it takes touches only while fb says its console is on the screen
 * with the keyboard drawn (keys_show()): under the test card, or under a
 * compositor that has the screen, a touch at the bottom is not a key.
 * When the keyboard goes away everything it holds is let go - a key held
 * down, a locked Ctrl - so nothing stays pressed behind the screen's back;
 * the layer it showed it keeps.
 */

#include <stddef.h>
#include <minix/libosk.h>

#include "keys.h"

static void
emit(void *arg, unsigned usage, int down)
{
	struct keys *k = arg;

	k->emit(usage, down);
}

void
keys_init(struct keys *k, unsigned w, unsigned h,
	void (*emitf)(unsigned usage, int down))
{
	unsigned kh = osk_console_height(h);

	k->w = w;
	k->h = h;
	k->top = h - kh;
	k->shown = 0;
	k->emit = emitf;
	osk_init(&k->osk, w, kh, emit, k);
}

int
keys_touch(struct keys *k, unsigned id, int event, unsigned x, unsigned y)
{
	if (!k->shown)
		return 0;
	/* Above the keyboard: below its rectangle, on no key. */
	return osk_touch(&k->osk, id, event, x,
	    y >= k->top ? y - k->top : k->osk.h);
}

int
keys_lost(struct keys *k, unsigned id)
{
	return k->shown ? osk_lost(&k->osk, id) : 0;
}

int
keys_show(struct keys *k, int shown)
{
	shown = shown != 0;
	if (shown == k->shown)
		return 0;
	k->shown = shown;
	return shown ? 0 : osk_reset(&k->osk);
}

uint32_t
keys_word(const struct keys *k)
{
	return osk_word(&k->osk);
}
