/*
 * The x-ray's picture: the windows drawn back, into a band in the middle
 * of the screen, and the processes around them by how far they are -
 * those the windows talk to straight in a row above, the next ones in two
 * columns at the sides, the drivers in a row below, and under everything
 * the kernel, as a bar.  A line is as thick as its stream of messages
 * (xr_line_width(), a logarithm: a hundred times more is three times
 * thicker, so that one busy pair does not hide the rest).
 *
 * Laid out afresh at every sample, in an order that does not depend on
 * the rates - the services in a fixed order, then by label - so that the
 * picture does not jump about as the numbers change.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pixman.h>
#include <fcft/fcft.h>

#include "style.h"
#include "xray.h"

#define NODE_PAD	6
#define BG_ALPHA	0xe0	/* how far the windows go back */

struct xr_draw {
	struct fcft_font *font;
	int fh;			/* line height */
	int W, H;
	int bx, by, bw, bh;	/* the band the windows are drawn back into */
	double scale;
	int ox, oy;		/* the band's origin for screen coordinates */
	int kx, ky, kw, kh;	/* the kernel's bar */
};

/* The services that are the system's spine, first and in this order. */
static const char *const order[] = {
	"vfs", "pm", "vm", "sched", "rs", "ds", "mib", "tty", "pfs", "mfs",
	"uds", "lwip", "procfs", "input", "mali", "fb",
};

double
xr_line_width(double rate)
{
	double w = 1 + 2.5 * log10(1 + rate);

	return w > 14 ? 14 : w;
}

struct xr_draw *
xr_draw_new(void)
{
	struct xr_draw *d = calloc(1, sizeof(*d));

	if (d == NULL)
		return NULL;
	fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	return d;
}

/* The font, at a size for the screen: re-made when the height changes. */
static void
font_for(struct xr_draw *d, int height)
{
	char name[64];
	const char *names[1] = { name };
	int px = height / 60 < 11 ? 11 : height / 60;

	if (d->font != NULL && d->H == height)
		return;
	if (d->font != NULL)
		fcft_destroy(d->font);
	snprintf(name, sizeof(name), "monospace:pixelsize=%d", px);
	d->font = fcft_from_name(1, names, NULL);
	d->fh = d->font != NULL ? d->font->height : px + 2;
}

/*===========================================================================*
 *    text                                                                   *
 *===========================================================================*/
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
colour(uint32_t rgb, unsigned a)
{
	pixman_color_t c;

	/* Premultiplied, as pixman and wl_shm's ARGB8888 have it. */
	c.alpha = (uint16_t)(a * 257);
	c.red = (uint16_t)(((rgb >> 16) & 0xff) * a / 255 * 257);
	c.green = (uint16_t)(((rgb >> 8) & 0xff) * a / 255 * 257);
	c.blue = (uint16_t)((rgb & 0xff) * a / 255 * 257);
	return c;
}

/* Draw s at (x, y) - its top - no wider than maxw; dst NULL: measure. */
static int
text(struct xr_draw *d, pixman_image_t *dst, int x, int y, const char *s,
	uint32_t rgb, int maxw)
{
	const struct fcft_glyph *g;
	pixman_color_t c = colour(rgb, 255);
	pixman_image_t *src = NULL;
	int pen = x;

	if (d->font == NULL)
		return 0;
	if (dst != NULL)
		src = pixman_image_create_solid_fill(&c);
	while (*s != '\0') {
		g = fcft_rasterize_char_utf32(d->font, next_cp(&s),
		    FCFT_SUBPIXEL_NONE);
		if (g == NULL)
			continue;
		if (maxw > 0 && pen + g->advance.x > x + maxw)
			break;
		if (dst != NULL && g->width > 0) {
			if (pixman_image_get_format(g->pix) == PIXMAN_a8r8g8b8)
				pixman_image_composite32(PIXMAN_OP_OVER, g->pix,
				    NULL, dst, 0, 0, 0, 0, pen + g->x,
				    y + d->font->ascent - g->y, g->width,
				    g->height);
			else
				pixman_image_composite32(PIXMAN_OP_OVER, src,
				    g->pix, dst, 0, 0, 0, 0, pen + g->x,
				    y + d->font->ascent - g->y, g->width,
				    g->height);
		}
		pen += g->advance.x;
	}
	if (src != NULL)
		pixman_image_unref(src);
	return pen - x;
}

