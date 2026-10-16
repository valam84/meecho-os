/*
 * Touches on the on-screen keyboard as key events (keys.c).  No MINIX
 * calls: the host test (port/test/qcom_touch/keystest.c) drives it.
 */
#ifndef _KEYS_H
#define _KEYS_H

#define KEYS_MAX_TOUCH	10

/* Touch events, as the controller reports them. */
#define TOUCH_DOWN	0
#define TOUCH_UP	1
#define TOUCH_CONTACT	2

struct keys {
	unsigned	w, h;		/* the screen, in pixels */
	void		(*emit)(unsigned usage, int down);

	int		shift, ctrl;	/* one-shot, waiting for a key */
	struct {
		int	active;
		unsigned usage;		/* 0: on no key, or a modifier */
		int	shift, ctrl;	/* pressed along with it */
	} t[KEYS_MAX_TOUCH];
};

void keys_init(struct keys *k, unsigned w, unsigned h,
	void (*emit)(unsigned usage, int down));
void keys_touch(struct keys *k, unsigned id, int event, unsigned x,
	unsigned y);
/* A touch the last frame had and this one does not: let it go. */
void keys_lost(struct keys *k, unsigned id);

#endif /* _KEYS_H */
