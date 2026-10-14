/*
 * One boot mouse report, and the events in it.
 *
 * This is the only part of the driver that is arithmetic and nothing
 * else, and so it is in a file of its own with no MINIX in it: the host
 * stand in port/test/usbmouse compiles this very file with the host's
 * compiler and puts reports through it.
 *
 * A boot mouse report (HID 1.11, appendix B.2) is three bytes: a bit per
 * button in byte 0 (bit 0 the left button, 1 the right, 2 the middle; the
 * other five bits are padding in the boot descriptor), then the motion
 * since the last report along X and along Y, each a signed byte.  Unlike a
 * keyboard's, the report is not a state but a difference - the motion is
 * already relative, and only the buttons have to be compared with the
 * report before.
 *
 * The specification lets a device send more than three bytes in boot
 * protocol and tells the host to ignore the rest.  Nearly every mouse with
 * a wheel puts it in the fourth byte, signed, and that is taken when the
 * driver is told to (wheel=1, the default); a device that puts something
 * else there turns it off with wheel=0.
 *
 * The signs are HID's and are passed on as they are: X grows to the right,
 * Y grows toward the user - down the screen - and the wheel grows away
 * from the user.  The input server's events are HID's by its own header;
 * the PS/2 path of pckbd passes the PS/2 sign of Y, which is the other
 * way, and that is pckbd's to answer for.
 *
 * Order: the motion first and the buttons after, because a report says
 * "moved this far, and the buttons are now these" - a click that comes
 * with motion lands where the motion ended.  Releases before presses, so
 * that a reader never sees more buttons down at once than were.
 *
 * The order is a contract: the input server marks no end of a report, and
 * meechowm (mousesplit.c) finds the ends by it - an event out of this
 * order begins the next report.  port/gfx/test/mousetest.sh builds this
 * file in front of that one.
 */
#include "report.h"

static int
sbyte(uint8_t b)
{
	return b < 0x80 ? (int)b : (int)b - 0x100;
}

unsigned
mouse_report(unsigned old, const uint8_t *rep, size_t len, int wheel,
	mouse_event_fn ev, void *arg)
{
	unsigned now, bit, i;
	int v;

	if (len < MOUSE_REPORT_MIN)
		return old;

	if ((v = sbyte(rep[1])) != 0)
		ev(MOUSE_PAGE_GD, MOUSE_GD_X, v, 1, arg);
	if ((v = sbyte(rep[2])) != 0)
		ev(MOUSE_PAGE_GD, MOUSE_GD_Y, v, 1, arg);
	if (wheel && len > MOUSE_REPORT_MIN && (v = sbyte(rep[3])) != 0)
		ev(MOUSE_PAGE_GD, MOUSE_GD_WHEEL, v, 1, arg);

	now = rep[0] & ((1u << MOUSE_BUTTONS) - 1);

	for (i = 0; i < MOUSE_BUTTONS; i++) {
		bit = 1u << i;
		if ((old & bit) && !(now & bit))
			ev(MOUSE_PAGE_BUTTON, MOUSE_BUTTON_1 + i, 0, 0, arg);
	}
	for (i = 0; i < MOUSE_BUTTONS; i++) {
		bit = 1u << i;
		if (!(old & bit) && (now & bit))
			ev(MOUSE_PAGE_BUTTON, MOUSE_BUTTON_1 + i, 1, 0, arg);
	}

	return now;
}