static void
rate_text(char *buf, size_t size, double rate)
{
	if (rate >= 10000)
		snprintf(buf, size, "%.0fk/s", rate / 1000);
	else if (rate >= 10)
		snprintf(buf, size, "%.0f/s", rate);
	else
		snprintf(buf, size, "%.1f/s", rate);
}

/*===========================================================================*
 *    layout                                                                 *
 *===========================================================================*/
static int
rank(const struct xr_node *n)
{
	size_t i;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
		if (strcmp(n->label, order[i]) == 0)
			return (int)i;
	return (int)(sizeof(order) / sizeof(order[0]));
}

static const struct xr_model *sort_model;

static int
cmp_nodes(const void *a, const void *b)
{
	const struct xr_node *x = &sort_model->node[*(const int *)a];
	const struct xr_node *y = &sort_model->node[*(const int *)b];
	int r = rank(x) - rank(y);

	return r != 0 ? r : strcmp(x->label, y->label);
}

static void
node_size(struct xr_draw *d, struct xr_node *n)
{
	char r[32];
	int w1, w2;

	rate_text(r, sizeof(r), n->rate);
	w1 = text(d, NULL, 0, 0, n->label, 0, 0);
	w2 = text(d, NULL, 0, 0, r, 0, 0);
	n->w = (w1 > w2 ? w1 : w2) + 2 * NODE_PAD;
	if (n->w < 6 * d->fh / 2)
		n->w = 6 * d->fh / 2;
	n->h = 2 * d->fh + NODE_PAD;
}

/* A row of nodes centred on y, across [x0, x1]; too many: the rest hide. */
static void
place_row(struct xr_draw *d, struct xr_model *m, const int *idx, int n,
	int x0, int x1, int y)
{
	int total = 0, gap, i, x, fit = n;

	for (i = 0; i < n; i++)
		total += m->node[idx[i]].w;
	while (fit > 0 && total + (fit - 1) * d->fh / 2 > x1 - x0) {
		fit--;
		total -= m->node[idx[fit]].w;
	}
	for (i = fit; i < n; i++)
		m->node[idx[i]].shown = false;
	if (fit == 0)
		return;
	gap = fit > 1 ? (x1 - x0 - total) / (fit + 1) : 0;
	if (gap > 4 * d->fh)
		gap = 4 * d->fh;
	x = (x0 + x1) / 2 - (total + (fit - 1) * gap) / 2;
	for (i = 0; i < fit; i++) {
		m->node[idx[i]].x = x;
		m->node[idx[i]].y = y - m->node[idx[i]].h / 2;
		x += m->node[idx[i]].w + gap;
	}
}

/* A column of nodes centred on x, across [y0, y1]. */
static void
place_column(struct xr_draw *d, struct xr_model *m, const int *idx, int n,
	int x, int y0, int y1)
{
	int h = 2 * d->fh + NODE_PAD, gap, i, y, fit = n;

	while (fit > 0 && fit * h + (fit - 1) * d->fh / 3 > y1 - y0)
		fit--;
	for (i = fit; i < n; i++)
		m->node[idx[i]].shown = false;
	if (fit == 0)
		return;
	gap = fit > 1 ? (y1 - y0 - fit * h) / (fit + 1) : 0;
	if (gap > 2 * d->fh)
		gap = 2 * d->fh;
	y = (y0 + y1) / 2 - (fit * h + (fit - 1) * gap) / 2;
	for (i = 0; i < fit; i++) {
		m->node[idx[i]].x = x - m->node[idx[i]].w / 2;
		m->node[idx[i]].y = y;
		y += h + gap;
	}
}

