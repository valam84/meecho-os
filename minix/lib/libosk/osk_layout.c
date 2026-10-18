/*
 * The layers of the on-screen keyboard (<minix/libosk.h>).
 *
 * Every layer has the same top row of terminal keys - Esc, Tab, Ctrl, Alt
 * and the arrows - and the same bottom row: a layer key, ',', space, '.'
 * and Enter.  Between them: the letters, or digits and symbols.  Every
 * printable ASCII character is on some layer (the host test checks).
 *
 * Rows are OSK_UNITS = 20 units across; a letter key is 2 wide, so a row
 * of ten fills it, and the middle letter row is set in by one unit.
 */
#include <stddef.h>
#include <minix/libosk.h>

#define N(a)	(sizeof(a) / sizeof((a)[0]))

/* A key sending usage; one with Shift; one never with it. */
#define K(r, x, w, u, l)	{ r, x, w, OSK_A_KEY, u, 0, l, NULL }
#define KS(r, x, w, u, l)	{ r, x, w, OSK_A_KEY, u, OSK_K_SHIFT, l, NULL }
#define KP(r, x, w, u, l)	{ r, x, w, OSK_A_KEY, u, OSK_K_PLAIN, l, NULL }
#define KG(r, x, w, u, l, g)	{ r, x, w, OSK_A_KEY, u, 0, l, g }
#define MOD(r, x, w, m, l)	{ r, x, w, OSK_A_MOD, m, 0, l, NULL }
#define LAYER(r, x, w, n, l)	{ r, x, w, OSK_A_LAYER, n, 0, l, NULL }

#define TERMINAL_ROW \
	K(0, 0, 3, OSK_U_ESC, "Esc"), K(0, 3, 3, OSK_U_TAB, "Tab"), \
	MOD(0, 6, 3, OSK_CTRL, "Ctrl"), MOD(0, 9, 3, OSK_ALT, "Alt"), \
	KG(0, 12, 2, OSK_U_LEFT, "<", "\xe2\x86\x90"), \
	KG(0, 14, 2, OSK_U_DOWN, "v", "\xe2\x86\x93"), \
	KG(0, 16, 2, OSK_U_UP, "^", "\xe2\x86\x91"), \
	KG(0, 18, 2, OSK_U_RIGHT, ">", "\xe2\x86\x92")

#define BOTTOM_ROW(layer, label) \
	LAYER(4, 0, 3, layer, label), KP(4, 3, 2, 0x36, ","), \
	K(4, 5, 10, OSK_U_SPACE, ""), KP(4, 15, 2, 0x37, "."), \
	KG(4, 17, 3, OSK_U_ENTER, "Enter", "\xe2\x8f\x8e")

#define BKSP	KG(3, 17, 3, OSK_U_BKSP, "Bksp", "\xe2\x8c\xab")

/* The letters; on OSK_UPPER every key but ',' and '.' goes with Shift. */
#define LETTERS(q,w,e,r,t,y,u,i,o,p, a,s,d,f,g,h,j,k,l, z,x,c,v,b,n,m) \
	K(1, 0, 2, 0x14, q), K(1, 2, 2, 0x1a, w), K(1, 4, 2, 0x08, e), \
	K(1, 6, 2, 0x15, r), K(1, 8, 2, 0x17, t), K(1, 10, 2, 0x1c, y), \
	K(1, 12, 2, 0x18, u), K(1, 14, 2, 0x0c, i), K(1, 16, 2, 0x12, o), \
	K(1, 18, 2, 0x13, p), \
	K(2, 1, 2, 0x04, a), K(2, 3, 2, 0x16, s), K(2, 5, 2, 0x07, d), \
	K(2, 7, 2, 0x09, f), K(2, 9, 2, 0x0a, g), K(2, 11, 2, 0x0b, h), \
	K(2, 13, 2, 0x0d, j), K(2, 15, 2, 0x0e, k), K(2, 17, 2, 0x0f, l), \
	MOD(3, 0, 3, OSK_SHIFT, "Shift"), \
	K(3, 3, 2, 0x1d, z), K(3, 5, 2, 0x1b, x), K(3, 7, 2, 0x06, c), \
	K(3, 9, 2, 0x19, v), K(3, 11, 2, 0x05, b), K(3, 13, 2, 0x11, n), \
	K(3, 15, 2, 0x10, m), BKSP

