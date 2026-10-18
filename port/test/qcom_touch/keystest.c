/*
 * Host test for minix/drivers/hid/qcom_touch/keys.c over libosk: touches
 * on the Mi Pad 4's 1200x1920 screen as the console keyboard's keys - where
 * the keyboard is, when it takes touches, what it publishes.  The keyboard's
 * own rules (layers, modifiers) are port/test/osk/'s; here only that they
 * arrive through the screen's coordinates.
 */
#include <stdio.h>
#include <string.h>

#include <minix/libosk.h>
#include "keys.h"

#define W	1200
#define H	1920

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

/* The centre, on the screen, of the key labelled so on the layer shown. */
static void
centre(const struct keys *k, const char *label, unsigned *x, unsigned *y)
{
	const struct osk_layer *l = &osk_layers[osk_shown(&k->osk)];
	unsigned i, x0, y0, x1, y1;

	for (i = 0; i < l->nkeys; i++)
		if (strcmp(l->keys[i].label, label) == 0) {
			osk_rect(&l->keys[i], k->osk.w, k->osk.h, &x0, &y0,
			    &x1, &y1);
			*x = (x0 + x1) / 2;
			*y = k->top + (y0 + y1) / 2;
			return;
		}
	fails++;
	printf("FAIL: no key \"%s\"\n", label);
	*x = *y = 0;
}

/* A tap; how many of its three steps changed what is shown. */
static int
tap(struct keys *k, unsigned id, const char *label)
{
	unsigned x, y;
	int n = 0;

	centre(k, label, &x, &y);
	n += keys_touch(k, id, TOUCH_DOWN, x, y) != 0;
	n += keys_touch(k, id, TOUCH_CONTACT, x, y) != 0;
	n += keys_touch(k, id, TOUCH_UP, x, y) != 0;
	return n;
}

