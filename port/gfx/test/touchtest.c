/*
 * touchtest - meechowm's reading of a touch screen (minix/usr.bin/meechowm/
 * touchsplit.c) on the host.  touchtest.sh builds and runs it.
 *
 * A stream is what the input server hands a reader of /dev/mousemux: the
 * frames of a touch screen (<minix/input.h>: contact, absolute X and Y, tip
 * switch) interleaved, event by event, with a mouse's reports (relative X
 * and Y, buttons, the wheel), and read in pieces of any size - a frame can
 * be cut by the end of a read.  Two things are checked, against a reference
 * that takes the frames whole, one at a time:
 *
 *   - the mouse's events come out of mc_touch_split() untouched and in
 *     their order, and nothing of the touch screen's with them;
 *   - the contacts: each frame of the reference says down (a contact that
 *     was up, touching), motion (one that was down, somewhere else), up
 *     (one that was down, released) or nothing, with X and Y clamped into
 *     the screen; the calls must be those, in that order, the fractions
 *     exact; and a frame call ends every read that had a call, and only
 *     those.
 *
 *   touchtest [seed]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <minix/input.h>

#include "touchsplit.h"

#define MAXEV	8192

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

/* A call made, or wanted: 'd' down, 'm' motion, 'u' up, 'f' a frame. */
struct call {
	int kind;
	unsigned id;
	double x, y;
};

static struct call got[MAXEV], want[MAXEV];
static int ngot, nwant;
static int read_had_call;

static void
c_down(unsigned id, double x, double y, void *arg)
{
	got[ngot++] = (struct call){ 'd', id, x, y };
	read_had_call = 1;
}

static void
c_motion(unsigned id, double x, double y, void *arg)
{
	got[ngot++] = (struct call){ 'm', id, x, y };
	read_had_call = 1;
}

static void
c_up(unsigned id, void *arg)
{
	got[ngot++] = (struct call){ 'u', id, 0, 0 };
	read_had_call = 1;
}

static int frames, frame_without_call, frame_not_last;

static void
c_frame(void *arg)
{
	frames++;
	if (!read_had_call)
		frame_without_call++;
	read_had_call = 0;
	got[ngot++] = (struct call){ 'f', 0, 0, 0 };
}

static const struct mc_touch_ops ops = { c_down, c_motion, c_up, c_frame };

static struct input_event
ev(uint16_t page, uint16_t code, int32_t value, uint16_t flags)
{
	struct input_event e;

	memset(&e, 0, sizeof(e));
	e.page = page;
	e.code = code;
	e.value = value;
	e.flags = flags;
	return e;
}

static double
clampf(int32_t v)
{
	return (double)(v < 0 ? 0 : v > INPUT_DIGI_MAX ? INPUT_DIGI_MAX : v) /
	    INPUT_DIGI_MAX;
}

static int
rnd(int n)
{
	return (int)(random() % n);
}

