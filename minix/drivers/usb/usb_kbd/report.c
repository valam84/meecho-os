/*
 * Two boot reports, and the key events between them.
 *
 * This is the only part of the driver that is arithmetic and nothing
 * else, and so it is in a file of its own with no MINIX in it: the host
 * stand in port/test/usbkbd compiles this very file with the host's
 * compiler and puts reports through it.
 *
 * A boot report (HID 1.11, appendix B.1) is what is held down NOW: byte 0
 * a bit per modifier (bit i is usage 0xE0 + i - left control, shift, alt,
 * GUI, then the same four on the right), byte 1 reserved, bytes 2 to 7 up
 * to six key codes, in no promised order, zero for an empty slot.  The
 * events are the difference from the previous report.
 *
 * Order matters to whoever turns the events into characters, so it is
 * fixed: releases before presses, and within each, keys around the
 * modifiers so that a modifier is never released before the key it
 * modified, nor pressed after it.  So:
 *
 *   keys released, modifiers released, modifiers pressed, keys pressed.
 */
#include "report.h"

#include <string.h>

static int
held_in(const uint8_t *r, unsigned code)
{
	unsigned i;

	for (i = 2; i < KBD_REPORT_LEN; i++)
		if (r[i] == code)
			return 1;
	return 0;
}

int
kbd_report_diff(const uint8_t old[KBD_REPORT_LEN],
	const uint8_t now[KBD_REPORT_LEN], kbd_event_fn ev, void *arg)
{
	unsigned i, bit;

	/*
	 * More keys than the keyboard can tell apart: every key slot says
	 * "rollover", and the report carries no state at all - the keys of
	 * the last good report are still down, and nothing new is known.
	 * Taking it as a report would release them all.
	 */
	for (i = 2; i < KBD_REPORT_LEN; i++)
		if (now[i] == KBD_KEY_ROLLOVER)
			return 0;

	for (i = 2; i < KBD_REPORT_LEN; i++)
		if (old[i] >= KBD_KEY_FIRST && !held_in(now, old[i]))
			ev(old[i], 0, arg);

	for (i = 0; i < 8; i++) {
		bit = 1u << i;
		if ((old[0] & bit) && !(now[0] & bit))
			ev(KBD_KEY_MOD_FIRST + i, 0, arg);
	}
	for (i = 0; i < 8; i++) {
		bit = 1u << i;
		if (!(old[0] & bit) && (now[0] & bit))
			ev(KBD_KEY_MOD_FIRST + i, 1, arg);
	}

	for (i = 2; i < KBD_REPORT_LEN; i++)
		if (now[i] >= KBD_KEY_FIRST && !held_in(old, now[i]))
			ev(now[i], 1, arg);

	return 1;
}