static const struct osk_key lower[] = {
	TERMINAL_ROW,
	LETTERS("q","w","e","r","t","y","u","i","o","p",
	    "a","s","d","f","g","h","j","k","l",
	    "z","x","c","v","b","n","m"),
	BOTTOM_ROW(OSK_DIGITS, "123"),
};

static const struct osk_key upper[] = {
	TERMINAL_ROW,
	LETTERS("Q","W","E","R","T","Y","U","I","O","P",
	    "A","S","D","F","G","H","J","K","L",
	    "Z","X","C","V","B","N","M"),
	BOTTOM_ROW(OSK_DIGITS, "123"),
};

/* " ' : ; ! ? / - the same third row on both symbol layers. */
#define PUNCT_ROW(layer, label) \
	LAYER(3, 0, 3, layer, label), \
	KS(3, 3, 2, 0x34, "\""), K(3, 5, 2, 0x34, "'"), \
	KS(3, 7, 2, 0x33, ":"), K(3, 9, 2, 0x33, ";"), \
	KS(3, 11, 2, 0x1e, "!"), KS(3, 13, 2, 0x38, "?"), \
	K(3, 15, 2, 0x38, "/"), BKSP

static const struct osk_key digits[] = {
	TERMINAL_ROW,
	K(1, 0, 2, 0x1e, "1"), K(1, 2, 2, 0x1f, "2"), K(1, 4, 2, 0x20, "3"),
	K(1, 6, 2, 0x21, "4"), K(1, 8, 2, 0x22, "5"), K(1, 10, 2, 0x23, "6"),
	K(1, 12, 2, 0x24, "7"), K(1, 14, 2, 0x25, "8"), K(1, 16, 2, 0x26, "9"),
	K(1, 18, 2, 0x27, "0"),
	KS(2, 0, 2, 0x1f, "@"), KS(2, 2, 2, 0x20, "#"), KS(2, 4, 2, 0x21, "$"),
	KS(2, 6, 2, 0x22, "%"), KS(2, 8, 2, 0x24, "&"), K(2, 10, 2, 0x2d, "-"),
	KS(2, 12, 2, 0x2d, "_"), KS(2, 14, 2, 0x2e, "+"),
	KS(2, 16, 2, 0x26, "("), KS(2, 18, 2, 0x27, ")"),
	PUNCT_ROW(OSK_SYMBOLS, "=\\<"),
	BOTTOM_ROW(OSK_LOWER, "abc"),
};

static const struct osk_key symbols[] = {
	TERMINAL_ROW,
	K(1, 0, 2, 0x35, "`"), KS(1, 2, 2, 0x35, "~"), K(1, 4, 2, 0x31, "\\"),
	KS(1, 6, 2, 0x31, "|"), KS(1, 8, 2, 0x23, "^"), KS(1, 10, 2, 0x25, "*"),
	K(1, 12, 2, 0x2e, "="), KS(1, 14, 2, 0x36, "<"),
	KS(1, 16, 2, 0x37, ">"), KS(1, 18, 2, 0x2f, "{"),
	KS(2, 0, 2, 0x30, "}"), K(2, 2, 2, 0x2f, "["), K(2, 4, 2, 0x30, "]"),
	KS(2, 6, 2, 0x1f, "@"), KS(2, 8, 2, 0x20, "#"), KS(2, 10, 2, 0x21, "$"),
	KS(2, 12, 2, 0x22, "%"), KS(2, 14, 2, 0x24, "&"),
	KS(2, 16, 2, 0x2e, "+"), K(2, 18, 2, 0x2d, "-"),
	PUNCT_ROW(OSK_DIGITS, "123"),
	BOTTOM_ROW(OSK_LOWER, "abc"),
};

const struct osk_layer osk_layers[OSK_NLAYERS] = {
	[OSK_LOWER]	= { lower, N(lower) },
	[OSK_UPPER]	= { upper, N(upper) },
	[OSK_DIGITS]	= { digits, N(digits) },
	[OSK_SYMBOLS]	= { symbols, N(symbols) },
};
