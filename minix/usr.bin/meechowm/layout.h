/*
 * The strip's arithmetic (stage 11.7): how wide a column is and where the
 * view stands.  Nothing of wlroots or of MEECHO is in it, so the host's
 * compiler builds it too - port/gfx/test/striptest.c checks it there.
 *
 * A workspace is a strip of columns, left to right, a gap between them
 * and at the edges of the screen.  Strip coordinates start at the left
 * edge of the first column; the view is the strip's x at the left edge
 * of the area that columns may take (the screen less a gap on each
 * side).  A column at strip x s is on the screen at
 *
 *	area_x + gap + s - view
 */
#ifndef MEECHOWM_LAYOUT_H
#define MEECHOWM_LAYOUT_H

/* A column's width: a third, a half, two thirds of the area, or all. */
enum { MC_W_THIRD, MC_W_HALF, MC_W_TWOTHIRDS, MC_W_FULL, MC_W_N };

/*
 * The width of a column of the given preset in an area area_w wide.  The
 * proportion is of the area less one gap, less one gap again: so that
 * columns of 1/n each, n of them, fill the area exactly, gaps included.
 */
int mc_col_width(int preset, int area_w, int gap);
/* "1/3", "1/2", "2/3", "1"; and back - -1 for anything else. */
const char *mc_width_name(int preset);
int mc_width_parse(const char *s);

/*
 * The columns of a workspace have widths w[0..n-1], and the focused one
 * is focus.  Answers the view that shows the focused column whole, moved
 * as little as possible from the view given, and never further than
 * either end of the strip: when the strip is narrower than the area it
 * starts at the left.  x[i] gets column i's x on the screen.
 */
int mc_strip_fit(const int *w, int n, int focus, int view, int area_x,
	int area_w, int gap, int *x);

/* The view with the focused column in the middle of the area. */
int mc_strip_center(const int *w, int n, int focus, int area_w, int gap);

/* x[i] for the view given, as it is. */
void mc_strip_place(const int *w, int n, int view, int area_x, int gap,
	int *x);

/*
 * Motion: where a thing is t ms into a move of d ms from a to b.  Eased
 * out - quick at first, slowing to rest - exactly a at the start and b
 * from d on, and never beyond b.  d <= 0 is no motion: b at once.
 */
int mc_ease(int a, int b, long t, long d);

#endif
