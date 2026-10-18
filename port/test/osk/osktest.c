/*
 * Host test for libosk (minix/lib/libosk, <minix/libosk.h>): the layers,
 * what touches on them send, what a side that draws is told, and where
 * the keys are - on the Mi Pad 4's console keyboard (1200 wide, 30% of
 * 1920 high) and on a small one.
 *
 *   sh port/test/osk/run.sh [-m]
 */
#include <stdio.h>
#include <string.h>

#include <minix/libosk.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", \
    __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define W	1200
#define H	576

static char log_[1024];

static void
emit(void *arg, unsigned usage, int down)
{
	char t[16];

	(void)arg;
	snprintf(t, sizeof(t), "%c%02x ", down ? '+' : '-', usage);
	strncat(log_, t, sizeof(log_) - strlen(log_) - 1);
}

/* The first key of layer with this label. */
static const struct osk_key *
find(unsigned layer, const char *label)
{
	const struct osk_layer *l = &osk_layers[layer];
	unsigned i;

	for (i = 0; i < l->nkeys; i++)
		if (strcmp(l->keys[i].label, label) == 0)
			return &l->keys[i];
	return NULL;
}

static void
centre(const struct osk_key *k, unsigned *x, unsigned *y)
{
	unsigned x0, y0, x1, y1;

	osk_rect(k, W, H, &x0, &y0, &x1, &y1);
	*x = (x0 + x1) / 2;
	*y = (y0 + y1) / 2;
}

/* Touch down and up on the key labelled so on the layer shown now. */
static void
tap(struct osk *o, unsigned id, const char *label)
{
	const struct osk_key *k = find(osk_shown(o), label);
	unsigned x, y;

	if (k == NULL) {
		CHECK(0, "no key \"%s\" on layer %u", label, osk_shown(o));
		return;
	}
	centre(k, &x, &y);
	osk_touch(o, id, OSK_TOUCH_DOWN, x, y);
	osk_touch(o, id, OSK_TOUCH_CONTACT, x, y);
	osk_touch(o, id, OSK_TOUCH_UP, x, y);
}

static void
down(struct osk *o, unsigned id, const char *label)
{
	const struct osk_key *k = find(osk_shown(o), label);
	unsigned x, y;

	if (k == NULL) {
		CHECK(0, "no key \"%s\" on layer %u", label, osk_shown(o));
		return;
	}
	centre(k, &x, &y);
	osk_touch(o, id, OSK_TOUCH_DOWN, x, y);
}

static void
up(struct osk *o, unsigned id)
{
	osk_touch(o, id, OSK_TOUCH_UP, 0, 0);
}

#define EXPECT(want) do { CHECK(strcmp(log_, want) == 0, \
    "sent \"%s\", want \"%s\"", log_, want); log_[0] = '\0'; } while (0)

/* What a US keymap makes of a usage, with Shift or without; 0: nothing. */
static int
us_char(unsigned u, int shift)
{
	static const char digits[] = "1234567890", sdigits[] = "!@#$%^&*()";
	static const struct { unsigned u; char c, s; } p[] = {
		{ 0x2c, ' ', ' ' }, { 0x2d, '-', '_' }, { 0x2e, '=', '+' },
		{ 0x2f, '[', '{' }, { 0x30, ']', '}' }, { 0x31, '\\', '|' },
		{ 0x33, ';', ':' }, { 0x34, '\'', '"' }, { 0x35, '`', '~' },
		{ 0x36, ',', '<' }, { 0x37, '.', '>' }, { 0x38, '/', '?' },
	};
	unsigned i;

	if (u >= 0x04 && u <= 0x1d)
		return (shift ? 'A' : 'a') + (int)(u - 0x04);
	if (u >= 0x1e && u <= 0x27)
		return shift ? sdigits[u - 0x1e] : digits[u - 0x1e];
	for (i = 0; i < sizeof(p) / sizeof(p[0]); i++)
		if (p[i].u == u)
			return shift ? p[i].s : p[i].c;
	return 0;
}

