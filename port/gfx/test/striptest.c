/*
 * striptest - the strip's arithmetic (minix/usr.bin/meechowm/layout.c) on
 * the host.  striptest.sh builds and runs it.
 *
 * Two kinds of check.  Cases worked out by hand, before the code ran:
 * widths on the screens the port meets (1280 and 1920 HDMI, BTT's 800 and
 * 1024 panels), and the view through a walk across three half columns.
 * Then properties, over random strips and random walks: the focused column
 * is whole on the screen, columns keep their order and their gaps, the
 * view stays within the strip's ends, a column already on the screen does
 * not move the view, and a strip narrower than the screen starts at its
 * left.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layout.h"

static long checks, failed;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		failed++;						\
		if (failed <= 20) {					\
			printf("FAIL %s:%d: ", __FILE__, __LINE__);	\
			printf(__VA_ARGS__);				\
			printf("\n");					\
		}							\
	}								\
} while (0)

static void
widths(void)
{
	static const int areas[] = { 800, 1024, 1280, 1920 };
	unsigned i;
	int p, gap = 8;

	/* By hand: (1280 - 8) * p - 8. */
	CHECK(mc_col_width(MC_W_THIRD, 1280, 8) == 416, "1/3 of 1280");
	CHECK(mc_col_width(MC_W_HALF, 1280, 8) == 628, "1/2 of 1280");
	CHECK(mc_col_width(MC_W_TWOTHIRDS, 1280, 8) == 840, "2/3 of 1280");
	CHECK(mc_col_width(MC_W_FULL, 1280, 8) == 1264, "1 of 1280");
	CHECK(mc_col_width(MC_W_THIRD, 800, 8) == 256, "1/3 of 800");
	CHECK(mc_col_width(MC_W_HALF, 800, 8) == 388, "1/2 of 800");
	CHECK(mc_col_width(MC_W_TWOTHIRDS, 800, 8) == 520, "2/3 of 800");
	CHECK(mc_col_width(-1, 1280, 8) == 628, "unknown preset: a half");
	CHECK(mc_col_width(MC_W_THIRD, 10, 8) == 1, "never below a pixel");

	/* n columns of 1/n, and a third beside two thirds, fill the area:
	 * to the pixel where the area divides, a pixel short per column
	 * where it does not. */
	for (i = 0; i < sizeof(areas) / sizeof(areas[0]); i++) {
		int a = areas[i], t = mc_col_width(MC_W_THIRD, a, gap),
		    h = mc_col_width(MC_W_HALF, a, gap),
		    tt = mc_col_width(MC_W_TWOTHIRDS, a, gap),
		    f = mc_col_width(MC_W_FULL, a, gap), s;

		s = 3 * t + 4 * gap;
		CHECK(s <= a && s >= a - 2, "three thirds of %d: %d", a, s);
		s = 2 * h + 3 * gap;
		CHECK(s <= a && s >= a - 1, "two halves of %d: %d", a, s);
		s = t + tt + 3 * gap;
		CHECK(s <= a && s >= a - 2, "1/3 + 2/3 of %d: %d", a, s);
		CHECK(f + 2 * gap == a, "all of %d: %d", a, f);
		CHECK(t < h && h < tt && tt < f, "order of widths at %d", a);
	}

	for (p = 0; p < MC_W_N; p++)
		CHECK(mc_width_parse(mc_width_name(p)) == p, "name of %d", p);
	CHECK(mc_width_parse("3/4") == -1, "3/4 is no preset");
	CHECK(strcmp(mc_width_name(7), "?") == 0, "name of no preset");
}

