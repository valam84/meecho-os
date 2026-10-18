/*
 * The layers of the on-screen keyboard (<minix/libosk.h>), laid out as
 * Buffyboard's terminal layout looks.
 *
 * Every layer has the same top row - Ctrl, Tab and the four arrows - and
 * Enter and the terminal key (">_") at the bottom right.  Between them:
 * the letters, the digits and symbols, or the terminal's keys (Esc, Alt,
 * Home, End, the pages, F1-F10 and the shell's punctuation).  Tab is on
 * top rather than Buffyboard's Alt: a shell completes with it all the
 * time, and Alt is for the programs that want it, behind ">_".  The letter
 * layers keep only letters, Shift, Backspace and space, as Buffyboard's
 * do; ',' and '.' are on the digit layers, next to the space.  Every
 * printable ASCII character is on some layer (the host test checks).
 *
 * Rows are OSK_UNITS = 60 units across, so that both six keys and ten
 * divide a row: a letter key is 6 wide, a key of the top row 10, and the
 * middle letter row is set in by half a key.
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
#define MODG(r, x, w, m, l, g)	{ r, x, w, OSK_A_MOD, m, 0, l, g }
#define LAYER(r, x, w, n, l)	{ r, x, w, OSK_A_LAYER, n, 0, l, NULL }

/* The glyphs a side that can draw them shows instead of the label. */
#define G_UP	"\xe2\x86\x91"		/* U+2191 */
#define G_DOWN	"\xe2\x86\x93"		/* U+2193 */
#define G_LEFT	"\xe2\x86\x90"		/* U+2190 */
#define G_RIGHT	"\xe2\x86\x92"		/* U+2192 */
#define G_SHIFT	"\xe2\x87\xa7"		/* U+21E7 upwards white arrow */
#define G_BKSP	"\xe2\x8c\xab"		/* U+232B erase to the left */
#define G_ENTER	"\xe2\x9c\x93"		/* U+2713 check mark */

#define TOP_ROW \
	MOD(0, 0, 10, OSK_CTRL, "Ctrl"), K(0, 10, 10, OSK_U_TAB, "Tab"), \
	KG(0, 20, 10, OSK_U_UP, "^", G_UP), \
	KG(0, 30, 10, OSK_U_DOWN, "v", G_DOWN), \
	KG(0, 40, 10, OSK_U_LEFT, "<", G_LEFT), \
	KG(0, 50, 10, OSK_U_RIGHT, ">", G_RIGHT)

#define ENTER	KG(4, 48, 12, OSK_U_ENTER, "Enter", G_ENTER)
#define BKSP	KG(3, 51, 9, OSK_U_BKSP, "Bksp", G_BKSP)
#define TERM	LAYER(4, 36, 12, OSK_TERM, ">_")

/* Letters and the terminal: a layer key, space, ">_", Enter. */
#define WIDE_BOTTOM(layer, label) \
	LAYER(4, 0, 12, layer, label), K(4, 12, 24, OSK_U_SPACE, ""), \
	TERM, ENTER

/* Digits and symbols: ',' and '.' either side of the space. */
#define PUNCT_BOTTOM \
	LAYER(4, 0, 9, OSK_LOWER, "abc"), KP(4, 9, 6, 0x36, ","), \
	K(4, 15, 18, OSK_U_SPACE, ""), KP(4, 33, 6, 0x37, "."), \
	LAYER(4, 39, 9, OSK_TERM, ">_"), ENTER

/* The letters; on OSK_UPPER every key goes with Shift. */
#define LETTERS(q,w,e,r,t,y,u,i,o,p, a,s,d,f,g,h,j,k,l, z,x,c,v,b,n,m) \
	K(1, 0, 6, 0x14, q), K(1, 6, 6, 0x1a, w), K(1, 12, 6, 0x08, e), \
	K(1, 18, 6, 0x15, r), K(1, 24, 6, 0x17, t), K(1, 30, 6, 0x1c, y), \
	K(1, 36, 6, 0x18, u), K(1, 42, 6, 0x0c, i), K(1, 48, 6, 0x12, o), \
	K(1, 54, 6, 0x13, p), \
	K(2, 3, 6, 0x04, a), K(2, 9, 6, 0x16, s), K(2, 15, 6, 0x07, d), \
	K(2, 21, 6, 0x09, f), K(2, 27, 6, 0x0a, g), K(2, 33, 6, 0x0b, h), \
	K(2, 39, 6, 0x0d, j), K(2, 45, 6, 0x0e, k), K(2, 51, 6, 0x0f, l), \
	MODG(3, 0, 9, OSK_SHIFT, "Shift", G_SHIFT), \
	K(3, 9, 6, 0x1d, z), K(3, 15, 6, 0x1b, x), K(3, 21, 6, 0x06, c), \
	K(3, 27, 6, 0x19, v), K(3, 33, 6, 0x05, b), K(3, 39, 6, 0x11, n), \
	K(3, 45, 6, 0x10, m), BKSP

static const struct osk_key lower[] = {
	TOP_ROW,
	LETTERS("q","w","e","r","t","y","u","i","o","p",
	    "a","s","d","f","g","h","j","k","l",
	    "z","x","c","v","b","n","m"),
	WIDE_BOTTOM(OSK_DIGITS, "123"),
};

