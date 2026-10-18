/*
 * The on-screen keyboard's picture: libosk's keys of the layer shown, each
 * a box with its label in the middle, in MEECHO's colours.
 *
 * A key that types stands on surface; one that does something else -
 * Esc, Tab, the arrows, Enter, Backspace, the modifiers and the layer
 * keys, and space - on line, a step lighter.  A modifier armed for the next key is
 * accent_deep with its label in accent; locked, it is accent itself.  The
 * key held down is accent, as on Buffyboard: one can see what one hit.
 */
#include <stdio.h>
#include <string.h>

#include <pixman.h>
#include <fcft/fcft.h>
#include <minix/libosk.h>

#include "osk.h"
#include "style.h"

static struct fcft_font *font;
static int font_px;

static pixman_color_t
colour(uint32_t rgb)
{
	pixman_color_t c;

	c.alpha = 0xffff;
	c.red = (uint16_t)(((rgb >> 16) & 0xff) * 257);
	c.green = (uint16_t)(((rgb >> 8) & 0xff) * 257);
	c.blue = (uint16_t)((rgb & 0xff) * 257);
	return c;
}

static void
fill(pixman_image_t *dst, int x, int y, int w, int h, uint32_t rgb)
{
	pixman_color_t c = colour(rgb);
	pixman_box32_t b = { x, y, x + w, y + h };

	if (w > 0 && h > 0)
		pixman_image_fill_boxes(PIXMAN_OP_SRC, dst, &c, 1, &b);
}

/* The font, at a size for the row: re-made when the row changes. */
static void
font_for(int row)
{
	char name[64];
	const char *names[1] = { name };
	int px = row * 2 / 5 < 9 ? 9 : row * 2 / 5;

	if (font != NULL && font_px == px)
		return;
	if (font == NULL)
		fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	else
		fcft_destroy(font);
	snprintf(name, sizeof(name), "monospace:pixelsize=%d", px);
	font = fcft_from_name(1, names, NULL);
	font_px = px;
}

/* The next code point of a UTF-8 string; a broken byte is itself. */
static uint32_t
next_cp(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p;
	int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0, i;

	if (n > 0) {
		c &= 0x3f >> n;
		for (i = 1; i <= n; i++) {
			if ((p[i] & 0xc0) != 0x80) {
				*s += 1;
				return *p;
			}
			c = c << 6 | (p[i] & 0x3f);
		}
	}
	*s += n + 1;
	return c;
}

/* How wide s is in the font; -1 if the font cannot draw all of it. */
static int
measure(const char *s)
{
	const struct fcft_glyph *g;
	uint32_t cp;
	int w = 0;

	while (*s != '\0') {
		cp = next_cp(&s);
		if ((g = fcft_rasterize_char_utf32(font, cp,
		    FCFT_SUBPIXEL_NONE)) == NULL || (cp > 0x7f && g->width == 0))
			return -1;
		w += g->advance.x;
	}
	return w;
}

static void
text(pixman_image_t *dst, int x, int y, const char *s, uint32_t rgb)
{
	const struct fcft_glyph *g;
	pixman_color_t c = colour(rgb);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	while (*s != '\0') {
		g = fcft_rasterize_char_utf32(font, next_cp(&s),
		    FCFT_SUBPIXEL_NONE);
		if (g == NULL)
			continue;
		if (g->width > 0) {
			if (pixman_image_get_format(g->pix) == PIXMAN_a8r8g8b8)
				pixman_image_composite32(PIXMAN_OP_OVER, g->pix,
				    NULL, dst, 0, 0, 0, 0, x + g->x,
				    y + font->ascent - g->y, g->width,
				    g->height);
			else
				pixman_image_composite32(PIXMAN_OP_OVER, src,
				    g->pix, dst, 0, 0, 0, 0, x + g->x,
				    y + font->ascent - g->y, g->width,
				    g->height);
		}
		x += g->advance.x;
	}
	pixman_image_unref(src);
}

/* A key that does something other than type a character. */
static int
special(const struct osk_key *k)
{
	return k->act != OSK_A_KEY || strlen(k->label) > 1 ||
	    k->glyph != NULL || k->usage == OSK_U_SPACE;
}

void
kb_draw(uint32_t *px, int w, int h, int stride, const struct osk_view *v)
{
	const struct osk_layer *l = &osk_layers[v->layer];
	const struct osk_key *k;
	pixman_image_t *dst;
	unsigned i, x0, y0, x1, y1;
	int gap = h / OSK_ROWS / 14 < 2 ? 2 : h / OSK_ROWS / 14, tw;
	uint32_t bg, fg;
	const char *label;

	dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, px, stride);
	font_for(h / OSK_ROWS);
	fill(dst, 0, 0, w, h, STYLE_INK_DEEP);
	for (i = 0; i < l->nkeys; i++) {
		k = &l->keys[i];
		osk_rect(k, (unsigned)w, (unsigned)h, &x0, &y0, &x1, &y1);
		bg = special(k) ? STYLE_LINE : STYLE_SURFACE;
		fg = STYLE_TEXT;
		if (k->act == OSK_A_MOD && k->usage < OSK_NMODS) {
			if (v->mod[k->usage] == OSK_ARMED) {
				bg = STYLE_ACCENT_DEEP;
				fg = STYLE_ACCENT;
			} else if (v->mod[k->usage] == OSK_LOCKED) {
				bg = STYLE_ACCENT;
				fg = STYLE_INK;
			}
		}
		if ((int)i == v->pressed) {
			bg = STYLE_ACCENT;
			fg = STYLE_INK;
		}
		fill(dst, (int)x0 + gap, (int)y0 + gap,
		    (int)(x1 - x0) - 2 * gap, (int)(y1 - y0) - 2 * gap, bg);
		if (font == NULL)
			continue;
		label = k->label;
		if (k->glyph != NULL && measure(k->glyph) >= 0)
			label = k->glyph;
		if ((tw = measure(label)) < 0)
			continue;
		text(dst, (int)(x0 + x1) / 2 - tw / 2,
		    (int)(y0 + y1) / 2 - font->height / 2, label, fg);
	}
	pixman_image_unref(dst);
}
