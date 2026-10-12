#ifndef _USB_KBD_REPORT_H
#define _USB_KBD_REPORT_H

/* No MINIX here: the host stand (port/test/usbkbd) builds report.c too. */
#include <stdint.h>

#define KBD_REPORT_LEN		8	/* a boot keyboard report */

/* Codes in a report that are not keys (HID usage tables, 10). */
#define KBD_KEY_NONE		0x00
#define KBD_KEY_ROLLOVER	0x01	/* too many keys: no state at all */
#define KBD_KEY_FIRST		0x04	/* the first real key, "a" */
#define KBD_KEY_MOD_FIRST	0xe0	/* left control; bit 0 of byte 0 */

/* One key went down (press 1) or came up (press 0). */
typedef void (*kbd_event_fn)(unsigned code, int press, void *arg);

/*
 * The events between two reports, in a fixed order: keys released,
 * modifiers released, modifiers pressed, keys pressed.  Answers 0, and
 * calls nothing, for a report that carries no state (rollover) - the
 * caller keeps the old one.
 */
int kbd_report_diff(const uint8_t old[KBD_REPORT_LEN],
	const uint8_t now[KBD_REPORT_LEN], kbd_event_fn ev, void *arg);

#endif /* _USB_KBD_REPORT_H */
