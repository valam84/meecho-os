#ifndef _USB_MOUSE_REPORT_H
#define _USB_MOUSE_REPORT_H

/* No MINIX here: the host stand (port/test/usbmouse) builds report.c too. */
#include <stdint.h>
#include <stddef.h>

#define MOUSE_REPORT_MIN	3	/* buttons, X, Y: a boot mouse report */
#define MOUSE_BUTTONS		3	/* what the boot report defines */

/*
 * What a report turns into: relative motion along an axis, or a button
 * going down or up.  The codes are the input server's (<minix/input.h>),
 * which are the HID usages; they are repeated here because this file is
 * built without MINIX headers.
 */
#define MOUSE_PAGE_GD		0x0001
#define MOUSE_PAGE_BUTTON	0x0009
#define MOUSE_GD_X		0x0030
#define MOUSE_GD_Y		0x0031
#define MOUSE_GD_WHEEL		0x0038
#define MOUSE_BUTTON_1		0x0001

typedef void (*mouse_event_fn)(unsigned page, unsigned code, int value,
	int rel, void *arg);

/*
 * The events in one report, given the buttons that were down before it:
 * motion first (X, Y, then the wheel if "wheel" and the report has a
 * fourth byte), then buttons released, then buttons pressed.  Answers the
 * buttons that are down now - the caller keeps that for the next report -
 * or "old" unchanged, calling nothing, for a report too short to be one.
 */
unsigned mouse_report(unsigned old, const uint8_t *rep, size_t len,
	int wheel, mouse_event_fn ev, void *arg);

#endif /* _USB_MOUSE_REPORT_H */
