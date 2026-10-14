/*
 * The strip's arithmetic: layout.h says what it is.
 */
#include <string.h>

#include "layout.h"

static const int num[MC_W_N] = { 1, 1, 2, 1 };
static const int den[MC_W_N] = { 3, 2, 3, 1 };
static const char *const names[MC_W_N] = { "1/3", "1/2", "2/3", "1" };

int
mc_col_width(int preset, int area_w, int gap)
{
	int w;

	if (preset < 0 || preset >= MC_W_N)
		preset = MC_W_HALF;
	w = (area_w - gap) * num[preset] / den[preset] - gap;
	return w < 1 ? 1 : w;
}

const char *
mc_width_name(int preset)
{
	return preset >= 0 && preset < MC_W_N ? names[preset] : "?";
}

int
mc_width_parse(const char *s)
{
	int i;

	for (i = 0; i < MC_W_N; i++)
		if (strcmp(s, names[i]) == 0)
			return i;
	return -1;
}

void
mc_strip_place(const int *w, int n, int view, int area_x, int gap, int *x)
{
	int i, s = 0;

	for (i = 0; i < n; i++) {
		x[i] = area_x + gap + s - view;
		s += w[i] + gap;
	}
}

/* Where column focus starts on the strip, and how long the strip is. */
static void
strip_span(const int *w, int n, int focus, int gap, int *start, int *total)
{
	int i, s = 0;

	*start = 0;
	for (i = 0; i < n; i++) {
		if (i == focus)
			*start = s;
		s += w[i] + gap;
	}
	*total = n > 0 ? s - gap : 0;
}

int
mc_strip_fit(const int *w, int n, int focus, int view, int area_x,
	int area_w, int gap, int *x)
{
	int avail = area_w - 2 * gap, start, total, most;

	strip_span(w, n, focus, gap, &start, &total);
	if (focus >= 0 && focus < n) {
		if (start < view)
			view = start;
		else if (start + w[focus] > view + avail)
			view = start + w[focus] - avail;
	}
	/*
	 * Within the strip's ends.  This never hides the focused column:
	 * it lies inside the strip, and the view moved only as far as was
	 * needed to show it.
	 */
	most = total - avail;
	if (most < 0)
		most = 0;
	if (view > most)
		view = most;
	if (view < 0)
		view = 0;
	mc_strip_place(w, n, view, area_x, gap, x);
	return view;
}

/* A cubic ease-out, 1 - (1 - p)^3: the speed falls to nothing at the end. */
int
mc_ease(int a, int b, long t, long d)
{
	double p, e, v;

	if (d <= 0 || t >= d)
		return b;
	if (t <= 0)
		return a;
	p = (double)t / (double)d;
	e = 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p);
	v = (double)(b - a) * e;
	return a + (v >= 0 ? (int)(v + 0.5) : -(int)(-v + 0.5));
}

int
mc_strip_center(const int *w, int n, int focus, int area_w, int gap)
{
	int start, total;

	if (focus < 0 || focus >= n)
		return 0;
	strip_span(w, n, focus, gap, &start, &total);
	return start + w[focus] / 2 - (area_w - 2 * gap) / 2;
}

/* In integers: c + (v - c) * zoom / 1000, the half away from c. */
int
mc_zoom(int v, int c, int zoom)
{
	long d = (long)(v - c) * zoom;

	return c + (int)(d >= 0 ? (d + 500) / 1000 : -((-d + 500) / 1000));
}
