/*
 * Touches on the screen as the desktop's (desk.c): frames of the digitizer
 * page (<minix/input.h>) through the mice, for meechowm - while the screen
 * is not the console's keyboard's.  No MINIX calls: the host test
 * (port/test/qcom_touch/desktest.c) drives it.
 */
#ifndef _DESK_H
#define _DESK_H

#include <stdint.h>

#define DESK_TOUCHES	16	/* contacts followed (MC_TOUCH_MAX) */

struct desk {
	unsigned	w, h;		/* the screen, in pixels */
	uint32_t	down;		/* contacts down, a bit each */
	/* An event to the input server: page, code, value, flags. */
	void		(*send)(unsigned page, unsigned code, int value,
			    unsigned flags);
};

void desk_init(struct desk *d, unsigned w, unsigned h,
	void (*send)(unsigned page, unsigned code, int value, unsigned flags));
/* Contact id down (or still down) at pixel (x, y) of the screen. */
void desk_touch(struct desk *d, unsigned id, unsigned x, unsigned y);
/* The same where x and y are already fractions, 0 to INPUT_DIGI_MAX. */
void desk_touch_frac(struct desk *d, unsigned id, unsigned fx, unsigned fy);
/* Contact id lifted; every contact lifted. */
void desk_up(struct desk *d, unsigned id);
void desk_lift(struct desk *d);

#endif /* _DESK_H */
