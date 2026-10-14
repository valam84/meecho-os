#ifndef MEECHOWM_MOUSESPLIT_H
#define MEECHOWM_MOUSESPLIT_H

/* No wlroots here: the host stand (port/gfx/test/mousetest.sh) builds
 * mousesplit.c too. */
#include <stddef.h>

struct input_event;

/*
 * What a read of the mouse turns into, report by report: the report's
 * motion (X and Y together), then its wheel and its buttons in the order
 * they came, then a frame.  The codes are the input server's: a button is
 * its HID usage (1 the left), the wheel is HID's (away from the user > 0).
 */
struct mc_mouse_ops {
	void (*motion)(int dx, int dy, void *arg);
	void (*wheel)(int value, void *arg);
	void (*button)(unsigned hid, int pressed, void *arg);
	void (*frame)(void *arg);
};

void mc_mouse_split(const struct input_event *ev, size_t n,
	const struct mc_mouse_ops *ops, void *arg);

#endif /* MEECHOWM_MOUSESPLIT_H */
