/*
 * The on-screen keyboard's picture, as the console's is drawn (fb's
 * fbcon.c, osk_key_draw()): Buffyboard's look.
 *
 * Every key looks the same - a rounded box with a thin frame, a gap round
 * it, its corners smoothed - and only its state changes its colours (face,
 * frame, label, from MEECHO's palette, on ink_deep):
 *
 *   a key              surface      dim      text
 *   a modifier armed   accent_deep  accent   bright
 *   ... locked         accent       pale     ink_deep
 *   the key held down  muted        pale     ink_deep
 *
 * The proportions are the console's, from the row's height rh: half the
 * gap rh/22 + 1, the corner's radius rh/18 + 1, the frame rh/70 + 1.
 * Labels are Inter (external/ofl) at the largest size whose em is no more
 * than 0.48 of the row, smaller if the label does not fit, set so that
 * capitals stand in the middle of the key (Inter's cap height: 0.727 em).
 * Shift, Backspace, Enter and the arrows are icons, chosen by what the key
 * does rather than by a glyph a font may not have: the shapes are fb's
 * (mkoskfont.py's ICONS), polygons in a unit box drawn 0.78 em square.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pixman.h>
#include <fcft/fcft.h>
#include <minix/libosk.h>

#include "osk.h"
#include "style.h"

/*===========================================================================*
 *    looks                                                                  *
 *===========================================================================*/
enum { LOOK_KEY, LOOK_ARMED, LOOK_LOCKED, LOOK_PRESSED };

static const struct {
	uint32_t face, frame, label;
} looks[] = {
	[LOOK_KEY]	= { STYLE_SURFACE, STYLE_DIM, STYLE_TEXT },
	[LOOK_ARMED]	= { STYLE_ACCENT_DEEP, STYLE_ACCENT, STYLE_BRIGHT },
	[LOOK_LOCKED]	= { STYLE_ACCENT, STYLE_PALE, STYLE_INK_DEEP },
	[LOOK_PRESSED]	= { STYLE_MUTED, STYLE_PALE, STYLE_INK_DEEP },
};

static int
look_of(const struct osk_view *v, unsigned i, const struct osk_key *k)
{
	if ((int)i == v->pressed)
		return LOOK_PRESSED;
	if (k->act == OSK_A_MOD && k->usage < OSK_NMODS) {
		if (v->mod[k->usage] == OSK_LOCKED)
			return LOOK_LOCKED;
		if (v->mod[k->usage] == OSK_ARMED)
			return LOOK_ARMED;
	}
	return LOOK_KEY;
}

/* a over b, n sixteenths of a. */
static uint32_t
mix(uint32_t a, uint32_t b, unsigned n)
{
	uint32_t r = 0;
	unsigned sh;

	for (sh = 0; sh < 24; sh += 8)
		r |= (((a >> sh & 0xff) * n + (b >> sh & 0xff) * (16 - n) + 8) /
		    16) << sh;
	return r;
}

/*===========================================================================*
 *    the box                                                                *
 *===========================================================================*/
/*
 * How much of pixel (x, y) the rounded rectangle [x0, x1) x [y0, y1) with
 * corner radius r covers, in sixteenths - fbcon.c's rr_cover(): straight
 * edges on pixel boundaries, corners measured from their centres.
 */
static unsigned
rr_cover(int x, int y, int x0, int y0, int x1, int y1, int r)
{
	int cx, cy, dx, dy, d;

	if (x < x0 || x >= x1 || y < y0 || y >= y1)
		return 0;
	cx = x < x0 + r ? x0 + r : x >= x1 - r ? x1 - r : -1;
	cy = y < y0 + r ? y0 + r : y >= y1 - r ? y1 - r : -1;
	if (cx < 0 || cy < 0 || r <= 0)
		return 16;
	dx = (x * 16 + 8) - cx * 16;
	dy = (y * 16 + 8) - cy * 16;
	d = (int)sqrt((double)(dx * dx + dy * dy));
	d = r * 16 + 8 - d;
	return d <= 0 ? 0 : d >= 16 ? 16 : (unsigned)d;
}

/*===========================================================================*
 *    icons                                                                  *
 *===========================================================================*/
enum { IC_UP, IC_DOWN, IC_LEFT, IC_RIGHT, IC_SHIFT, IC_BKSP, IC_ENTER, NICONS };

#define MAXPTS	64
#define MAXCONT	2

struct contour {
	int n;
	double x[MAXPTS], y[MAXPTS];
};

