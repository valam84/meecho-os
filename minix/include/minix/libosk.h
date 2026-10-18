/*
 * libosk: the on-screen keyboard - its layers, what a touch on it does, and
 * where its keys are.
 *
 * One keyboard for every side that shows or works one: the frame buffer's
 * console draws it, a touch screen driver feeds it touches, the desktop's
 * client (meechowm-osk) does both.  The library makes no MINIX calls and
 * keeps no globals but the tables, so a service, a driver, a Wayland client
 * and the host test (port/test/osk/) link the same code.
 *
 * Its model is Buffyboard's (postmarketOS, now Nura): layers of lower case,
 * upper case, digits and symbols with a row of terminal keys on top of
 * each, modifiers that stick and show it, a pressed key that shows.  The
 * code and the layouts are MEECHO's own - Buffyboard and squeekboard, whose
 * layouts it takes, are GPL-3.
 *
 * Keys go out as HID usages on the keyboard page, for a US keymap - the
 * way a USB keyboard's do - so nothing after the keyboard's owner knows
 * the keyboard is drawn.  A key that needs Shift (a capital, '!') says so,
 * and the owner presses Shift around it.
 *
 * Modifiers (Shift, Ctrl, Alt): a tap arms one for the next key, a second
 * tap locks it, a third lets it go.  Shift on the letters shows the upper
 * case layer; armed, it goes back after one key.  Modifier and layer keys
 * act when touched; any other key goes down when touched and up when that
 * touch ends, wherever it ends, so a finger that slides off does not leave
 * a key down.
 *
 * Geometry: OSK_ROWS rows of equal height, each OSK_UNITS units across; a
 * key is a row, a first unit and a width in units.  Coordinates are pixels
 * of the keyboard's own rectangle, w by h; where the rectangle is on the
 * screen is the caller's.
 */
#ifndef _MINIX_LIBOSK_H
#define _MINIX_LIBOSK_H

#include <stdint.h>

#define OSK_ROWS	5
#define OSK_UNITS	20

/* The console's keyboard: the bottom OSK_CONSOLE_PERMILLE of the screen. */
#define OSK_CONSOLE_PERMILLE	300

/* Layers. */
#define OSK_LOWER	0
#define OSK_UPPER	1	/* shown, never chosen: OSK_LOWER with Shift */
#define OSK_DIGITS	2
#define OSK_SYMBOLS	3
#define OSK_NLAYERS	4

/* Modifiers, and what state each is in. */
#define OSK_SHIFT	0
#define OSK_CTRL	1
#define OSK_ALT		2
#define OSK_NMODS	3

#define OSK_OFF		0
#define OSK_ARMED	1	/* for the next key */
#define OSK_LOCKED	2	/* until tapped again */

/* What a key does. */
#define OSK_A_KEY	0	/* send usage */
#define OSK_A_MOD	1	/* usage is OSK_SHIFT, OSK_CTRL or OSK_ALT */
#define OSK_A_LAYER	2	/* usage is the layer to show */

/* Key flags (OSK_A_KEY). */
#define OSK_K_SHIFT	0x01	/* always with Shift: '!' */
#define OSK_K_PLAIN	0x02	/* never with Shift, even on OSK_UPPER: ',' */

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
#define OSK_U_LALT	0xe2

struct osk_key {
	uint8_t		row, x, w;	/* row, first unit, width in units */
	uint8_t		act;		/* OSK_A_ */
	uint8_t		usage;		/* HID usage, modifier or layer */
	uint8_t		flags;		/* OSK_K_ */
	const char	*label;		/* ASCII: any font draws it */
	const char	*glyph;		/* UTF-8 if nicer than label, or NULL */
};

struct osk_layer {
	const struct osk_key	*keys;
	unsigned		nkeys;
};

extern const struct osk_layer osk_layers[OSK_NLAYERS];

/* A touch, as a touch screen reports it. */
#define OSK_TOUCH_DOWN		0
#define OSK_TOUCH_UP		1
#define OSK_TOUCH_CONTACT	2	/* still down, somewhere */

#define OSK_MAX_TOUCH	10

/* The keyboard's owner: it takes touches and sends keys. */
struct osk {
	unsigned	w, h;		/* the keyboard's rectangle, pixels */
	void		(*emit)(void *arg, unsigned usage, int down);
	void		*arg;

	unsigned	base;		/* OSK_LOWER, OSK_DIGITS, OSK_SYMBOLS */
	uint8_t		mod[OSK_NMODS];	/* OSK_OFF, OSK_ARMED, OSK_LOCKED */
	struct {
		int			active;
		const struct osk_key	*key;	/* NULL: on no key */
		unsigned		seq;	/* when it went down */
		uint8_t			shift, ctrl, alt; /* held with it */
	} t[OSK_MAX_TOUCH];
	unsigned	seq;
};

/* What a side that only draws needs to know (osk_view(), osk_unword()). */
struct osk_view {
	unsigned	layer;		/* the layer shown */
	uint8_t		mod[OSK_NMODS];
	int		pressed;	/* a key of layer held down, or -1 */
};

void osk_init(struct osk *o, unsigned w, unsigned h,
	void (*emit)(void *arg, unsigned usage, int down), void *arg);
/* The screen turned: same state, new size. */
void osk_resize(struct osk *o, unsigned w, unsigned h);

/*
 * A touch: id the controller's number for it, x and y in the keyboard's
 * rectangle (outside it - a touch on no key).  Returns nonzero if what is
 * shown changed.
 */
int osk_touch(struct osk *o, unsigned id, int event, unsigned x, unsigned y);
/* A touch the controller stopped reporting without an up. */
int osk_lost(struct osk *o, unsigned id);
/* Let every key and modifier go: the keyboard is hidden. */
int osk_reset(struct osk *o);

unsigned osk_shown(const struct osk *o);
void osk_view(const struct osk *o, struct osk_view *v);

/* The view in one word - what the owner publishes to a side that draws. */
uint32_t osk_word(const struct osk *o);
void osk_unword(uint32_t word, struct osk_view *v);

/* The pixel rectangle of k on a w by h keyboard: [x0, x1) x [y0, y1). */
void osk_rect(const struct osk_key *k, unsigned w, unsigned h,
	unsigned *x0, unsigned *y0, unsigned *x1, unsigned *y1);
/* The key of layer at (x, y), or NULL. */
const struct osk_key *osk_hit(unsigned layer, unsigned x, unsigned y,
	unsigned w, unsigned h);

/* The console's keyboard on a screen h pixels high: its height, whole rows. */
unsigned osk_console_height(unsigned h);

#endif /* _MINIX_LIBOSK_H */