void
xr_layout(struct xr_draw *d, struct xr_model *m, int W, int H)
{
	int top[XR_MAXNODE], left[XR_MAXNODE], right[XR_MAXNODE];
	int bottom[XR_MAXNODE], side[XR_MAXNODE];
	int nt = 0, nl = 0, nr = 0, nb = 0, ns = 0, i, desk = -1;
	struct xr_node *n;
	double sx, sy;

	font_for(d, H);
	d->W = W;
	d->H = H;

	/* The band the windows go back into, and the scale they go by. */
	d->bx = W * 20 / 100;
	d->bw = W * 60 / 100;
	d->by = H * 25 / 100;
	d->bh = H * 44 / 100;
	sx = m->sw > 0 ? (double)d->bw / m->sw : 1;
	sy = m->ah > 0 ? (double)d->bh / m->ah : 1;
	d->scale = sx < sy ? sx : sy;
	d->ox = d->bx + (d->bw - (int)(m->sw * d->scale)) / 2;
	d->oy = d->by + (d->bh - (int)(m->ah * d->scale)) / 2;
	for (i = 0; i < m->nwin; i++) {
		m->win[i].dx = d->ox + (int)(m->win[i].x * d->scale);
		m->win[i].dy = d->oy + (int)((m->win[i].y - m->ay) * d->scale);
		m->win[i].dw = (int)(m->win[i].w * d->scale);
		m->win[i].dh = (int)(m->win[i].h * d->scale);
	}

	/* The kernel's bar along the bottom. */
	d->kh = d->fh + 2 * NODE_PAD;
	d->kx = W * 8 / 100;
	d->kw = W * 84 / 100;
	d->ky = H - d->kh - H * 3 / 100;

	for (i = 0; i < m->nnode; i++) {
		n = &m->node[i];
		if (!n->shown || n->kind == XR_WINDOW)
			continue;
		if (n->kind == XR_KERNEL) {
			n->x = d->kx;
			n->y = d->ky;
			n->w = d->kw;
			n->h = d->kh;
			continue;
		}
		node_size(d, n);
		if (n->kind == XR_DESK)
			desk = i;
		else if (n->depth == 1)
			top[nt++] = i;
		else if (n->depth == 2)
			side[ns++] = i;
		else
			bottom[nb++] = i;
	}
	sort_model = m;
	qsort(top, nt, sizeof(int), cmp_nodes);
	qsort(side, ns, sizeof(int), cmp_nodes);
	qsort(bottom, nb, sizeof(int), cmp_nodes);
	for (i = 0; i < ns; i++) {
		if (i % 2 == 0)
			left[nl++] = side[i];
		else
			right[nr++] = side[i];
	}
	place_row(d, m, top, nt, W * 3 / 100, W * 97 / 100, H * 12 / 100);
	place_column(d, m, left, nl, W * 10 / 100, H * 22 / 100, H * 74 / 100);
	place_column(d, m, right, nr, W * 90 / 100, H * 22 / 100, H * 74 / 100);
	place_row(d, m, bottom, nb, W * 3 / 100, W * 97 / 100, H * 80 / 100);
	if (desk >= 0) {
		m->node[desk].x = W / 2 - m->node[desk].w / 2;
		m->node[desk].y = d->by + d->bh + d->fh / 2;
	}
	for (i = 0; i < m->nwin; i++)
		if (m->win[i].node >= 0) {
			n = &m->node[m->win[i].node];
			n->x = m->win[i].dx;
			n->y = m->win[i].dy;
			n->w = m->win[i].dw;
			n->h = m->win[i].dh;
		}
}

int
xr_hit_window(const struct xr_draw *d, const struct xr_model *m, int x,
	int y)
{
	int i;

	for (i = 0; i < m->nwin; i++)
		if (x >= m->win[i].dx && x < m->win[i].dx + m->win[i].dw &&
		    y >= m->win[i].dy && y < m->win[i].dy + m->win[i].dh)
			return i;
	return -1;
}

/*===========================================================================*
 *    drawing                                                                *
 *===========================================================================*/
static void
fill(pixman_image_t *dst, int x, int y, int w, int h, uint32_t rgb,
	unsigned a)
{
	pixman_color_t c = colour(rgb, a);
	pixman_rectangle16_t r;

	if (w <= 0 || h <= 0)
		return;
	r.x = (int16_t)x;
	r.y = (int16_t)y;
	r.width = (uint16_t)w;
	r.height = (uint16_t)h;
	pixman_image_fill_rectangles(PIXMAN_OP_OVER, dst, &c, 1, &r);
}