struct icon {
	int ncont;
	struct contour c[MAXCONT];
};

static struct icon icons[NICONS];
static int icons_made;

static void
pt(struct contour *c, double x, double y)
{
	c->x[c->n] = x;
	c->y[c->n] = y;
	c->n++;
}

/* Pointing up, stroke t (mkoskfont.py's chevron()); turned a quarter at a
 * time, clockwise, for the others. */
static void
chevron(struct contour *c, double t, int quarters)
{
	static const double base[6][2] = {
		{ 0.5, 0.22 }, { 0.92, 0.64 }, { 0, 0 }, { 0.5, 0 }, { 0, 0 },
		{ 0.08, 0.64 },
	};
	double p[6][2], x, y;
	int i, q;

	memcpy(p, base, sizeof(p));
	p[2][0] = 0.92 - t;	p[2][1] = 0.64 + t * 0.75;
	p[3][1] = 0.22 + t * 1.45;
	p[4][0] = 0.08 + t;	p[4][1] = 0.64 + t * 0.75;
	c->n = 0;
	for (i = 0; i < 6; i++) {
		x = p[i][0];
		y = p[i][1];
		for (q = 0; q < quarters; q++) {
			double nx = 1 - y;

			y = x;
			x = nx;
		}
		pt(c, x, y);
	}
}

/* A list of points, backwards if rev: a hole winds the other way. */
static void
poly(struct contour *c, const double (*p)[2], int n, int rev, double dx,
	double dy)
{
	int i;

	c->n = 0;
	for (i = 0; i < n; i++) {
		int j = rev ? n - 1 - i : i;

		pt(c, p[j][0] + dx, p[j][1] + dy);
	}
}

static void
make_icons(void)
{
	static const double arrow[7][2] = {
		{ 0.5, 0.18 }, { 0.80, 0.52 }, { 0.63, 0.52 }, { 0.63, 0.80 },
		{ 0.37, 0.80 }, { 0.37, 0.52 }, { 0.20, 0.52 },
	};
	static const double tag[5][2] = {
		{ 0.0, 0.5 }, { 0.28, 0.16 }, { 1.0, 0.16 }, { 1.0, 0.84 },
		{ 0.28, 0.84 },
	};
	static const double cross[12][2] = {
		{ -0.17, -0.22 }, { 0.0, -0.06 }, { 0.17, -0.22 },
		{ 0.22, -0.17 }, { 0.06, 0.0 }, { 0.22, 0.17 }, { 0.17, 0.22 },
		{ 0.0, 0.06 }, { -0.17, 0.22 }, { -0.22, 0.17 }, { -0.06, 0.0 },
		{ -0.22, -0.17 },
	};
	static const double tick[6][2] = {
		{ 0.04, 0.52 }, { 0.16, 0.40 }, { 0.38, 0.62 }, { 0.84, 0.16 },
		{ 0.96, 0.28 }, { 0.38, 0.86 },
	};
	struct contour *c;
	int i;

	if (icons_made)
		return;
	icons_made = 1;
	icons[IC_UP].ncont = 1;
	chevron(&icons[IC_UP].c[0], 0.2, 0);
	icons[IC_RIGHT].ncont = 1;
	chevron(&icons[IC_RIGHT].c[0], 0.2, 1);
	icons[IC_DOWN].ncont = 1;
	chevron(&icons[IC_DOWN].c[0], 0.2, 2);
	icons[IC_LEFT].ncont = 1;
	chevron(&icons[IC_LEFT].c[0], 0.2, 3);
	/* A disc with an arrow cut out of it. */
	icons[IC_SHIFT].ncont = 2;
	c = &icons[IC_SHIFT].c[0];
	c->n = 0;
	for (i = 0; i < 48; i++)
		pt(c, 0.5 + 0.48 * cos(2 * M_PI * i / 48),
		    0.5 + 0.48 * sin(2 * M_PI * i / 48));
	poly(&icons[IC_SHIFT].c[1], arrow, 7, 1, 0, 0);
	/* A tag pointing left with a cross cut out of it. */
	icons[IC_BKSP].ncont = 2;
	poly(&icons[IC_BKSP].c[0], tag, 5, 0, 0, 0);
	poly(&icons[IC_BKSP].c[1], cross, 12, 1, 0.62, 0.5);
	icons[IC_ENTER].ncont = 1;
	poly(&icons[IC_ENTER].c[0], tick, 6, 0, 0, 0);
}

