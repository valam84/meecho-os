/*
 * mousetest - meechowm's reading of the mouse (minix/usr.bin/meechowm/
 * mousesplit.c) on the host, fed by usb_mouse's own report.c: the events
 * come in the order the driver really sends them, not in a copy of that
 * order.  mousetest.sh builds and runs it.
 *
 * The cursor is a 1280x720 screen's, stopped at the edges axis by axis as
 * wlr_cursor stops it on one output.  The reference is every event sent
 * alone, in the order it came: one motion per X or Y, a button where it
 * is.  What mc_mouse_split() makes of the same stream, read in pieces of
 * any size, must put every button and every notch of the wheel at the
 * reference's place and leave the cursor where the reference does.
 *
 * Two kinds of check.  The moves of qemu-strip.py, step 8, worked out by
 * hand before the code ran: twelve reports -127,-127 into the corner,
 * three of X +100 and two of Y +100, a press and a release - the click at
 * (300,200); read in the pieces the compositor's log showed when it went
 * wrong (4, 1, 3 and 1 events at the end), and in pieces of every size
 * from 1 to 32.  Then random streams: one to three mice, each with its
 * own buttons, reports with motion biased toward the edges (where motions
 * stop adding up), the mice interleaved event by event as the input
 * server may interleave them, read in random pieces.  And on every run:
 * no frame without an event before it, and nothing left after a read -
 * the last thing a read sends is a frame.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <minix/input.h>

#include "mousesplit.h"
#include "report.h"

#define W	1280
#define H	720
#define MAXEV	4096
#define MAXOUT	4096

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

/* A button or a notch, and where the cursor was when it came. */
struct thing {
	int kind;		/* 'b' a button, 'w' the wheel */
	int code, value;
	int x, y;
};

struct cursor {
	int x, y;
	struct thing out[MAXOUT];
	int n;
	int pending;		/* events since the last frame */
	int frames, empty;	/* frames, frames with nothing before them */
};

static int
clamp(int v, int max)
{
	return v < 0 ? 0 : v > max ? max : v;
}

static void
move(struct cursor *c, int dx, int dy)
{
	c->x = clamp(c->x + dx, W - 1);
	c->y = clamp(c->y + dy, H - 1);
}

static void
put(struct cursor *c, int kind, int code, int value)
{
	if (c->n < MAXOUT)
		c->out[c->n++] = (struct thing){ kind, code, value, c->x, c->y };
}

static void
op_motion(int dx, int dy, void *arg)
{
	struct cursor *c = arg;

	move(c, dx, dy);
	c->pending++;
}

static void
op_wheel(int value, void *arg)
{
	struct cursor *c = arg;

	put(c, 'w', 0, value);
	c->pending++;
}

static void
op_button(unsigned hid, int pressed, void *arg)
{
	struct cursor *c = arg;

	put(c, 'b', (int)hid, pressed);
	c->pending++;
}

static void
op_frame(void *arg)
{
	struct cursor *c = arg;

	c->frames++;
	if (c->pending == 0)
		c->empty++;
	c->pending = 0;
}

static const struct mc_mouse_ops ops = {
	op_motion, op_wheel, op_button, op_frame
};

/* The stream of events, as the input server keeps them. */
static struct input_event ev[MAXEV];
static int nev;
static uint16_t cur_dev;

static void
driver_event(unsigned page, unsigned code, int value, int rel, void *arg)
{
	(void)arg;
	if (nev >= MAXEV)
		return;
	memset(&ev[nev], 0, sizeof(ev[nev]));
	ev[nev].page = (uint16_t)page;
	ev[nev].code = (uint16_t)code;
	ev[nev].value = value;
	ev[nev].flags = rel ? INPUT_FLAG_REL : 0;
	ev[nev].devid = cur_dev;
	nev++;
}