int
main(void)
{
	struct keys k;
	struct osk_view v;
	unsigned x, y;
	int n;

	keys_init(&k, W, H, emit);

	/* Where it is: the bottom of the screen, whole rows, as fb draws. */
	CHECK(k.osk.w == W && k.osk.h == osk_console_height(H) &&
	    k.top == H - k.osk.h, "rectangle %ux%u at %u", k.osk.w, k.osk.h,
	    k.top);
	CHECK(k.osk.h == 575 && k.osk.h % OSK_ROWS == 0, "height %u",
	    k.osk.h);

	/* Until fb says it is drawn, a touch is no key. */
	log_[0] = 0;
	n = tap(&k, 0, "a");
	CHECK(log_[0] == 0 && n == 0, "hidden: %s (%d)", log_, n);
	CHECK(keys_show(&k, 1) == 0, "showing changes nothing drawn");
	CHECK(keys_show(&k, 1) == 0, "showing twice");

	/* "a": press, release; down and up both change what is shown. */
	log_[0] = 0;
	n = tap(&k, 0, "a");
	CHECK(strcmp(log_, "+04 -04 ") == 0 && n == 2, "a: %s (%d)", log_,
	    n);

	/* Above the keyboard - its last text line, and higher - nothing. */
	log_[0] = 0;
	CHECK(keys_touch(&k, 0, TOUCH_DOWN, 600, k.top - 1) == 0, "edge");
	(void)keys_touch(&k, 0, TOUCH_UP, 600, k.top - 1);
	(void)keys_touch(&k, 0, TOUCH_DOWN, 600, 100);
	(void)keys_touch(&k, 0, TOUCH_UP, 600, 100);
	CHECK(log_[0] == 0, "text area: %s", log_);

	/* A touch that starts on the text and slides onto a key: nothing. */
	centre(&k, "q", &x, &y);
	(void)keys_touch(&k, 0, TOUCH_DOWN, x, 100);
	(void)keys_touch(&k, 0, TOUCH_CONTACT, x, y);
	(void)keys_touch(&k, 0, TOUCH_UP, x, y);
	CHECK(log_[0] == 0, "slide on: %s", log_);

	/* Its first line is the keyboard's: Esc there. */
	log_[0] = 0;
	(void)keys_touch(&k, 0, TOUCH_DOWN, 10, k.top);
	(void)keys_touch(&k, 0, TOUCH_UP, 10, k.top);
	CHECK(strcmp(log_, "+29 -29 ") == 0, "top line: %s", log_);

	/* The screen's last line too: the layer key, which sends nothing. */
	log_[0] = 0;
	(void)keys_touch(&k, 0, TOUCH_DOWN, 10, H - 1);
	(void)keys_touch(&k, 0, TOUCH_UP, 10, H - 1);
	osk_unword(keys_word(&k), &v);
	CHECK(log_[0] == 0 && v.layer == OSK_DIGITS, "bottom line: %s, "
	    "layer %u", log_, v.layer);
	(void)tap(&k, 0, "abc");
	osk_unword(keys_word(&k), &v);
	CHECK(v.layer == OSK_LOWER, "back to letters: %u", v.layer);

	/* Shift through the screen: the upper case shown, then gone. */
	log_[0] = 0;
	CHECK(tap(&k, 0, "Shift") >= 1, "Shift changes nothing shown");
	osk_unword(keys_word(&k), &v);
	CHECK(v.layer == OSK_UPPER && v.mod[OSK_SHIFT] == OSK_ARMED,
	    "Shift: layer %u, %u", v.layer, v.mod[OSK_SHIFT]);
	(void)tap(&k, 0, "A");
	CHECK(strcmp(log_, "+e1 +04 -04 -e1 ") == 0, "A: %s", log_);
	osk_unword(keys_word(&k), &v);
	CHECK(v.layer == OSK_LOWER && v.mod[OSK_SHIFT] == OSK_OFF,
	    "after A: layer %u", v.layer);

	/*
	 * Hidden with things held - a locked Ctrl, a key down: all let go,
	 * the change reported, and touches are no keys until shown again.
	 */
	log_[0] = 0;
	(void)tap(&k, 0, "Ctrl");
	(void)tap(&k, 0, "Ctrl");
	centre(&k, "x", &x, &y);
	(void)keys_touch(&k, 3, TOUCH_DOWN, x, y);
	osk_unword(keys_word(&k), &v);
	CHECK(v.mod[OSK_CTRL] == OSK_LOCKED && v.pressed >= 0,
	    "held: ctrl %u, pressed %d", v.mod[OSK_CTRL], v.pressed);
	CHECK(keys_show(&k, 0) != 0, "hiding changes nothing shown");
	CHECK(strcmp(log_, "+e0 +1b -1b -e0 ") == 0, "hide: %s", log_);
	osk_unword(keys_word(&k), &v);
	CHECK(v.mod[OSK_CTRL] == OSK_OFF && v.pressed == -1,
	    "hidden: ctrl %u, pressed %d", v.mod[OSK_CTRL], v.pressed);
	CHECK(keys_touch(&k, 3, TOUCH_UP, x, y) == 0 &&
	    strcmp(log_, "+e0 +1b -1b -e0 ") == 0, "up after hide: %s", log_);
	CHECK(keys_show(&k, 0) == 0, "hiding twice");
	n = tap(&k, 0, "a");
	CHECK(n == 0 && strcmp(log_, "+e0 +1b -1b -e0 ") == 0,
	    "hidden tap: %s", log_);
	(void)keys_show(&k, 1);

	/* A lost touch lets its key go; one never down changes nothing. */
	log_[0] = 0;
	centre(&k, "Enter", &x, &y);
	(void)keys_touch(&k, 2, TOUCH_CONTACT, x, y);	/* the down missed */
	CHECK(keys_lost(&k, 2) != 0, "lost changes nothing shown");
	CHECK(keys_lost(&k, 4) == 0, "lost, never down");
	CHECK(strcmp(log_, "+28 -28 ") == 0, "lost: %s", log_);

	/* Two fingers through the screen: both down, each up its own. */
	log_[0] = 0;
	centre(&k, "e", &x, &y);
	(void)keys_touch(&k, 0, TOUCH_DOWN, x, y);
	centre(&k, "r", &x, &y);
	(void)keys_touch(&k, 1, TOUCH_DOWN, x, y);
	(void)keys_touch(&k, 0, TOUCH_UP, 0, 0);
	(void)keys_touch(&k, 1, TOUCH_UP, 0, 0);
	CHECK(strcmp(log_, "+08 +15 -08 -15 ") == 0, "two: %s", log_);

	/* An id out of range is ignored. */
	log_[0] = 0;
	CHECK(keys_touch(&k, OSK_MAX_TOUCH, TOUCH_DOWN, x, y) == 0 &&
	    log_[0] == 0, "id out of range");

	printf("%s: %d failure%s\n", fails ? "FAIL" : "ok", fails,
	    fails == 1 ? "" : "s");
	return fails != 0;
}