/* The winding number of icon ic round (x, y), the unit box's. */
static int
winding(const struct icon *ic, double x, double y)
{
	const struct contour *c;
	double x0, y0, x1, y1;
	int k, i, w = 0;

	for (k = 0; k < ic->ncont; k++) {
		c = &ic->c[k];
		for (i = 0; i < c->n; i++) {
			x0 = c->x[i];
			y0 = c->y[i];
			x1 = c->x[(i + 1) % c->n];
			y1 = c->y[(i + 1) % c->n];
			if (y0 <= y && y1 > y &&
			    (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) > 0)
				w++;
			else if (y0 > y && y1 <= y &&
			    (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) < 0)
				w--;
		}
	}
	return w;
}

/* Icon ic, box pixels square, as coverage in sixteenths: made once a size. */
static const uint8_t *
icon_mask(int ic, int box)
{
	static uint8_t *mask[NICONS];
	static int mask_box[NICONS];
	int x, y, sx, sy, n;
	uint8_t *m;

	if (mask[ic] != NULL && mask_box[ic] == box)
		return mask[ic];
	make_icons();
	free(mask[ic]);
	if ((m = mask[ic] = malloc((size_t)box * box)) == NULL)
		return NULL;
	mask_box[ic] = box;
	for (y = 0; y < box; y++)
		for (x = 0; x < box; x++) {
			n = 0;
			for (sy = 0; sy < 4; sy++)
				for (sx = 0; sx < 4; sx++)
					n += winding(&icons[ic],
					    (x + (sx + 0.5) / 4) / box,
					    (y + (sy + 0.5) / 4) / box) != 0;
			m[y * box + x] = (uint8_t)n;
		}
	return m;
}

/* The icon a key shows instead of its label, by what it does; or -1. */
static int
icon_of(const struct osk_key *k)
{
	if (k->act == OSK_A_MOD)
		return k->usage == OSK_SHIFT ? IC_SHIFT : -1;
	if (k->act != OSK_A_KEY)
		return -1;
	switch (k->usage) {
	case OSK_U_UP:		return IC_UP;
	case OSK_U_DOWN:	return IC_DOWN;
	case OSK_U_LEFT:	return IC_LEFT;
	case OSK_U_RIGHT:	return IC_RIGHT;
	case OSK_U_BKSP:	return IC_BKSP;
	case OSK_U_ENTER:	return IC_ENTER;
	}
	return -1;
}

/*===========================================================================*
 *    labels                                                                 *
 *===========================================================================*/
#define NFONTS	8

static struct {
	int px;
	struct fcft_font *f;
} fonts[NFONTS];
static int fcft_up;

/* Inter at px pixels to the em; kept, a few sizes. */
static struct fcft_font *
font_px(int px)
{
	char name[64];
	const char *names[1] = { name };
	int i, slot = 0;

	for (i = 0; i < NFONTS; i++)
		if (fonts[i].f != NULL && fonts[i].px == px)
			return fonts[i].f;
	if (!fcft_up) {
		fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
		fcft_up = 1;
	}
	for (i = 0; i < NFONTS; i++)
		if (fonts[i].f == NULL) {
			slot = i;
			break;
		}
	if (i == NFONTS) {		/* all taken: the first goes */
		fcft_destroy(fonts[0].f);
		slot = 0;
	}
	snprintf(name, sizeof(name), "Inter:pixelsize=%d", px);
	fonts[slot].f = fcft_from_name(1, names, NULL);
	fonts[slot].px = px;
	return fonts[slot].f;
}

static int
text_width(struct fcft_font *f, const char *s)
{
	const struct fcft_glyph *g;
	int w = 0;

	for (; *s != '\0'; s++)
		if ((g = fcft_rasterize_char_utf32(f, (unsigned char)*s,
		    FCFT_SUBPIXEL_NONE)) != NULL)
			w += g->advance.x;
	return w;
}

/* s with its pen at x and its baseline at base, label over face. */
static void
text(pixman_image_t *dst, struct fcft_font *f, int x, int base,
	const char *s, uint32_t label)
{
	const struct fcft_glyph *g;
	pixman_color_t c;
	pixman_image_t *src;

	c.alpha = 0xffff;
	c.red = (uint16_t)((label >> 16 & 0xff) * 257);
	c.green = (uint16_t)((label >> 8 & 0xff) * 257);
	c.blue = (uint16_t)((label & 0xff) * 257);
	src = pixman_image_create_solid_fill(&c);
	for (; *s != '\0'; s++) {
		if ((g = fcft_rasterize_char_utf32(f, (unsigned char)*s,
		    FCFT_SUBPIXEL_NONE)) == NULL)
			continue;
		if (g->width > 0)
			pixman_image_composite32(PIXMAN_OP_OVER,
			    pixman_image_get_format(g->pix) == PIXMAN_a8r8g8b8 ?
			    g->pix : src, pixman_image_get_format(g->pix) ==
			    PIXMAN_a8r8g8b8 ? NULL : g->pix, dst, 0, 0, 0, 0,
			    x + g->x, base - g->y, g->width, g->height);
		x += g->advance.x;
	}
	pixman_image_unref(src);
}