static void
frame(pixman_image_t *dst, int x, int y, int w, int h, int t, uint32_t rgb,
	unsigned a)
{
	fill(dst, x, y, w, t, rgb, a);
	fill(dst, x, y + h - t, w, t, rgb, a);
	fill(dst, x, y + t, t, h - 2 * t, rgb, a);
	fill(dst, x + w - t, y + t, t, h - 2 * t, rgb, a);
}

/* A straight line w wide: a quadrilateral, as two triangles, smoothed. */
static void
line(pixman_image_t *dst, double x0, double y0, double x1, double y1,
	double w, uint32_t rgb, unsigned a)
{
	pixman_color_t c = colour(rgb, a);
	pixman_image_t *src;
	pixman_triangle_t t[2];
	double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy), nx, ny;

	if (l < 1)
		return;
	nx = -dy / l * w / 2;
	ny = dx / l * w / 2;
#define P(px, py)	{ pixman_double_to_fixed(px), pixman_double_to_fixed(py) }
	{
		pixman_point_fixed_t a0 = P(x0 + nx, y0 + ny);
		pixman_point_fixed_t a1 = P(x0 - nx, y0 - ny);
		pixman_point_fixed_t a2 = P(x1 - nx, y1 - ny);
		pixman_point_fixed_t a3 = P(x1 + nx, y1 + ny);

		t[0].p1 = a0; t[0].p2 = a1; t[0].p3 = a2;
		t[1].p1 = a0; t[1].p2 = a2; t[1].p3 = a3;
	}
#undef P
	src = pixman_image_create_solid_fill(&c);
	pixman_composite_triangles(PIXMAN_OP_OVER, src, dst, PIXMAN_a8, 0, 0,
	    0, 0, 2, t);
	pixman_image_unref(src);
}

/* Where the line from a box's centre towards (tx, ty) leaves the box. */
static void
attach(const struct xr_node *n, double tx, double ty, double *x, double *y)
{
	double cx = n->x + n->w / 2.0, cy = n->y + n->h / 2.0;
	double dx = tx - cx, dy = ty - cy, k = 1;

	if (dx != 0 && fabs(dx) * n->h > fabs(dy) * n->w)
		k = (n->w / 2.0) / fabs(dx);
	else if (dy != 0)
		k = (n->h / 2.0) / fabs(dy);
	*x = cx + dx * k;
	*y = cy + dy * k;
}

static void
edge_ends(const struct xr_model *m, const struct xr_edge *e, double *x0,
	double *y0, double *x1, double *y1)
{
	const struct xr_node *a = &m->node[e->from], *b = &m->node[e->to];

	/* To or from the kernel: straight up from its bar. */
	if (a->kind == XR_KERNEL || b->kind == XR_KERNEL) {
		const struct xr_node *k = a->kind == XR_KERNEL ? a : b;
		const struct xr_node *o = a->kind == XR_KERNEL ? b : a;
		double ox = o->x + o->w / 2.0;

		if (ox < k->x + 4)
			ox = k->x + 4;
		if (ox > k->x + k->w - 4)
			ox = k->x + k->w - 4;
		*x0 = ox;
		*y0 = k->y;
		attach(o, ox, k->y, x1, y1);
		return;
	}
	attach(a, b->x + b->w / 2.0, b->y + b->h / 2.0, x0, y0);
	attach(b, a->x + a->w / 2.0, a->y + a->h / 2.0, x1, y1);
}

