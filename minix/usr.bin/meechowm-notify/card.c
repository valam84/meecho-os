/*
 * A card: a stripe in the colour of what happened - yellow a restart, red
 * a stop, green a return - and two lines, the service and what happened,
 * then the facts in a quieter colour: its pid and endpoint now, and when.
 * Opaque, so that what the compositor puts on the screen is exactly what
 * is drawn here (the stand compares the two).  The colours are MEECHO's
 * palette (style.h), the typeface its monospace (Go Mono), sized for the
 * screen as the x-ray sizes its own.
 */
#include <stdlib.h>
#include <string.h>

#include <pixman.h>
#include <fcft/fcft.h>

#include "notify.h"
#include "style.h"

struct nt_card {
	enum nt_kind kind;
	char label[NT_LABEL];
	char what[32];		/* after the label: "restarted", "stopped" */
	char facts[96];
	int w, h;
	int title_w;		/* the first line, label and what */
};

static struct fcft_font *font;
static int fh = 16, pad = 8, stripe = 4, gap = 4;

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

/* Draw s with its top at (x, y); dst NULL: only measure.  Its width. */
static int
text(pixman_image_t *dst, int x, int y, const char *s, uint32_t rgb)
{
	const struct fcft_glyph *g;
	pixman_color_t c = colour(rgb);
	pixman_image_t *src = NULL;
	int pen = x;

	if (font == NULL)
		return 0;
	if (dst != NULL)
		src = pixman_image_create_solid_fill(&c);
	while (*s != '\0') {
		g = fcft_rasterize_char_utf32(font, next_cp(&s),
		    FCFT_SUBPIXEL_NONE);
		if (g == NULL)
			continue;
		if (dst != NULL && g->width > 0) {
			if (pixman_image_get_format(g->pix) == PIXMAN_a8r8g8b8)
				pixman_image_composite32(PIXMAN_OP_OVER, g->pix,
				    NULL, dst, 0, 0, 0, 0, pen + g->x,
				    y + font->ascent - g->y, g->width,
				    g->height);
			else
				pixman_image_composite32(PIXMAN_OP_OVER, src,
				    g->pix, dst, 0, 0, 0, 0, pen + g->x,
				    y + font->ascent - g->y, g->width,
				    g->height);
		}
		pen += g->advance.x;
	}
	if (src != NULL)
		pixman_image_unref(src);
	return pen - x;
}

static void
fill(pixman_image_t *dst, int x, int y, int w, int h, uint32_t rgb)
{
	pixman_color_t c = colour(rgb);
	pixman_rectangle16_t r;

	if (w <= 0 || h <= 0)
		return;
	r.x = (int16_t)x;
	r.y = (int16_t)y;
	r.width = (uint16_t)w;
	r.height = (uint16_t)h;
	pixman_image_fill_rectangles(PIXMAN_OP_SRC, dst, &c, 1, &r);
}

/* The typeface for a screen this tall: the x-ray's size, a little larger. */
void
nt_card_init(int screen_height)
{
	char name[64];
	const char *names[1] = { name };
	int px = screen_height / 45 < 12 ? 12 : screen_height / 45;

	if (font == NULL)
		fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	else
		fcft_destroy(font);
	snprintf(name, sizeof(name), "monospace:pixelsize=%d", px);
	font = fcft_from_name(1, names, NULL);
	fh = font != NULL ? font->height : px + 2;
	pad = fh / 2;
	stripe = fh / 4 < 3 ? 3 : fh / 4;
	gap = fh / 4;
}

static uint32_t
stripe_colour(enum nt_kind k)
{
	return k == NT_STOPPED ? STYLE_RED : k == NT_BACK ? STYLE_GREEN :
	    STYLE_YELLOW;
}

struct nt_card *
nt_card_new(const struct nt_event *ev, const char *when)
{
	struct nt_card *c;
	int w;

	if ((c = calloc(1, sizeof(*c))) == NULL)
		return NULL;
	c->kind = ev->kind;
	strlcpy(c->label, ev->label, sizeof(c->label));
	switch (ev->kind) {
	case NT_RESTART:
		if (ev->times > 1)
			snprintf(c->what, sizeof(c->what), " restarted %d times",
			    ev->times);
		else
			strlcpy(c->what, " restarted", sizeof(c->what));
		snprintf(c->facts, sizeof(c->facts), "pid %d · endpoint %d · %s",
		    ev->pid, ev->endpoint, when);
		break;
	case NT_STOPPED:
		strlcpy(c->what, " stopped", sizeof(c->what));
		snprintf(c->facts, sizeof(c->facts), "not running · %s", when);
		break;
	case NT_BACK:
		strlcpy(c->what, " is back", sizeof(c->what));
		snprintf(c->facts, sizeof(c->facts), "pid %d · endpoint %d · %s",
		    ev->pid, ev->endpoint, when);
		break;
	}
	w = c->title_w = text(NULL, 0, 0, c->label, 0) +
	    text(NULL, 0, 0, c->what, 0);
	if (text(NULL, 0, 0, c->facts, 0) > w)
		w = text(NULL, 0, 0, c->facts, 0);
	if (w < 14 * fh)
		w = 14 * fh;
	c->w = stripe + 2 * pad + w;
	c->h = 2 * pad + 2 * fh + gap;
	return c;
}

void
nt_card_free(struct nt_card *c)
{
	free(c);
}

void
nt_card_size(const struct nt_card *c, int *w, int *h)
{
	*w = c->w;
	*h = c->h;
}

/*
 * Where in a card its first line is - what the stand compares: from the
 * left edge, stripe and all, to the end of the text; the rest of the card
 * is as wide as its second line, which has the time in it.
 */
void
nt_card_title_band(const struct nt_card *c, int *x, int *y, int *w, int *h)
{
	*x = 0;
	*y = pad;
	*w = stripe + pad + c->title_w;
	*h = fh;
}

/* The card into a buffer buf_w x buf_h, its top left corner at (x, y). */
void
nt_card_draw(const struct nt_card *c, uint32_t *px, int stride, int x, int y,
	int buf_w, int buf_h)
{
	pixman_image_t *dst;
	int tx = x + stripe + pad, lw;

	dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, buf_w, buf_h, px,
	    stride);
	if (dst == NULL)
		return;
	fill(dst, x, y, c->w, c->h, STYLE_SURFACE);
	fill(dst, x, y, stripe, c->h, stripe_colour(c->kind));
	/* A quiet edge round it, so that it stands off a light window. */
	fill(dst, x + stripe, y, c->w - stripe, 1, STYLE_LINE);
	fill(dst, x + stripe, y + c->h - 1, c->w - stripe, 1, STYLE_LINE);
	fill(dst, x + c->w - 1, y, 1, c->h, STYLE_LINE);
	lw = text(dst, tx, y + pad, c->label, STYLE_BRIGHT);
	text(dst, tx + lw, y + pad, c->what, STYLE_TEXT);
	text(dst, tx, y + pad + fh + gap, c->facts, STYLE_MUTED);
	pixman_image_unref(dst);
}

/* The card as a line of text, for -d. */
void
nt_card_text(const struct nt_card *c, FILE *f)
{
	static const char *const kinds[] = { "restart", "stopped", "back" };

	fprintf(f, "card %s %s%s | %s\n", kinds[c->kind], c->label, c->what,
	    c->facts);
}
