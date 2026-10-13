/*
 * The USB mouse report stand: the driver's own report.c, built by the
 * host compiler, against boot reports whose events are known.
 *
 * Every check names the events it expects, in order, as a string - "X+3
 * Y-1 B1v" is three right, one up, the left button down - so that a wrong
 * order is as visible as a wrong value.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "report.h"

static char got[512];
static int fails, checks;

static void
ev(unsigned page, unsigned code, int value, int rel, void *arg)
{
	char one[32];

	(void)arg;
	if (page == MOUSE_PAGE_GD) {
		const char *axis = code == MOUSE_GD_X ? "X" :
		    code == MOUSE_GD_Y ? "Y" : code == MOUSE_GD_WHEEL ? "W" :
		    "?";
		snprintf(one, sizeof(one), "%s%+d%s ", axis, value,
		    rel ? "" : "abs");
	} else if (page == MOUSE_PAGE_BUTTON) {
		snprintf(one, sizeof(one), "B%u%s%s ", code - MOUSE_BUTTON_1 + 1,
		    value ? "v" : "^", rel ? "rel" : "");
	} else
		snprintf(one, sizeof(one), "page%u ", page);
	strncat(got, one, sizeof(got) - strlen(got) - 1);
}

/* One report through report.c; "want" is the events, "now" the buttons. */
static unsigned
check(const char *name, unsigned old, const unsigned char *rep, size_t len,
	int wheel, const char *want, unsigned want_now)
{
	unsigned now;
	size_t n;

	got[0] = '\0';
	now = mouse_report(old, rep, len, wheel, ev, NULL);
	n = strlen(got);
	if (n > 0)
		got[n - 1] = '\0';
	checks++;
	if (strcmp(got, want) != 0 || now != want_now) {
		printf("FAIL %s: got \"%s\" (buttons %u), want \"%s\" "
		    "(buttons %u)\n", name, got, now, want, want_now);
		fails++;
	} else
		printf("ok   %s\n", name);
	return now;
}

int
main(void)
{
	unsigned b;

	{
		const unsigned char r[] = { 0, 3, 0 };
		check("right three", 0, r, 3, 1, "X+3", 0);
	}
	{
		const unsigned char r[] = { 0, 0xff, 0x80 };
		check("signed: left one, up 128", 0, r, 3, 1, "X-1 Y-128",
		    0);
	}
	{
		const unsigned char r[] = { 0, 0x7f, 0x01 };
		check("signed: the largest steps", 0, r, 3, 1, "X+127 Y+1", 0);
	}
	{
		const unsigned char r[] = { 0, 0, 0 };
		check("no motion, no event", 0, r, 3, 1, "", 0);
	}
	{
		const unsigned char r[] = { 1, 0, 0 };
		b = check("left button down", 0, r, 3, 1, "B1v", 1);
	}
	{
		const unsigned char r[] = { 1, 0, 0 };
		b = check("held: no event", b, r, 3, 1, "", 1);
	}
	{
		const unsigned char r[] = { 0, 0, 0 };
		b = check("left button up", b, r, 3, 1, "B1^", 0);
	}
	{
		const unsigned char r[] = { 5, 2, 0xfe };
		b = check("a click that moves: motion first", 0, r, 3, 1,
		    "X+2 Y-2 B1v B3v", 5);
	}
	{
		const unsigned char r[] = { 2, 0, 0 };
		check("a change of hands: releases first", b, r, 3, 1,
		    "B1^ B3^ B2v", 2);
	}
	{
		const unsigned char r[] = { 0xf8, 0, 0 };
		check("padding bits are not buttons", 0, r, 3, 1, "", 0);
	}
	{
		const unsigned char r[] = { 0, 1, 1, 0xff };
		check("the wheel, toward the user", 0, r, 4, 1, "X+1 Y+1 W-1",
		    0);
	}
	{
		const unsigned char r[] = { 0, 0, 0, 2 };
		check("wheel=0 ignores the fourth byte", 0, r, 4, 0, "", 0);
	}
	{
		const unsigned char r[] = { 0, 0, 0 };
		check("three bytes: no wheel to read", 0, r, 3, 1, "", 0);
	}
	{
		const unsigned char r[] = { 1, 5 };
		check("too short: nothing, buttons kept", 3, r, 2, 1, "", 3);
	}
	{
		const unsigned char r[] = { 0, 0, 0, 0, 9, 9, 9, 9 };
		check("bytes past the fourth are not read", 0, r, 8, 1, "", 0);
	}

	printf("\n%d check(s), %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