static const struct osk_key upper[] = {
	TOP_ROW,
	LETTERS("Q","W","E","R","T","Y","U","I","O","P",
	    "A","S","D","F","G","H","J","K","L",
	    "Z","X","C","V","B","N","M"),
	WIDE_BOTTOM(OSK_DIGITS, "123"),
};

/* " ' : ; ! ? / - the same third row on both symbol layers. */
#define PUNCT_ROW(layer, label) \
	LAYER(3, 0, 9, layer, label), \
	KS(3, 9, 6, 0x34, "\""), K(3, 15, 6, 0x34, "'"), \
	KS(3, 21, 6, 0x33, ":"), K(3, 27, 6, 0x33, ";"), \
	KS(3, 33, 6, 0x1e, "!"), KS(3, 39, 6, 0x38, "?"), \
	K(3, 45, 6, 0x38, "/"), BKSP

static const struct osk_key digits[] = {
	TOP_ROW,
	K(1, 0, 6, 0x1e, "1"), K(1, 6, 6, 0x1f, "2"), K(1, 12, 6, 0x20, "3"),
	K(1, 18, 6, 0x21, "4"), K(1, 24, 6, 0x22, "5"), K(1, 30, 6, 0x23, "6"),
	K(1, 36, 6, 0x24, "7"), K(1, 42, 6, 0x25, "8"), K(1, 48, 6, 0x26, "9"),
	K(1, 54, 6, 0x27, "0"),
	KS(2, 0, 6, 0x1f, "@"), KS(2, 6, 6, 0x20, "#"), KS(2, 12, 6, 0x21, "$"),
	KS(2, 18, 6, 0x22, "%"), KS(2, 24, 6, 0x24, "&"), K(2, 30, 6, 0x2d, "-"),
	KS(2, 36, 6, 0x2d, "_"), KS(2, 42, 6, 0x2e, "+"),
	KS(2, 48, 6, 0x26, "("), KS(2, 54, 6, 0x27, ")"),
	PUNCT_ROW(OSK_SYMBOLS, "=\\<"),
	PUNCT_BOTTOM,
};

static const struct osk_key symbols[] = {
	TOP_ROW,
	K(1, 0, 6, 0x35, "`"), KS(1, 6, 6, 0x35, "~"), K(1, 12, 6, 0x31, "\\"),
	KS(1, 18, 6, 0x31, "|"), KS(1, 24, 6, 0x23, "^"), KS(1, 30, 6, 0x25, "*"),
	K(1, 36, 6, 0x2e, "="), KS(1, 42, 6, 0x36, "<"),
	KS(1, 48, 6, 0x37, ">"), KS(1, 54, 6, 0x2f, "{"),
	KS(2, 0, 6, 0x30, "}"), K(2, 6, 6, 0x2f, "["), K(2, 12, 6, 0x30, "]"),
	KS(2, 18, 6, 0x1f, "@"), KS(2, 24, 6, 0x20, "#"), KS(2, 30, 6, 0x21, "$"),
	KS(2, 36, 6, 0x22, "%"), KS(2, 42, 6, 0x24, "&"),
	KS(2, 48, 6, 0x2e, "+"), K(2, 54, 6, 0x2d, "-"),
	PUNCT_ROW(OSK_DIGITS, "123"),
	PUNCT_BOTTOM,
};

/* The terminal's keys: what a shell and a full-screen program want. */
static const struct osk_key term[] = {
	TOP_ROW,
	K(1, 0, 10, OSK_U_ESC, "Esc"), MOD(1, 10, 10, OSK_ALT, "Alt"),
	K(1, 20, 10, 0x4a, "Home"), K(1, 30, 10, 0x4d, "End"),
	K(1, 40, 10, 0x4b, "PgUp"), K(1, 50, 10, 0x4e, "PgDn"),
	K(2, 0, 6, 0x3a, "F1"), K(2, 6, 6, 0x3b, "F2"), K(2, 12, 6, 0x3c, "F3"),
	K(2, 18, 6, 0x3d, "F4"), K(2, 24, 6, 0x3e, "F5"),
	K(2, 30, 6, 0x3f, "F6"), K(2, 36, 6, 0x40, "F7"),
	K(2, 42, 6, 0x41, "F8"), K(2, 48, 6, 0x42, "F9"),
	K(2, 54, 6, 0x43, "F10"),
	K(3, 0, 9, 0x4c, "Del"),
	KS(3, 9, 6, 0x31, "|"), KS(3, 15, 6, 0x35, "~"), K(3, 21, 6, 0x38, "/"),
	K(3, 27, 6, 0x2d, "-"), KS(3, 33, 6, 0x2d, "_"), KS(3, 39, 6, 0x25, "*"),
	KS(3, 45, 6, 0x24, "&"), BKSP,
	WIDE_BOTTOM(OSK_LOWER, "abc"),
};

const struct osk_layer osk_layers[OSK_NLAYERS] = {
	[OSK_LOWER]	= { lower, N(lower) },
	[OSK_UPPER]	= { upper, N(upper) },
	[OSK_DIGITS]	= { digits, N(digits) },
	[OSK_SYMBOLS]	= { symbols, N(symbols) },
	[OSK_TERM]	= { term, N(term) },
};