/*===========================================================================*
 *    the keyboard                                                           *
 *===========================================================================*/
static void
key_draw(pixman_image_t *dst, uint32_t *px, int W, int H, int stride,
	const struct osk_key *k, int look)
{
	unsigned x0, y0, x1, y1;
	int rh = H / OSK_ROWS, gap = rh / 22 + 1, r = rh / 18 + 1,
	    bw = rh / 70 + 1, x, y, ix0, iy0, ix1, iy1, em, cap, box, icon, w;
	uint32_t face = looks[look].face, frame = looks[look].frame,
	    label = looks[look].label, *row;
	struct fcft_font *f;
	const uint8_t *m;

	osk_rect(k, (unsigned)W, (unsigned)H, &x0, &y0, &x1, &y1);
	if ((int)(x1 - x0) <= 2 * gap + 2 * r ||
	    (int)(y1 - y0) <= 2 * gap + 2 * r)
		return;
	ix0 = (int)x0 + gap;
	iy0 = (int)y0 + gap;
	ix1 = (int)x1 - gap;
	iy1 = (int)y1 - gap;
	for (y = (int)y0; y < (int)y1; y++) {
		row = (uint32_t *)((char *)px + (size_t)y * stride);
		for (x = (int)x0; x < (int)x1; x++)
			row[x] = 0xff000000 | mix(mix(face, frame,
			    rr_cover(x, y, ix0 + bw, iy0 + bw, ix1 - bw,
			    iy1 - bw, r - bw)), STYLE_INK_DEEP,
			    rr_cover(x, y, ix0, iy0, ix1, iy1, r));
	}

	/* The largest em no more than 0.48 of the row. */
	em = rh * 48 / 100;
	if (em < 6)
		em = 6;
	if ((icon = icon_of(k)) >= 0) {
		box = (em * 78 + 50) / 100;
		if ((m = icon_mask(icon, box)) == NULL)
			return;
		x0 = (unsigned)((ix0 + ix1 - box) / 2);
		y0 = (unsigned)((iy0 + iy1 - box) / 2);
		for (y = 0; y < box; y++) {
			row = (uint32_t *)((char *)px + (size_t)(y0 + y) *
			    stride);
			for (x = 0; x < box; x++)
				if (m[y * box + x] != 0)
					row[x0 + x] = 0xff000000 | mix(label,
					    row[x0 + x] & 0xffffff,
					    m[y * box + x]);
		}
		return;
	}
	if (k->label[0] == '\0')
		return;
	/* Smaller while the label does not fit. */
	for (;;) {
		if ((f = font_px(em)) == NULL)
			return;
		w = text_width(f, k->label);
		if (em <= 6 || w + 2 * r + 4 * gap <= ix1 - ix0)
			break;
		em--;
	}
	cap = em * 727 / 1000;			/* Inter's cap height */
	text(dst, f, (ix0 + ix1 - w) / 2, (iy0 + iy1 + cap) / 2, k->label,
	    label);
}

void
kb_draw(uint32_t *px, int w, int h, int stride, const struct osk_view *v)
{
	const struct osk_layer *l = &osk_layers[v->layer];
	pixman_image_t *dst;
	pixman_color_t c;
	pixman_box32_t all = { 0, 0, w, h };
	unsigned i;

	dst = pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h, px, stride);
	c.alpha = 0xffff;
	c.red = (uint16_t)((STYLE_INK_DEEP >> 16 & 0xff) * 257);
	c.green = (uint16_t)((STYLE_INK_DEEP >> 8 & 0xff) * 257);
	c.blue = (uint16_t)((STYLE_INK_DEEP & 0xff) * 257);
	pixman_image_fill_boxes(PIXMAN_OP_SRC, dst, &c, 1, &all);
	for (i = 0; i < l->nkeys; i++)
		key_draw(dst, px, w, h, stride, &l->keys[i],
		    look_of(v, i, &l->keys[i]));
	pixman_image_unref(dst);
}