/* The tables: in bounds, no overlap, labels say what the keys type. */
static void
test_tables(void)
{
	char reach[128];
	unsigned layer, i, j, c;

	memset(reach, 0, sizeof(reach));
	for (layer = 0; layer < OSK_NLAYERS; layer++) {
		const struct osk_layer *l = &osk_layers[layer];
		unsigned char cover[OSK_ROWS][OSK_UNITS];

		CHECK(l->nkeys > 0 && l->nkeys < 255, "layer %u: %u keys",
		    layer, l->nkeys);
		memset(cover, 0, sizeof(cover));
		for (i = 0; i < l->nkeys; i++) {
			const struct osk_key *k = &l->keys[i];
			int shift, ch;

			CHECK(k->row < OSK_ROWS && k->w > 0 &&
			    k->x + k->w <= OSK_UNITS, "layer %u key %u \"%s\" "
			    "out of bounds", layer, i, k->label);
			if (k->row >= OSK_ROWS || k->x + k->w > OSK_UNITS)
				continue;
			for (j = k->x; j < (unsigned)(k->x + k->w); j++) {
				CHECK(!cover[k->row][j], "layer %u key \"%s\" "
				    "overlaps", layer, k->label);
				cover[k->row][j] = 1;
			}
			if (k->act != OSK_A_KEY)
				continue;
			shift = (k->flags & OSK_K_SHIFT) ||
			    (layer == OSK_UPPER && !(k->flags & OSK_K_PLAIN));
			ch = us_char(k->usage, shift);
			if (ch > ' ') {
				CHECK(strlen(k->label) == 1 &&
				    k->label[0] == ch, "layer %u: \"%s\" types "
				    "'%c'", layer, k->label, ch);
				reach[ch] = 1;
			}
		}
		/* Every layer: terminal keys on top, Enter at the bottom. */
		CHECK(find(layer, "Esc") && find(layer, "Ctrl") &&
		    find(layer, "Alt") && find(layer, "Enter") &&
		    find(layer, "Bksp"), "layer %u lacks a fixed key", layer);
	}
	for (c = '!'; c <= '~'; c++)
		CHECK(reach[c], "'%c' is on no layer", c);
	/* The letter layers match key for key. */
	CHECK(osk_layers[OSK_LOWER].nkeys == osk_layers[OSK_UPPER].nkeys,
	    "lower and upper differ");
	for (i = 0; i < osk_layers[OSK_LOWER].nkeys; i++) {
		const struct osk_key *a = &osk_layers[OSK_LOWER].keys[i];
		const struct osk_key *b = &osk_layers[OSK_UPPER].keys[i];

		CHECK(a->row == b->row && a->x == b->x && a->w == b->w &&
		    a->usage == b->usage && a->act == b->act,
		    "lower/upper key %u differ", i);
	}
}

/* Geometry: every key found at its centre and its edges, none off it. */
static void
test_geometry(unsigned w, unsigned h)
{
	unsigned layer, i, x0, y0, x1, y1;

	for (layer = 0; layer < OSK_NLAYERS; layer++) {
		const struct osk_layer *l = &osk_layers[layer];

		for (i = 0; i < l->nkeys; i++) {
			const struct osk_key *k = &l->keys[i];

			osk_rect(k, w, h, &x0, &y0, &x1, &y1);
			CHECK(x0 < x1 && y0 < y1 && x1 <= w && y1 <= h,
			    "%ux%u: \"%s\" rect %u,%u-%u,%u", w, h, k->label,
			    x0, y0, x1, y1);
			CHECK(osk_hit(layer, (x0 + x1) / 2, (y0 + y1) / 2,
			    w, h) == k, "%ux%u: centre of \"%s\"", w, h,
			    k->label);
			CHECK(osk_hit(layer, x0, y0, w, h) == k &&
			    osk_hit(layer, x1 - 1, y1 - 1, w, h) == k,
			    "%ux%u: corners of \"%s\"", w, h, k->label);
			if (x1 < w)
				CHECK(osk_hit(layer, x1, y0, w, h) != k,
				    "%ux%u: \"%s\" reaches past x1", w, h,
				    k->label);
			if (y1 < h)
				CHECK(osk_hit(layer, x0, y1, w, h) != k,
				    "%ux%u: \"%s\" reaches past y1", w, h,
				    k->label);
		}
		CHECK(osk_hit(layer, w, 0, w, h) == NULL &&
		    osk_hit(layer, 0, h, w, h) == NULL, "%ux%u: hit outside",
		    w, h);
	}
	CHECK(osk_console_height(1920) == 575, "console height %u",
	    osk_console_height(1920));
	CHECK(osk_console_height(1920) % OSK_ROWS == 0, "not whole rows");
}