/* The reference: every event alone. */
static void
reference(const struct input_event *e, int n, struct cursor *c, int x, int y)
{
	int i;

	memset(c, 0, sizeof(*c));
	c->x = x;
	c->y = y;
	for (i = 0; i < n; i++) {
		if (e[i].page == INPUT_PAGE_GD && e[i].code == INPUT_GD_X)
			move(c, e[i].value, 0);
		else if (e[i].page == INPUT_PAGE_GD && e[i].code == INPUT_GD_Y)
			move(c, 0, e[i].value);
		else if (e[i].page == INPUT_PAGE_GD &&
		    e[i].code == INPUT_GD_WHEEL)
			put(c, 'w', 0, e[i].value);
		else if (e[i].page == INPUT_PAGE_BUTTON)
			put(c, 'b', e[i].code, e[i].value != INPUT_RELEASE);
	}
}

/*
 * The stream read in pieces: piece[] gives their sizes in turn, the last
 * one again and again.  Compares with the reference; answers the number of
 * differences.
 */
static int
run(const char *what, const struct input_event *e, int n, int x, int y,
	const int *piece, int npiece, struct cursor *out)
{
	static struct cursor ref, got;
	int i, k, at, bad = 0;

	reference(e, n, &ref, x, y);
	memset(&got, 0, sizeof(got));
	got.x = x;
	got.y = y;
	for (at = 0, k = 0; at < n; at += i, k++) {
		i = piece[k < npiece ? k : npiece - 1];
		if (i > n - at)
			i = n - at;
		mc_mouse_split(e + at, (size_t)i, &ops, &got);
		CHECK(got.pending == 0, "%s: a read at %d left %d event(s) "
		    "without a frame", what, at, got.pending);
		got.pending = 0;
	}
	CHECK(got.empty == 0, "%s: %d empty frame(s)", what, got.empty);
	CHECK(got.n == ref.n, "%s: %d button(s) and notch(es), reference %d",
	    what, got.n, ref.n);
	for (i = 0; i < got.n && i < ref.n; i++) {
		if (memcmp(&got.out[i], &ref.out[i], sizeof(got.out[i])) == 0)
			continue;
		CHECK(0, "%s: #%d %c %d/%d at %d,%d, reference %c %d/%d at "
		    "%d,%d", what, i, got.out[i].kind, got.out[i].code,
		    got.out[i].value, got.out[i].x, got.out[i].y,
		    ref.out[i].kind, ref.out[i].code, ref.out[i].value,
		    ref.out[i].x, ref.out[i].y);
		bad++;
	}
	CHECK(got.x == ref.x && got.y == ref.y,
	    "%s: cursor at %d,%d, reference %d,%d", what, got.x, got.y,
	    ref.x, ref.y);
	if (got.x != ref.x || got.y != ref.y || got.n != ref.n)
		bad++;
	if (out != NULL)
		*out = got;
	return bad;
}

static unsigned
report(unsigned old, int b, int x, int y, int w)
{
	uint8_t rep[4] = { (uint8_t)b, (uint8_t)x, (uint8_t)y, (uint8_t)w };

	return mouse_report(old, rep, sizeof(rep), 1, driver_event, NULL);
}