void
xr_render(struct xr_draw *d, const struct xr_model *m, uint32_t *px,
	int W, int H, int stride)
{
	pixman_image_t *dst;
	const struct xr_node *n;
	const struct xr_edge *e;
	double x0, y0, x1, y1, f;
	char buf[128], r[32];
	uint32_t rgb;
	int i, j, a;

	dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, px, stride);
	if (dst == NULL)
		return;
	/* Everything goes back: the screen under a veil of the ground. */
	memset(px, 0, (size_t)stride * H);
	fill(dst, 0, 0, W, H, STYLE_INK, BG_ALPHA);

	/* The lines under the boxes; both ways of a pair in one. */
	for (i = 0; i < m->nedge; i++) {
		e = &m->edge[i];
		if (!e->shown)
			continue;
		f = e->rate;
		for (j = 0; j < m->nedge; j++)
			if (m->edge[j].from == e->to && m->edge[j].to == e->from &&
			    m->edge[j].shown) {
				if (j < i)
					f = -1;	/* drawn with the other */
				else
					f += m->edge[j].rate;
				break;
			}
		if (f < 0)
			continue;
		edge_ends(m, e, &x0, &y0, &x1, &y1);
		if (m->node[e->from].kind == XR_KERNEL ||
		    m->node[e->to].kind == XR_KERNEL) {
			rgb = STYLE_MAGENTA;
			a = 0xb0;
		} else if (m->node[e->from].depth == 0 ||
		    m->node[e->to].depth == 0) {
			rgb = STYLE_ACCENT;
			a = 0xe0;
		} else {
			rgb = STYLE_BLUE_B;
			a = 0x90;
		}
		line(dst, x0, y0, x1, y1, xr_line_width(f), rgb, a);
	}

	/* The windows, gone back. */
	for (i = 0; i < m->nwin; i++) {
		const struct xr_win *w = &m->win[i];

		fill(dst, w->dx, w->dy, w->dw, w->dh, STYLE_INK_DEEP, 0xd0);
		frame(dst, w->dx, w->dy, w->dw, w->dh, 2, w->focus ?
		    STYLE_ACCENT : STYLE_ACCENT_DEEP, 0xff);
		text(d, dst, w->dx + NODE_PAD, w->dy + NODE_PAD, w->title,
		    STYLE_TEXT, w->dw - 2 * NODE_PAD);
		snprintf(buf, sizeof(buf), "pid %d", (int)w->pid);
		if (w->node >= 0) {
			rate_text(r, sizeof(r), m->node[w->node].rate);
			snprintf(buf, sizeof(buf), "pid %d  %s", (int)w->pid, r);
		}
		text(d, dst, w->dx + NODE_PAD, w->dy + NODE_PAD + d->fh, buf,
		    STYLE_MUTED, w->dw - 2 * NODE_PAD);
	}

	/* The processes around them. */
	for (i = 0; i < m->nnode; i++) {
		n = &m->node[i];
		if (!n->shown || n->kind == XR_WINDOW)
			continue;
		if (n->kind == XR_KERNEL) {
			fill(dst, n->x, n->y, n->w, n->h, STYLE_SURFACE, 0xff);
			frame(dst, n->x, n->y, n->w, n->h, 1, STYLE_MAGENTA, 0xff);
			rate_text(r, sizeof(r), n->rate);
			snprintf(buf, sizeof(buf), "kernel: interrupts, clock, "
			    "kernel calls  %s", r);
			text(d, dst, n->x + NODE_PAD, n->y + NODE_PAD, buf,
			    STYLE_TEXT, n->w - 2 * NODE_PAD);
			continue;
		}
		fill(dst, n->x, n->y, n->w, n->h, STYLE_SURFACE, 0xff);
		if (n->flash_ms > 0)
			/* Restarted: a flash of yellow, fading. */
			fill(dst, n->x, n->y, n->w, n->h, STYLE_YELLOW,
			    (unsigned)(255 * n->flash_ms / 2500));
		frame(dst, n->x, n->y, n->w, n->h, 1, n->kind == XR_DESK ?
		    STYLE_ACCENT_DEEP : STYLE_LINE, 0xff);
		text(d, dst, n->x + NODE_PAD, n->y + NODE_PAD / 2, n->label,
		    n->flash_ms > 1200 ? STYLE_INK : STYLE_TEXT, 0);
		rate_text(r, sizeof(r), n->rate);
		text(d, dst, n->x + NODE_PAD, n->y + NODE_PAD / 2 + d->fh, r,
		    n->flash_ms > 1200 ? STYLE_INK : STYLE_MUTED, 0);
	}

	/* What this is, and how to leave it. */
	rate_text(r, sizeof(r), m->rate);
	snprintf(buf, sizeof(buf), "x-ray: the system as processes, the lines "
	    "its messages - %s", r);
	text(d, dst, W * 2 / 100, H * 2 / 100, buf, STYLE_TEXT, 0);
	text(d, dst, W * 2 / 100, H * 2 / 100 + d->fh, "click a window for "
	    "its passport; Esc or Super+x to leave", STYLE_MUTED, 0);
	pixman_image_unref(dst);
}