static void
walk(void)
{
	int w[3] = { 628, 628, 628 }, x[3], v;

	/* By hand, 1280 wide, gap 8: the area is 1264, half columns start
	 * on the strip at 0, 636 and 1272, which is 1900 long. */
	v = mc_strip_fit(w, 3, 0, 0, 0, 1280, 8, x);
	CHECK(v == 0 && x[0] == 8 && x[1] == 644 && x[2] == 1280,
	    "first: view %d, x %d %d %d", v, x[0], x[1], x[2]);
	v = mc_strip_fit(w, 3, 1, v, 0, 1280, 8, x);
	CHECK(v == 0, "second is on the screen: view %d", v);
	v = mc_strip_fit(w, 3, 2, v, 0, 1280, 8, x);
	CHECK(v == 636 && x[0] == -628 && x[1] == 8 && x[2] == 644,
	    "third: view %d, x %d %d %d", v, x[0], x[1], x[2]);
	v = mc_strip_fit(w, 3, 1, v, 0, 1280, 8, x);
	CHECK(v == 636, "back to the second, which is on the screen: %d", v);
	v = mc_strip_fit(w, 3, 0, v, 0, 1280, 8, x);
	CHECK(v == 0 && x[0] == 8, "back to the first: view %d", v);

	/* The last column closed, the view was at its end: the view comes
	 * back to the strip's end, not to the column alone. */
	v = mc_strip_fit(w, 2, 1, 636, 0, 1280, 8, x);
	CHECK(v == 0 && x[0] == 8 && x[1] == 644, "two left: view %d", v);

	/* One half column in the middle: 314 either side of 640. */
	v = mc_strip_center(w, 1, 0, 1280, 8);
	mc_strip_place(w, 1, v, 0, 8, x);
	CHECK(v == -318 && x[0] == 326 && x[0] + 314 == 640,
	    "centred: view %d, x %d", v, x[0]);
	/* ...and a fit after it brings it back to the left. */
	v = mc_strip_fit(w, 1, 0, v, 0, 1280, 8, x);
	CHECK(v == 0 && x[0] == 8, "fit after centre: view %d", v);

	/* An area that does not start at 0 (a panel on the left). */
	v = mc_strip_fit(w, 3, 2, 0, 100, 1280, 8, x);
	CHECK(x[2] + 628 == 100 + 1280 - 8, "area at 100: right edge %d",
	    x[2] + 628);

	/* No columns. */
	CHECK(mc_strip_fit(w, 0, -1, 55, 0, 1280, 8, x) == 0, "empty strip");
	CHECK(mc_strip_center(w, 0, 0, 1280, 8) == 0, "centre of nothing");
}

static int
visible(const int *w, int n, int f, int view, int area_x, int area_w,
	int gap)
{
	int x[64];

	mc_strip_place(w, n, view, area_x, gap, x);
	return x[f] >= area_x + gap && x[f] + w[f] <= area_x + area_w - gap;
}

static void
properties(void)
{
	static const int areas[] = { 800, 1024, 1280, 1920 };
	static const int gaps[] = { 0, 4, 8, 16 };
	int w[64], x[64], round, step, i;

	srand(11);
	for (round = 0; round < 20000; round++) {
		int a = areas[rand() % 4], g = gaps[rand() % 4],
		    ax = rand() % 3 == 0 ? rand() % 200 : 0,
		    n = 1 + rand() % 12, view = 0, f = 0, total = 0, most;

		for (i = 0; i < n; i++) {
			w[i] = mc_col_width(rand() % MC_W_N, a, g);
			total += w[i] + (i > 0 ? g : 0);
		}
		most = total - (a - 2 * g);
		if (most < 0)
			most = 0;
		for (step = 0; step < 30; step++) {
			int was_visible, old = view, v;

			/* Now and then the view comes from a centring. */
			if (rand() % 10 == 0)
				view = mc_strip_center(w, n, rand() % n, a, g);
			old = view;
			f = rand() % 3 == 0 ? rand() % n :
			    (f + (rand() % 2 ? 1 : n - 1)) % n;
			was_visible = old >= 0 && old <= most &&
			    visible(w, n, f, old, ax, a, g);
			v = mc_strip_fit(w, n, f, old, ax, a, g, x);

			CHECK(x[f] >= ax + g && x[f] + w[f] <= ax + a - g,
			    "focused %d of %d not whole: x %d w %d, area %d+%d "
			    "gap %d", f, n, x[f], w[f], ax, a, g);
			for (i = 0; i + 1 < n; i++)
				CHECK(x[i + 1] - x[i] == w[i] + g,
				    "columns %d,%d apart by %d", i, i + 1,
				    x[i + 1] - x[i]);
			CHECK(v >= 0 && v <= most, "view %d outside 0..%d",
			    v, most);
			if (was_visible)
				CHECK(v == old, "view moved %d -> %d for a "
				    "column on the screen", old, v);
			if (most == 0)
				CHECK(x[0] == ax + g, "short strip not at the "
				    "left: x %d", x[0]);
			view = v;
		}
	}
}

int
main(void)
{
	widths();
	walk();
	properties();
	printf("striptest: %ld checks, %ld failed\n", checks, failed);
	return failed != 0;
}
