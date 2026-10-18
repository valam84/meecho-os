/*
 * Touches on the screen as the console keyboard's (keys.c): the keyboard is
 * libosk's (<minix/libosk.h>), this is where it sits on the screen and when
 * it takes touches at all.  No MINIX calls: the host test
 * (port/test/qcom_touch/keystest.c) drives it.
 */
#ifndef _KEYS_H
#define _KEYS_H

#include <stdint.h>
#include <minix/libosk.h>

/* Touch events, as the controller reports them - libosk's numbers. */
#define TOUCH_DOWN	OSK_TOUCH_DOWN
#define TOUCH_UP	OSK_TOUCH_UP
#define TOUCH_CONTACT	OSK_TOUCH_CONTACT

struct keys {
	struct osk	osk;
	unsigned	w, h;		/* the screen, in pixels */
	unsigned	top;		/* the keyboard's first line */
	int		shown;		/* the console's keyboard is on screen */
	void		(*emit)(unsigned usage, int down);
};

void keys_init(struct keys *k, unsigned w, unsigned h,
	void (*emit)(unsigned usage, int down));
/*
 * A touch at (x, y) on the screen; a lost one; whether the keyboard is on
 * the screen now.  Each returns nonzero if what the keyboard shows changed:
 * then keys_word() is the word to publish.
 */
int keys_touch(struct keys *k, unsigned id, int event, unsigned x,
	unsigned y);
int keys_lost(struct keys *k, unsigned id);
int keys_show(struct keys *k, int shown);
uint32_t keys_word(const struct keys *k);

#endif /* _KEYS_H */