static void
test_keys(void)
{
	struct osk o;
	struct osk_view v;
	uint32_t w;

	log_[0] = '\0';
	osk_init(&o, W, H, emit, NULL);

	tap(&o, 0, "a");
	EXPECT("+04 -04 ");

	/* Shift armed: one capital, back to lower case. */
	tap(&o, 0, "Shift");
	EXPECT("");
	CHECK(osk_shown(&o) == OSK_UPPER, "Shift does not show upper");
	tap(&o, 0, "A");
	EXPECT("+e1 +04 -04 -e1 ");
	CHECK(osk_shown(&o) == OSK_LOWER, "armed Shift stays");
	tap(&o, 0, "b");
	EXPECT("+05 -05 ");

	/* Shift locked: capitals until it is tapped a third time. */
	tap(&o, 0, "Shift");
	tap(&o, 0, "Shift");
	CHECK(o.mod[OSK_SHIFT] == OSK_LOCKED, "two taps do not lock");
	tap(&o, 0, "A");
	tap(&o, 0, "B");
	EXPECT("+e1 +04 -04 -e1 +e1 +05 -05 -e1 ");
	/* ',' and '.' are not '<' and '>' on the upper layer. */
	tap(&o, 0, ",");
	EXPECT("+36 -36 ");
	/* Shift-Tab, for a shell's completion backwards. */
	tap(&o, 0, "Tab");
	EXPECT("+e1 +2b -2b -e1 ");
	tap(&o, 0, "Shift");
	CHECK(o.mod[OSK_SHIFT] == OSK_OFF && osk_shown(&o) == OSK_LOWER,
	    "third tap does not let Shift go");
	tap(&o, 0, "a");
	EXPECT("+04 -04 ");

	/* Ctrl-c, then Ctrl is gone. */
	tap(&o, 0, "Ctrl");
	tap(&o, 0, "c");
	EXPECT("+e0 +06 -06 -e0 ");
	tap(&o, 0, "c");
	EXPECT("+06 -06 ");

	/* Ctrl locked, Alt and Shift armed: all three, up the other way. */
	tap(&o, 0, "Ctrl");
	tap(&o, 0, "Ctrl");
	tap(&o, 0, "Alt");
	tap(&o, 0, "Shift");
	osk_view(&o, &v);
	CHECK(v.mod[OSK_CTRL] == OSK_LOCKED && v.mod[OSK_ALT] == OSK_ARMED &&
	    v.mod[OSK_SHIFT] == OSK_ARMED, "view of modifiers %u %u %u",
	    v.mod[OSK_SHIFT], v.mod[OSK_CTRL], v.mod[OSK_ALT]);
	tap(&o, 0, "X");
	EXPECT("+e0 +e2 +e1 +1b -1b -e1 -e2 -e0 ");
	tap(&o, 0, "x");
	EXPECT("+e0 +1b -1b -e0 ");
	tap(&o, 0, "Ctrl");
	tap(&o, 0, "x");
	EXPECT("+1b -1b ");

	/* Digits and symbols, with Shift where the character needs it. */
	tap(&o, 0, "123");
	CHECK(osk_shown(&o) == OSK_DIGITS, "123 does not show digits");
	tap(&o, 0, "1");
	tap(&o, 0, "!");
	EXPECT("+1e -1e +e1 +1e -1e -e1 ");
	tap(&o, 0, "=\\<");
	CHECK(osk_shown(&o) == OSK_SYMBOLS, "no symbols");
	tap(&o, 0, "|");
	tap(&o, 0, "\\");
	EXPECT("+e1 +31 -31 -e1 +31 -31 ");
	tap(&o, 0, "abc");
	CHECK(osk_shown(&o) == OSK_LOWER, "abc does not show letters");

	/* An armed Shift is for a letter: leaving the letters drops it. */
	tap(&o, 0, "Shift");
	tap(&o, 0, "123");
	tap(&o, 0, "abc");
	CHECK(osk_shown(&o) == OSK_LOWER && o.mod[OSK_SHIFT] == OSK_OFF,
	    "armed Shift survives a layer");
	/* A locked one stays for the way back. */
	tap(&o, 0, "Shift");
	tap(&o, 0, "Shift");
	tap(&o, 0, "123");
	tap(&o, 0, "1");
	EXPECT("+1e -1e ");
	tap(&o, 0, "abc");
	CHECK(osk_shown(&o) == OSK_UPPER, "locked Shift lost on a layer");
	tap(&o, 0, "Shift");
	log_[0] = '\0';

	/* A finger that slides off lets its key go. */
	down(&o, 0, "a");
	osk_touch(&o, 0, OSK_TOUCH_CONTACT, W - 1, H - 1);
	osk_touch(&o, 0, OSK_TOUCH_UP, W - 1, H - 1);
	EXPECT("+04 -04 ");

	/* Two fingers, let go out of order; the pressed key is the last. */
	down(&o, 1, "a");
	down(&o, 2, "s");
	osk_view(&o, &v);
	CHECK(v.pressed >= 0 && strcmp(osk_layers[v.layer].keys[v.pressed]
	    .label, "s") == 0, "pressed is not s");
	up(&o, 2);
	osk_view(&o, &v);
	CHECK(v.pressed >= 0 && strcmp(osk_layers[v.layer].keys[v.pressed]
	    .label, "a") == 0, "pressed is not a after s");
	up(&o, 1);
	EXPECT("+04 +16 -16 -04 ");
	osk_view(&o, &v);
	CHECK(v.pressed == -1, "a key shows pressed with none held");

	/* Shift held down shows pressed on the layer it brought. */
	down(&o, 0, "Shift");
	osk_view(&o, &v);
	CHECK(v.layer == OSK_UPPER && v.pressed >= 0 &&
	    strcmp(osk_layers[v.layer].keys[v.pressed].label, "Shift") == 0,
	    "held Shift does not show");
	up(&o, 0);
	tap(&o, 0, "Shift");
	tap(&o, 0, "Shift");
	log_[0] = '\0';

	/* A down without an up, a contact without a down, a lost touch. */
	down(&o, 3, "q");
	down(&o, 3, "w");
	EXPECT("+14 -14 +1a ");
	osk_lost(&o, 3);
	EXPECT("-1a ");
	{
		unsigned x, y;

		centre(find(OSK_LOWER, "e"), &x, &y);
		osk_touch(&o, 4, OSK_TOUCH_CONTACT, x, y);
		osk_touch(&o, 4, OSK_TOUCH_UP, x, y);
		EXPECT("+08 -08 ");
	}

	/* Off the keyboard, and a touch number out of range: nothing. */
	osk_touch(&o, 0, OSK_TOUCH_DOWN, W, 0);
	osk_touch(&o, 0, OSK_TOUCH_UP, W, 0);
	CHECK(osk_touch(&o, OSK_MAX_TOUCH, OSK_TOUCH_DOWN, 1, 1) == 0,
	    "touch out of range changed something");
	EXPECT("");

	/* What changes the view says so; what does not, does not. */
	{
		const struct osk_key *k = find(OSK_LOWER, "a");
		unsigned x, y;

		centre(k, &x, &y);
		CHECK(osk_touch(&o, 0, OSK_TOUCH_DOWN, x, y) != 0,
		    "a press changed nothing");
		CHECK(osk_touch(&o, 0, OSK_TOUCH_CONTACT, x, y) == 0,
		    "a contact changed something");
		CHECK(osk_touch(&o, 0, OSK_TOUCH_UP, x, y) != 0,
		    "a release changed nothing");
	}
	log_[0] = '\0';

	/* Reset lets everything go. */
	tap(&o, 0, "Ctrl");
	tap(&o, 0, "Ctrl");
	down(&o, 5, "z");
	CHECK(osk_reset(&o) != 0, "reset changed nothing");
	EXPECT("+e0 +1d -1d -e0 ");
	w = osk_word(&o);
	osk_unword(w, &v);
	CHECK(v.mod[OSK_CTRL] == OSK_OFF && v.pressed == -1,
	    "reset left state");
}

