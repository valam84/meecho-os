/*
 * Host test for minix/drivers/hid/qcom_touch/keys.c: touches on the
 * on-screen keyboard of <minix/osk.h> as key events, on the Mi Pad 4's
 * 1200x1920 screen.
 */
#include <stdio.h>
#include <string.h>

#include <minix/osk.h>
#include "keys.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", \
    __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char log_[512];

static void
emit(unsigned usage, int down)
{
	char t[16];

	snprintf(t, sizeof(t), "%c%02x ", down ? '+' : '-', usage);
	strncat(log_, t, sizeof(log_) - strlen(log_) - 1);
}

/* The centre of the key with this usage (the first one). */
static void
centre(unsigned usage, unsigned *x, unsigned *y)
{
	unsigned i, x0, y0, x1, y1;

	for (i = 0; i < OSK_NKEYS; i++)
		if (osk_keys[i].usage == usage) {
			osk_rect(&osk_keys[i], 1200, 1920, &x0, &y0, &x1, &y1);
			*x = (x0 + x1) / 2;
			*y = (y0 + y1) / 2;
			return;
		}
	*x = *y = 0;
}

static void
tap(struct keys *k, unsigned id, unsigned usage)
{
	unsigned x, y;

	centre(usage, &x, &y);
	keys_touch(k, id, TOUCH_DOWN, x, y);
	keys_touch(k, id, TOUCH_CONTACT, x, y);
	keys_touch(k, id, TOUCH_UP, x, y);
}

int
main(void)
{
	struct keys k;
	unsigned i, x0, y0, x1, y1, x, y;

	/* The geometry: whole rows, nothing outside the screen, no overlap. */
	CHECK(osk_top(1920) == 1920 - osk_row_height(1920) * OSK_ROWS,
	    "top");
	CHECK(osk_row_height(1920) == 115, "row %u", osk_row_height(1920));
	for (i = 0; i < OSK_NKEYS; i++) {
		osk_rect(&osk_keys[i], 1200, 1920, &x0, &y0, &x1, &y1);
		CHECK(x1 <= 1200 && y1 <= 1920 && x0 < x1 && y0 < y1,
		    "key %u off screen", i);
		CHECK(osk_hit((x0 + x1) / 2, (y0 + y1) / 2, 1200, 1920) ==
		    &osk_keys[i], "key %u not hit at its centre", i);
	}
	/* Every row adds up to the full width. */
	for (i = 0; i < OSK_ROWS; i++) {
		unsigned j, units = 0;

		for (j = 0; j < OSK_NKEYS; j++)
			if (osk_keys[j].row == i)
				units += osk_keys[j].w;
		CHECK(units == OSK_UNITS, "row %u has %u units", i, units);
	}

	keys_init(&k, 1200, 1920, emit);

	/* "a": press, release. */
	log_[0] = 0;
	tap(&k, 0, 0x04);
	CHECK(strcmp(log_, "+04 -04 ") == 0, "a: %s", log_);

	/* Shift, then "a": shift wraps a, and is gone after. */
	log_[0] = 0;
	tap(&k, 0, OSK_U_LSHIFT);
	tap(&k, 0, 0x04);
	tap(&k, 0, 0x04);
	CHECK(strcmp(log_, "+e1 +04 -04 -e1 +04 -04 ") == 0, "A: %s", log_);

	/* Ctrl+Shift+c, then disarmed Shift. */
	log_[0] = 0;
	tap(&k, 0, OSK_U_LCTRL);
	tap(&k, 0, OSK_U_LSHIFT);
	tap(&k, 0, 0x06);
	tap(&k, 0, OSK_U_LSHIFT);
	tap(&k, 0, OSK_U_LSHIFT);
	tap(&k, 0, 0x06);
	CHECK(strcmp(log_, "+e0 +e1 +06 -06 -e1 -e0 +06 -06 ") == 0,
	    "^C: %s", log_);

	/* Above the keyboard: nothing. */
	log_[0] = 0;
	keys_touch(&k, 0, TOUCH_DOWN, 600, 100);
	keys_touch(&k, 0, TOUCH_UP, 600, 100);
	CHECK(log_[0] == 0, "text area: %s", log_);

	/* A slide off the key still releases it; a lost touch too. */
	log_[0] = 0;
	centre(OSK_U_ENTER, &x, &y);
	keys_touch(&k, 1, TOUCH_DOWN, x, y);
	keys_touch(&k, 1, TOUCH_CONTACT, 10, 10);
	keys_touch(&k, 1, TOUCH_UP, 10, 10);
	centre(OSK_U_SPACE, &x, &y);
	keys_touch(&k, 2, TOUCH_CONTACT, x, y);	/* down missed */
	keys_lost(&k, 2);
	CHECK(strcmp(log_, "+28 -28 +2c -2c ") == 0, "slide/lost: %s", log_);

	/* Two fingers: both keys down at once, released in their order. */
	log_[0] = 0;
	centre(0x1e, &x, &y);
	keys_touch(&k, 0, TOUCH_DOWN, x, y);
	centre(0x1f, &x, &y);
	keys_touch(&k, 1, TOUCH_DOWN, x, y);
	keys_touch(&k, 0, TOUCH_UP, 0, 0);
	keys_touch(&k, 1, TOUCH_UP, 0, 0);
	CHECK(strcmp(log_, "+1e +1f -1e -1f ") == 0, "two: %s", log_);

	/* An id out of range is ignored. */
	log_[0] = 0;
	keys_touch(&k, KEYS_MAX_TOUCH, TOUCH_DOWN, x, y);
	CHECK(log_[0] == 0, "id out of range");

	printf("%s: %d failure%s\n", fails ? "FAIL" : "ok", fails,
	    fails == 1 ? "" : "s");
	return fails != 0;
}
