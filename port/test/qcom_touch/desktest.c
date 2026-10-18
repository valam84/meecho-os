/*
 * desktest - the touch driver's touches for the desktop (qcom_touch's
 * desk.c) read back by meechowm's reader of them (touchsplit.c), on the
 * host.  run.sh builds and runs it.
 *
 * The driver sends; what it sends is collected as the input server would
 * hand it to /dev/mousemux and given to mc_touch_split() in pieces.  Worked
 * out by hand: on a 1200x1920 screen (the Mi Pad 4's), a touch at the top
 * left corner is 0,0, at the bottom right corner 1,1, at pixel (599, 959)
 * - to a 65535th, rounded down -
 * 599/1199 and 959/1919; past the edge is the edge.  A contact that moves
 * is a motion; one that does not, nothing; a lift, an up - once; a lift of
 * a contact that was never down, nothing at all; desk_lift() lifts all
 * that are down.  The mouse's events between them pass through untouched.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <minix/input.h>

#include "desk.h"
#include "touchsplit.h"

static long checks, failed;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		failed++;						\
		printf("FAIL %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

static struct input_event sent[256];
static unsigned nsent;

static void
send(unsigned page, unsigned code, int value, unsigned flags)
{
	struct input_event *e = &sent[nsent++];

	memset(e, 0, sizeof(*e));
	e->page = (uint16_t)page;
	e->code = (uint16_t)code;
	e->value = value;
	e->flags = (uint16_t)flags;
}

static void
mouse(int dx)
{
	send(INPUT_PAGE_GD, INPUT_GD_X, dx, INPUT_FLAG_REL);
}

static char got[512];
static size_t ngot;

static void
c_down(unsigned id, double x, double y, void *arg)
{
	(void)arg;
	ngot += (size_t)snprintf(got + ngot, sizeof(got) - ngot,
	    "d%u %.6f %.6f;", id, x, y);
}

static void
c_motion(unsigned id, double x, double y, void *arg)
{
	(void)arg;
	ngot += (size_t)snprintf(got + ngot, sizeof(got) - ngot,
	    "m%u %.6f %.6f;", id, x, y);
}

static void
c_up(unsigned id, void *arg)
{
	(void)arg;
	ngot += (size_t)snprintf(got + ngot, sizeof(got) - ngot, "u%u;", id);
}

static void
c_frame(void *arg)
{
	(void)arg;
}

static const struct mc_touch_ops ops = { c_down, c_motion, c_up, c_frame };

/* What the reader makes of what was sent, read `piece' events at a time;
 * the mouse's events it leaves, and how many. */
static unsigned
read_back(unsigned piece)
{
	static struct mc_touch t;
	struct input_event copy[256];
	unsigned i, n, left = 0;

	memset(&t, 0, sizeof(t));
	memcpy(copy, sent, sizeof(sent[0]) * nsent);
	ngot = 0;
	got[0] = '\0';
	for (i = 0; i < nsent; i += n) {
		n = nsent - i < piece ? nsent - i : piece;
		left += (unsigned)mc_touch_split(&t, &copy[i], n, &ops, NULL);
	}
	return left;
}

int
main(void)
{
	struct desk d;
	char want[512];
	unsigned piece, left;

	desk_init(&d, 1200, 1920, send);
	desk_touch(&d, 0, 0, 0);		/* the top left corner */
	mouse(5);
	desk_touch(&d, 1, 1199, 1919);		/* the bottom right */
	desk_touch(&d, 0, 599, 959);		/* touch 0 moves */
	desk_touch(&d, 0, 599, 959);		/* ...and stays */
	desk_touch(&d, 2, 5000, 5000);		/* past the edge */
	desk_up(&d, 1);
	desk_up(&d, 1);				/* once only */
	desk_up(&d, 7);				/* never down */
	mouse(-3);
	desk_lift(&d);				/* 0 and 2 */
	snprintf(want, sizeof(want),
	    "d0 0.000000 0.000000;d1 1.000000 1.000000;m0 %.6f %.6f;"
	    "d2 1.000000 1.000000;u1;u0;u2;",
	    /* to a 65535th, down: what INPUT_DIGI_MAX can say */
	    (double)(599 * 65535 / 1199) / 65535,
	    (double)(959 * 65535 / 1919) / 65535);
	for (piece = 1; piece <= nsent; piece++) {
		left = read_back(piece);
		CHECK(strcmp(got, want) == 0, "pieces of %u: %s, wanted %s",
		    piece, got, want);
		CHECK(left == 2, "pieces of %u: %u of the mouse's left", piece,
		    left);
	}
	CHECK(d.down == 0, "down 0x%x after desk_lift()", d.down);
	/* Fractions sent as they are, clamped. */
	nsent = 0;
	desk_touch_frac(&d, 3, 70000, 32768);
	read_back(nsent);
	snprintf(want, sizeof(want), "d3 1.000000 %.6f;", 32768.0 / 65535);
	CHECK(strcmp(got, want) == 0, "fractions: %s, wanted %s", got, want);
	printf("desktest: %ld checks, %ld failed\n", checks, failed);
	return failed != 0;
}
