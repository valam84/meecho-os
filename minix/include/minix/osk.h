/*
 * The on-screen keyboard: one layout, read by the two sides that need it.
 *
 * The frame buffer driver draws it along the bottom of the console (fb,
 * osk=1) and a touch screen driver turns a touch inside it into a key
 * (qcom_touch).  Neither tells the other anything: both compute the same
 * rectangles from this table and the screen's size, which both take from
 * the device tree.  A key reaches tty the way a USB keyboard's does - a HID
 * usage on the keyboard page, through the input server - so nothing after
 * the touch driver knows the keyboard is drawn.
 *
 * Geometry: the keyboard is the bottom OSK_PERMILLE thousandths of the
 * screen, OSK_ROWS rows of equal height, each row OSK_UNITS units across.
 * A key is a row, a first unit and a width in units.
 */
#ifndef _MINIX_OSK_H
#define _MINIX_OSK_H

#include <stdint.h>

#define OSK_PERMILLE	300
#define OSK_ROWS	5
#define OSK_UNITS	15

/* HID usages, keyboard page (HID Usage Tables 1.12, chapter 10). */
#define OSK_U_ENTER	0x28
#define OSK_U_ESC	0x29
#define OSK_U_BKSP	0x2a
#define OSK_U_TAB	0x2b
#define OSK_U_SPACE	0x2c
#define OSK_U_RIGHT	0x4f
#define OSK_U_LEFT	0x50
#define OSK_U_DOWN	0x51
#define OSK_U_UP	0x52
#define OSK_U_LCTRL	0xe0
#define OSK_U_LSHIFT	0xe1

struct osk_key {
	uint8_t	row, x, w;	/* row, first unit, width in units */
	uint8_t	usage;		/* HID usage */
	char	label[6];	/* what is drawn on the key */
};

/*
 * A US layout, in the order the rows are drawn.  Shift and Ctrl are
 * one-shot: the touch driver holds them down for the next key only.
 */
static const struct osk_key osk_keys[] = {
	{ 0,  0, 1, 0x35, "`" }, { 0,  1, 1, 0x1e, "1" }, { 0,  2, 1, 0x1f, "2" },
	{ 0,  3, 1, 0x20, "3" }, { 0,  4, 1, 0x21, "4" }, { 0,  5, 1, 0x22, "5" },
	{ 0,  6, 1, 0x23, "6" }, { 0,  7, 1, 0x24, "7" }, { 0,  8, 1, 0x25, "8" },
	{ 0,  9, 1, 0x26, "9" }, { 0, 10, 1, 0x27, "0" }, { 0, 11, 1, 0x2d, "-" },
	{ 0, 12, 1, 0x2e, "=" }, { 0, 13, 2, OSK_U_BKSP, "Bksp" },

	{ 1,  0, 1, OSK_U_TAB, "Tab" },
	{ 1,  1, 1, 0x14, "q" }, { 1,  2, 1, 0x1a, "w" }, { 1,  3, 1, 0x08, "e" },
	{ 1,  4, 1, 0x15, "r" }, { 1,  5, 1, 0x17, "t" }, { 1,  6, 1, 0x1c, "y" },
	{ 1,  7, 1, 0x18, "u" }, { 1,  8, 1, 0x0c, "i" }, { 1,  9, 1, 0x12, "o" },
	{ 1, 10, 1, 0x13, "p" }, { 1, 11, 1, 0x2f, "[" }, { 1, 12, 1, 0x30, "]" },
	{ 1, 13, 2, 0x31, "\\" },

	{ 2,  0, 1, OSK_U_ESC, "Esc" },
	{ 2,  1, 1, 0x04, "a" }, { 2,  2, 1, 0x16, "s" }, { 2,  3, 1, 0x07, "d" },
	{ 2,  4, 1, 0x09, "f" }, { 2,  5, 1, 0x0a, "g" }, { 2,  6, 1, 0x0b, "h" },
	{ 2,  7, 1, 0x0d, "j" }, { 2,  8, 1, 0x0e, "k" }, { 2,  9, 1, 0x0f, "l" },
	{ 2, 10, 1, 0x33, ";" }, { 2, 11, 1, 0x34, "'" },
	{ 2, 12, 3, OSK_U_ENTER, "Enter" },

	{ 3,  0, 2, OSK_U_LSHIFT, "Shift" },
	{ 3,  2, 1, 0x1d, "z" }, { 3,  3, 1, 0x1b, "x" }, { 3,  4, 1, 0x06, "c" },
	{ 3,  5, 1, 0x19, "v" }, { 3,  6, 1, 0x05, "b" }, { 3,  7, 1, 0x11, "n" },
	{ 3,  8, 1, 0x10, "m" }, { 3,  9, 1, 0x36, "," }, { 3, 10, 1, 0x37, "." },
	{ 3, 11, 1, 0x38, "/" }, { 3, 12, 1, OSK_U_UP, "^" },
	{ 3, 13, 2, OSK_U_LSHIFT, "Shift" },

	{ 4,  0, 2, OSK_U_LCTRL, "Ctrl" },
	{ 4,  2, 10, OSK_U_SPACE, "" },
	{ 4, 12, 1, OSK_U_LEFT, "<" }, { 4, 13, 1, OSK_U_DOWN, "v" },
	{ 4, 14, 1, OSK_U_RIGHT, ">" },
};

#define OSK_NKEYS	(sizeof(osk_keys) / sizeof(osk_keys[0]))

/*
 * Where the keyboard starts on a screen h pixels high, and how high a row
 * is: whole pixels, the same on both sides.
 */
static inline unsigned
osk_row_height(unsigned h)
{
	return h * OSK_PERMILLE / 1000 / OSK_ROWS;
}

static inline unsigned
osk_top(unsigned h)
{
	return h - osk_row_height(h) * OSK_ROWS;
}

/* The pixel rectangle of key k on a w by h screen: [x0, x1) x [y0, y1). */
static inline void
osk_rect(const struct osk_key *k, unsigned w, unsigned h, unsigned *x0,
	unsigned *y0, unsigned *x1, unsigned *y1)
{
	unsigned rh = osk_row_height(h);

	*x0 = (unsigned)k->x * w / OSK_UNITS;
	*x1 = (unsigned)(k->x + k->w) * w / OSK_UNITS;
	*y0 = osk_top(h) + k->row * rh;
	*y1 = *y0 + rh;
}

/* The key at (x, y), or NULL. */
static inline const struct osk_key *
osk_hit(unsigned x, unsigned y, unsigned w, unsigned h)
{
	unsigned i, x0, y0, x1, y1;

	for (i = 0; i < OSK_NKEYS; i++) {
		osk_rect(&osk_keys[i], w, h, &x0, &y0, &x1, &y1);
		if (x >= x0 && x < x1 && y >= y0 && y < y1)
			return &osk_keys[i];
	}
	return NULL;
}

#endif /* _MINIX_OSK_H */