/* Word and view say the same, for every state there is. */
static void
test_word(void)
{
	struct osk o;
	struct osk_view a, b;
	unsigned base, s, c, al, p, i;
	static const unsigned bases[] = { OSK_LOWER, OSK_DIGITS, OSK_SYMBOLS };

	osk_init(&o, W, H, emit, NULL);
	for (base = 0; base < 3; base++)
	for (s = 0; s < 3; s++) for (c = 0; c < 3; c++) for (al = 0; al < 3;
	    al++) for (p = 0; p < 3; p++) {
		o.base = bases[base];
		o.mod[OSK_SHIFT] = s;
		o.mod[OSK_CTRL] = c;
		o.mod[OSK_ALT] = al;
		for (i = 0; i < OSK_MAX_TOUCH; i++)
			o.t[i].active = 0;
		if (p > 0) {
			unsigned layer = osk_shown(&o);
			unsigned n = osk_layers[layer].nkeys;

			o.t[0].active = 1;
			o.t[0].key = &osk_layers[layer].keys[p == 1 ? 0 : n - 1];
			o.t[0].seq = 1;
		}
		osk_view(&o, &a);
		osk_unword(osk_word(&o), &b);
		CHECK(a.layer == b.layer && a.pressed == b.pressed &&
		    memcmp(a.mod, b.mod, sizeof(a.mod)) == 0,
		    "word of %u/%u%u%u/%u: layer %u/%u pressed %d/%d mods "
		    "%u%u%u/%u%u%u", base, s, c, al, p, a.layer, b.layer,
		    a.pressed, b.pressed, a.mod[0], a.mod[1], a.mod[2],
		    b.mod[0], b.mod[1], b.mod[2]);
	}
	/* A word naming no key or no layer is not believed. */
	osk_unword(0xff0007u, &b);
	CHECK(b.layer < OSK_NLAYERS && b.pressed == -1, "bad word believed");
}

int
main(void)
{
	test_tables();
	test_geometry(W, H);
	test_geometry(320, 100);
	test_geometry(1920, 324);
	test_keys();
	test_word();
	if (fails == 0)
		printf("osktest: ok\n");
	else
		printf("osktest: %d failure(s)\n", fails);
	return fails != 0;
}
