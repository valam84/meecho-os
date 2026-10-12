/*
 * The USB keyboard driver's report arithmetic, on the host.
 *
 * Builds minix/drivers/usb/usb_kbd/report.c - the driver's file, not a
 * copy - and puts boot reports through it.  What it checks is what a user
 * would see go wrong at the console: a key that never comes up, a shift
 * that is let go before the letter it shifted, a burst of keys pressed in
 * one report coming out in a different order, a rollover report taken as
 * "everything was released".
 *
 *   sh run.sh
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "report.h"

static unsigned checks, failures;

static void
check(int ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

/* The events of one diff, as a string: "+04 -e1" and so on. */
static char got[256];

static void
record(unsigned code, int press, void *arg)
{
	char one[8];

	snprintf(one, sizeof(one), "%s%c%02x", got[0] ? " " : "",
	    press ? '+' : '-', code);
	strncat(got, one, sizeof(got) - strlen(got) - 1);
}

static uint8_t held[KBD_REPORT_LEN];

/* Feed one report; answer what came out, and keep it if it counted. */
static const char *
feed(uint8_t m, uint8_t k0, uint8_t k1, uint8_t k2, uint8_t k3, uint8_t k4,
	uint8_t k5)
{
	uint8_t now[KBD_REPORT_LEN] = { m, 0, k0, k1, k2, k3, k4, k5 };

	got[0] = '\0';
	if (kbd_report_diff(held, now, record, NULL))
		memcpy(held, now, sizeof(held));
	return got;
}

static void
expect(const char *what, const char *want, const char *have)
{
	check(strcmp(want, have) == 0, "%s: want \"%s\", got \"%s\"", what,
	    want, have);
}

int
main(void)
{
	memset(held, 0, sizeof(held));

	/* One key, down and up. */
	expect("a down", "+04", feed(0, 0x04, 0, 0, 0, 0, 0));
	expect("a held, the same report again", "",
	    feed(0, 0x04, 0, 0, 0, 0, 0));
	expect("a up", "-04", feed(0, 0, 0, 0, 0, 0, 0));

	/*
	 * Shift and a letter in one report: the shift goes down first,
	 * or tty maps the letter unshifted.  And both up in one report:
	 * the letter comes up first.
	 */
	expect("shift+a down together", "+e1 +04",
	    feed(0x02, 0x04, 0, 0, 0, 0, 0));
	expect("shift+a up together", "-04 -e1",
	    feed(0, 0, 0, 0, 0, 0, 0));

	/* Right-hand modifiers are bits 4 to 7: right alt is 0xe6. */
	expect("right alt", "+e6", feed(0x40, 0, 0, 0, 0, 0, 0));
	expect("right alt up", "-e6", feed(0, 0, 0, 0, 0, 0, 0));

	/* Two keys in one report come out in slot order. */
	expect("two keys at once", "+04 +05",
	    feed(0, 0x04, 0x05, 0, 0, 0, 0));

	/*
	 * A key that moves slot is not a release: keyboards compact the
	 * array when an earlier key is let go.
	 */
	expect("a up, b moves to slot 0", "-04",
	    feed(0, 0x05, 0, 0, 0, 0, 0));

	/* A release and a press in one report: the release first. */
	expect("b up and c down at once", "-05 +06",
	    feed(0, 0, 0x06, 0, 0, 0, 0));

	/*
	 * Rollover: every slot says 0x01.  Nothing happened that the
	 * report can tell - c is still down - and the state is kept.
	 */
	expect("rollover", "", feed(0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01));
	check(held[3] == 0x06, "the state before the rollover was kept");
	expect("after the rollover, c up", "-06",
	    feed(0, 0, 0, 0, 0, 0, 0));

	/* Codes 0 to 3 are not keys. */
	expect("codes 2 and 3", "", feed(0, 0x02, 0x03, 0, 0, 0, 0));

	/* Six keys, and all up at once, in slot order. */
	expect("six keys", "+04 +05 +06 +07 +08 +09",
	    feed(0, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09));
	expect("six up", "-04 -05 -06 -07 -08 -09",
	    feed(0, 0, 0, 0, 0, 0, 0));

	/*
	 * Control held across keys: control down, c, c up, control up -
	 * the ^C typed at a shell, and the order tty must see.
	 */
	expect("ctrl", "+e0", feed(0x01, 0, 0, 0, 0, 0, 0));
	expect("ctrl+c", "+06", feed(0x01, 0x06, 0, 0, 0, 0, 0));
	expect("c up, ctrl held", "-06", feed(0x01, 0, 0, 0, 0, 0, 0));
	expect("ctrl up", "-e0", feed(0, 0, 0, 0, 0, 0, 0));

	printf("%u checks, %u failure(s)\n", checks, failures);
	return failures != 0;
}