/* qemu-strip.py, step 8: from anywhere, into the corner, to (300,200). */
static void
step8(void)
{
	static struct cursor got;
	/* The tail of the log of a failed run: 4, 1, 3 and 1 event(s), the
	 * twenty-two before them in pieces of 2 (one report each). */
	static const int logged[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	    4, 1, 3, 1 };
	unsigned old = 0;
	int i, k;
	char what[64];

	nev = 0;
	cur_dev = 0;
	for (i = 0; i < 12; i++)
		old = report(old, 0, -127, -127, 0);
	for (i = 0; i < 3; i++)
		old = report(old, 0, 100, 0, 0);
	for (i = 0; i < 2; i++)
		old = report(old, 0, 0, 100, 0);
	old = report(old, 1, 0, 0, 0);
	old = report(old, 0, 0, 0, 0);
	CHECK(nev == 31, "step 8: %d events, worked out 31", nev);

	/* By hand: the press and the release at (300,200). */
	run("step 8 as logged", ev, nev, 640, 360, logged,
	    sizeof(logged) / sizeof(logged[0]), &got);
	CHECK(got.n == 2 && got.out[0].kind == 'b' && got.out[0].value == 1 &&
	    got.out[0].x == 300 && got.out[0].y == 200 &&
	    got.out[1].value == 0 && got.out[1].x == 300 &&
	    got.out[1].y == 200, "step 8 as logged: the click at %d,%d, "
	    "worked out 300,200", got.n ? got.out[0].x : -1,
	    got.n ? got.out[0].y : -1);
	/* The corner from either side of the screen, in every piece size. */
	for (k = 1; k <= 32; k++) {
		snprintf(what, sizeof(what), "step 8 in pieces of %d", k);
		run(what, ev, nev, 1279, 719, &k, 1, &got);
		CHECK(got.n == 2 && got.out[0].x == 300 && got.out[0].y == 200,
		    "%s: the click at %d,%d", what, got.n ? got.out[0].x : -1,
		    got.n ? got.out[0].y : -1);
	}
	/* From the left edge: -127, +100, +100 ends at 200, not 73. */
	nev = 0;
	old = report(0, 0, -127, 0, 0);
	old = report(old, 0, 100, 0, 0);
	old = report(old, 0, 100, 0, 0);
	k = 32;
	run("the edge", ev, nev, 0, 0, &k, 1, &got);
	CHECK(got.x == 200, "the edge: at %d, worked out 200", got.x);
}

static int
axis(void)
{
	switch (rand() % 8) {
	case 0: case 1: case 2:
		return 0;
	case 3:
		return -127 - rand() % 2;	/* to an edge, hard */
	case 4:
		return 127;
	default:
		return rand() % 256 - 128;
	}
}

static void
streams(int rounds)
{
	static struct input_event mouse[3][MAXEV];
	int nm[3], pos[3], piece[64];
	unsigned old[3];
	int r, m, mice, i, k, nreports, bad = 0;
	char what[64];

	for (r = 0; r < rounds; r++) {
		mice = 1 + rand() % 3;
		nreports = 1 + rand() % 200;
		/* Each mouse's reports through report.c, alone. */
		for (m = 0; m < mice; m++) {
			nev = 0;
			cur_dev = (uint16_t)m;
			old[m] = 0;
			for (i = 0; i < nreports; i++)
				old[m] = report(old[m], rand() % 8, axis(),
				    axis(), rand() % 4 == 0 ?
				    rand() % 5 - 2 : 0);
			memcpy(mouse[m], ev, sizeof(ev[0]) * nev);
			nm[m] = nev;
			pos[m] = 0;
		}
		/* Interleaved event by event, each mouse in its own order. */
		for (nev = 0;;) {
			for (m = 0, k = 0; m < mice; m++)
				k += nm[m] - pos[m];
			if (k == 0 || nev >= MAXEV)
				break;
			do
				m = rand() % mice;
			while (pos[m] == nm[m]);
			ev[nev++] = mouse[m][pos[m]++];
		}
		for (i = 0; i < 64; i++)
			piece[i] = rand() % 3 == 0 ? 32 : 1 + rand() % 32;
		snprintf(what, sizeof(what), "stream %d (%d mice, %d events)",
		    r, mice, nev);
		bad += run(what, ev, nev, rand() % W, rand() % H, piece, 64,
		    NULL) != 0;
	}
	CHECK(bad == 0, "%d of %d stream(s) differ", bad, rounds);
}

int
main(int argc, char **argv)
{
	srand(argc > 1 ? (unsigned)atoi(argv[1]) : 11);
	step8();
	streams(20000);
	printf("mousetest: %ld check(s), %ld failed\n", checks, failed);
	return failed != 0;
}