/* One run: a random stream, read in random pieces. */
static void
run(int nmouse_reports, int nframes, int maxread)
{
	static struct input_event mouse[MAXEV], touch[MAXEV], stream[MAXEV],
	    left[MAXEV];
	struct mc_touch t;
	int nm = 0, nt = 0, ns = 0, nl = 0, i, im, it, n, k;
	int downs[MC_TOUCH_MAX + 2], ax[MC_TOUCH_MAX + 2],
	    ay[MC_TOUCH_MAX + 2];
	size_t r;

	memset(&t, 0, sizeof(t));
	memset(downs, 0, sizeof(downs));
	ngot = nwant = 0;
	frames = frame_without_call = frame_not_last = 0;

	/* A mouse: relative motion, buttons, the wheel. */
	for (i = 0; i < nmouse_reports; i++) {
		mouse[nm++] = ev(INPUT_PAGE_GD, INPUT_GD_X, rnd(255) - 127,
		    INPUT_FLAG_REL);
		mouse[nm++] = ev(INPUT_PAGE_GD, INPUT_GD_Y, rnd(255) - 127,
		    INPUT_FLAG_REL);
		if (rnd(4) == 0)
			mouse[nm++] = ev(INPUT_PAGE_GD, INPUT_GD_WHEEL,
			    rnd(3) - 1, INPUT_FLAG_REL);
		if (rnd(3) == 0)
			mouse[nm++] = ev(INPUT_PAGE_BUTTON, 1 + rnd(3),
			    rnd(2), 0);
	}
	/* A touch screen: frames of up to MC_TOUCH_MAX + 2 contacts (the
	 * last two out of range: never followed), X and Y a little past the
	 * screen at times; the reference takes each frame whole. */
	for (i = 0; i < nframes; i++) {
		int id = rnd(MC_TOUCH_MAX + 2), tip = rnd(3) != 0;
		int32_t x = rnd(INPUT_DIGI_MAX + 2001) - 1000;
		int32_t y = rnd(INPUT_DIGI_MAX + 2001) - 1000;

		if (downs[id] && rnd(2) == 0) {	/* stays where it is */
			x = ax[id];
			y = ay[id];
		}
		touch[nt++] = ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID,
		    id, 0);
		touch[nt++] = ev(INPUT_PAGE_GD, INPUT_GD_X, x, INPUT_FLAG_ABS);
		touch[nt++] = ev(INPUT_PAGE_GD, INPUT_GD_Y, y, INPUT_FLAG_ABS);
		touch[nt++] = ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH,
		    tip ? INPUT_PRESS : INPUT_RELEASE, 0);
		if (id >= MC_TOUCH_MAX)
			continue;
		if (!tip) {
			if (downs[id])
				want[nwant++] = (struct call){ 'u', id, 0, 0 };
			downs[id] = 0;
		} else if (!downs[id]) {
			want[nwant++] = (struct call){ 'd', id, clampf(x),
			    clampf(y) };
			downs[id] = 1;
		} else if (x != ax[id] || y != ay[id])
			want[nwant++] = (struct call){ 'm', id, clampf(x),
			    clampf(y) };
		ax[id] = x;
		ay[id] = y;
	}
	/* Interleaved event by event. */
	for (im = it = 0; im < nm || it < nt; )
		if (it == nt || (im < nm && rnd(2) == 0))
			stream[ns++] = mouse[im++];
		else
			stream[ns++] = touch[it++];

	for (i = 0; i < ns; i += n) {
		n = 1 + rnd(maxread);
		if (n > ns - i)
			n = ns - i;
		read_had_call = 0;
		k = ngot;
		r = mc_touch_split(&t, &stream[i], (size_t)n, &ops, NULL);
		/* A read that had a call ends with a frame. */
		if (ngot > k && got[ngot - 1].kind != 'f')
			frame_not_last++;
		memcpy(&left[nl], &stream[i], r * sizeof(stream[0]));
		nl += (int)r;
	}

	CHECK(nl == nm, "%d mouse events left, %d sent", nl, nm);
	for (i = 0; i < nl && i < nm; i++)
		if (memcmp(&left[i], &mouse[i], sizeof(left[i])) != 0) {
			CHECK(0, "mouse event %d changed or out of order", i);
			break;
		}
	CHECK(frame_without_call == 0, "%d frames with no call before",
	    frame_without_call);
	CHECK(frame_not_last == 0, "%d reads end without a frame",
	    frame_not_last);
	/* The calls, frames aside, against the reference. */
	for (i = k = 0; i < ngot; i++) {
		if (got[i].kind == 'f')
			continue;
		if (k >= nwant) {
			CHECK(0, "call %c %u past the reference's %d",
			    got[i].kind, got[i].id, nwant);
			return;
		}
		if (got[i].kind != want[k].kind || got[i].id != want[k].id ||
		    got[i].x != want[k].x || got[i].y != want[k].y) {
			CHECK(0, "call %d: %c %u %.6f %.6f, wanted %c %u %.6f "
			    "%.6f", k, got[i].kind, got[i].id, got[i].x,
			    got[i].y, want[k].kind, want[k].id, want[k].x,
			    want[k].y);
			return;
		}
		k++;
	}
	CHECK(k == nwant, "%d calls, the reference %d", k, nwant);
}

int
main(int argc, char **argv)
{
	unsigned seed = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 1;
	int i;

	srandom(seed);
	/* By hand: one finger down at the middle, moved, lifted - read whole
	 * and an event at a time. */
	{
		struct input_event s[] = {
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID, 0, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_X, 32768, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_Y, 65535, 0),
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH, 1, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_X, 5, INPUT_FLAG_REL),
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID, 0, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_X, 0, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_Y, 65535, 0),
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH, 1, 0),
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_CONTACT_ID, 0, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_X, 0, 0),
			ev(INPUT_PAGE_GD, INPUT_GD_Y, 65535, 0),
			ev(INPUT_PAGE_DIGITIZER, INPUT_DIGI_TIP_SWITCH, 0, 0),
		};
		int each;

		for (each = 0; each < 2; each++) {
			struct mc_touch t;
			struct input_event c[13];
			size_t r, kept = 0;
			unsigned j;

			memset(&t, 0, sizeof(t));
			ngot = 0;
			memcpy(c, s, sizeof(s));
			if (each)
				for (j = 0; j < 13; j++) {
					r = mc_touch_split(&t, &c[j], 1, &ops,
					    NULL);
					kept += r;
				}
			else
				kept = mc_touch_split(&t, c, 13, &ops, NULL);
			CHECK(kept == 1, "by hand: %zu kept", kept);
			CHECK(ngot >= 4 && got[0].kind == 'd' &&
			    got[0].x == 32768.0 / 65535 && got[0].y == 1.0,
			    "by hand: the down at the bottom middle");
			for (j = 0; j < (unsigned)ngot; j++)
				if (got[j].kind == 'm')
					break;
			CHECK(j < (unsigned)ngot && got[j].x == 0,
			    "by hand: one motion to the left edge");
			CHECK(got[ngot - 1].kind == 'f' &&
			    got[ngot - 2].kind == 'u', "by hand: up, frame");
		}
	}
	for (i = 0; i < 3000; i++)
		run(rnd(40), rnd(200), 1 + rnd(48));
	printf("touchtest: %ld checks, %ld failed (seed %u)\n", checks, failed,
	    seed);
	return failed != 0;
}
