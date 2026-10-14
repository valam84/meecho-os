/*
 * meechowm-notify: a card on the desktop for each service RS restarts
 * (notify.h says what and where from).  A layer surface in the top right
 * corner, under the panel, that takes no keys and exists only while there
 * is a card to show; a card goes after -t ms, or all of them on a click.
 * meecho-desktop starts it with the panel.
 *
 *   meechowm-notify [-i ms] [-t ms]   the cards: a look at /proc/service
 *                                     every -i ms (1000), each card shown
 *                                     for -t ms (8000)
 *   meechowm-notify -d [-n looks] [-i ms]
 *                                     no desktop: a line for each card,
 *                                     "card KIND LABEL WHAT | FACTS"
 *   meechowm-notify -m looks          what a look costs: looks back to back,
 *                                     "look N services S ms M messages K",
 *                                     milliseconds and messages a look (this
 *                                     process's row of the kernel's message
 *                                     matrix, /proc/PID/ipc)
 *   meechowm-notify -R [-H height] -o file kind label
 *                                     a card, drawn for a screen this tall
 *                                     (720) to a file, ARGB8888 a row after
 *                                     another; prints "card W H band X Y W H"
 *                                     - where its first line is.  The stand
 *                                     (port/gfx/test/qemu-notify.py) holds
 *                                     the screen against it
 */
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "notify.h"

#define MAXCARDS	4	/* on the screen at once; the oldest goes */
#define MARGIN		8	/* from the screen's edge, and between cards */

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_output *output;
static struct wl_pointer *pointer;
static struct zwlr_layer_shell_v1 *layer_shell;
static int screen_h = 720;
static volatile sig_atomic_t done;
static bool clicked;

static struct {
	struct nt_card *card;
	uint64_t until;
} cards[MAXCARDS];
static int ncards;

static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static struct wl_buffer *buffer;
static void *buffer_px;
static size_t buffer_size;
static int surf_w, surf_h;

static uint64_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void
clock_text(char *buf, size_t size)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	if (tm == NULL || strftime(buf, size, "%H:%M:%S", tm) == 0)
		strlcpy(buf, "?", size);
}

/*===========================================================================*
 *    -m: what a look costs                                                  *
 *===========================================================================*/
/* All this process has sent, by its row of the kernel's message matrix. */
static unsigned long long
sent_so_far(void)
{
	char path[64], line[256], name[16];
	unsigned long long c, sum = 0;
	int slot, ep, pid;
	FILE *f;

	snprintf(path, sizeof(path), "/proc/%d/ipc", (int)getpid());
	if ((f = fopen(path, "r")) == NULL)
		return 0;
	while (fgets(line, sizeof(line), f) != NULL)
		if (sscanf(line, "to %d %d %d %15s %llu", &slot, &ep, &pid, name,
		    &c) == 5)
			sum += c;
	fclose(f);
	return sum;
}

static int
measure(int looks)
{
	struct nt_event ev[16];
	unsigned long long m0, m1, m2;
	uint64_t t0, t1;
	int i;

	if (nt_look(ev, 16) < 0) {
		fprintf(stderr, "meechowm-notify: no /proc/service\n");
		return 1;
	}
	/* The read of the matrix costs messages too: once with nothing
	 * between, to take it off. */
	m0 = sent_so_far();
	m1 = sent_so_far();
	t0 = now_ms();
	for (i = 0; i < looks; i++)
		nt_look(ev, 16);
	t1 = now_ms();
	m2 = sent_so_far();
	printf("look %d services %d ms %.3f messages %.1f\n", looks,
	    nt_services(), (double)(t1 - t0) / looks,
	    (double)((m2 - m1) - (m1 - m0)) / looks);
	return 0;
}

/*===========================================================================*
 *    -R: a card to a file                                                   *
 *===========================================================================*/
static int
reference(const char *kind, const char *label, const char *path)
{
	struct nt_event ev;
	struct nt_card *c;
	uint32_t *px;
	int w, h, bx, by, bw, bh, fd;
	size_t size;

	memset(&ev, 0, sizeof(ev));
	ev.kind = strcmp(kind, "stopped") == 0 ? NT_STOPPED :
	    strcmp(kind, "back") == 0 ? NT_BACK : NT_RESTART;
	strlcpy(ev.label, label, sizeof(ev.label));
	ev.times = 1;
	nt_card_init(screen_h);
	if ((c = nt_card_new(&ev, "00:00:00")) == NULL)
		return 1;
	nt_card_size(c, &w, &h);
	size = (size_t)w * h * 4;
	if ((px = calloc(1, size)) == NULL)
		return 1;
	nt_card_draw(c, px, w * 4, 0, 0, w, h);
	nt_card_title_band(c, &bx, &by, &bw, &bh);
	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0 ||
	    write(fd, px, size) != (ssize_t)size || close(fd) != 0) {
		perror(path);
		return 1;
	}
	printf("card %d %d band %d %d %d %d\n", w, h, bx, by, bw, bh);
	return 0;
}

/*===========================================================================*
 *    the cards                                                              *
 *===========================================================================*/
static void
add_card(const struct nt_event *ev, uint64_t until)
{
	char when[16];
	struct nt_card *c;

	clock_text(when, sizeof(when));
	if ((c = nt_card_new(ev, when)) == NULL)
		return;
	if (ncards == MAXCARDS) {
		nt_card_free(cards[0].card);
		memmove(&cards[0], &cards[1], sizeof(cards[0]) * (MAXCARDS - 1));
		ncards--;
	}
	cards[ncards].card = c;
	cards[ncards].until = until;
	ncards++;
}

/* The cards whose time is up go; whether any did. */
static bool
expire(uint64_t now, bool all)
{
	int i, j;
	bool gone = false;

	for (i = j = 0; i < ncards; i++) {
		if (all || cards[i].until <= now) {
			nt_card_free(cards[i].card);
			gone = true;
			continue;
		}
		cards[j++] = cards[i];
	}
	ncards = j;
	return gone;
}

/*===========================================================================*
 *    the surface                                                            *
 *===========================================================================*/
static void
drop_buffer(void)
{
	if (buffer != NULL) {
		wl_buffer_destroy(buffer);
		munmap(buffer_px, buffer_size);
		buffer = NULL;
	}
}

static void
drop_surface(void)
{
	if (layer != NULL) {
		zwlr_layer_surface_v1_destroy(layer);
		layer = NULL;
	}
	if (surface != NULL) {
		wl_surface_destroy(surface);
		surface = NULL;
	}
	drop_buffer();
}

/* The cards, newest at the top, each right-aligned, into one buffer. */
static void
draw(int w, int h)
{
	struct wl_shm_pool *pool;
	int i, y, cw, ch, fd;

	drop_buffer();
	buffer_size = (size_t)w * h * 4;
	if ((fd = memfd_create("meechowm-notify", MFD_CLOEXEC)) < 0 ||
	    ftruncate(fd, (off_t)buffer_size) != 0) {
		perror("meechowm-notify: memfd");
		exit(1);
	}
	buffer_px = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, 0);
	if (buffer_px == MAP_FAILED) {
		perror("meechowm-notify: mmap");
		exit(1);
	}
	memset(buffer_px, 0, buffer_size);
	for (i = ncards - 1, y = 0; i >= 0; i--) {
		nt_card_size(cards[i].card, &cw, &ch);
		nt_card_draw(cards[i].card, buffer_px, w * 4, w - cw, y, w, h);
		y += ch + MARGIN;
	}
	pool = wl_shm_create_pool(shm, fd, (int32_t)buffer_size);
	buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4,
	    WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, w, h);
	wl_surface_commit(surface);
}

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
	uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (w > 0 && h > 0)
		draw((int)w, (int)h);
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	drop_surface();
	expire(0, true);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

/*
 * The surface for the cards there are now: none, if there are none - an
 * empty surface would still be a place on the screen.  Made afresh when
 * the cards change: they change seldom, and a new surface is told its
 * size by the compositor before anything is drawn on it.
 */
static void
show(void)
{
	int i, w = 0, h = 0, cw, ch;

	drop_surface();
	if (ncards == 0)
		return;
	for (i = 0; i < ncards; i++) {
		nt_card_size(cards[i].card, &cw, &ch);
		if (cw > w)
			w = cw;
		h += (i > 0 ? MARGIN : 0) + ch;
	}
	surf_w = w;
	surf_h = h;
	surface = wl_compositor_create_surface(compositor);
	layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface,
	    NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "meechowm-notify");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(layer,
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(layer, (uint32_t)surf_w,
	    (uint32_t)surf_h);
	zwlr_layer_surface_v1_set_margin(layer, MARGIN, MARGIN, 0, 0);
	zwlr_layer_surface_v1_set_keyboard_interactivity(layer,
	    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(surface);
}

/*===========================================================================*
 *    the pointer, the output, the globals                                   *
 *===========================================================================*/
static void
ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
	struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
}

static void
ptr_leave(void *data, struct wl_pointer *p, uint32_t serial,
	struct wl_surface *s)
{
}

static void
ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	wl_fixed_t y)
{
}

static void
ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		clicked = true;
}

static void
ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
	wl_fixed_t value)
{
}

static void
ptr_frame(void *data, struct wl_pointer *p)
{
}

static void
ptr_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
}

static void
ptr_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
}

static void
ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
	int32_t discrete)
{
}

/* Every event of wl_pointer 5: libwayland aborts on a NULL listener. */
static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

static void
seat_caps(void *data, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && pointer == NULL) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void
seat_name(void *data, struct wl_seat *s, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

static void
out_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
	int32_t pw, int32_t ph, int32_t subpixel, const char *make,
	const char *model, int32_t transform)
{
}

static void
out_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w,
	int32_t h, int32_t refresh)
{
	if ((flags & WL_OUTPUT_MODE_CURRENT) && h > 0)
		screen_h = h;
}

static void
out_done(void *data, struct wl_output *o)
{
}

static void
out_scale(void *data, struct wl_output *o, int32_t factor)
{
}

static const struct wl_output_listener output_listener = {
	.geometry = out_geometry,
	.mode = out_mode,
	.done = out_done,
	.scale = out_scale,
};

static void
global(void *data, struct wl_registry *reg, uint32_t name,
	const char *iface, uint32_t version)
{
	if (strcmp(iface, wl_compositor_interface.name) == 0)
		compositor = wl_registry_bind(reg, name,
		    &wl_compositor_interface, 4);
	else if (strcmp(iface, wl_shm_interface.name) == 0)
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	else if (strcmp(iface, wl_seat_interface.name) == 0 && seat == NULL) {
		seat = wl_registry_bind(reg, name, &wl_seat_interface,
		    version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(iface, wl_output_interface.name) == 0 &&
	    output == NULL) {
		output = wl_registry_bind(reg, name, &wl_output_interface,
		    version < 2 ? version : 2);
		wl_output_add_listener(output, &output_listener, NULL);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
		layer_shell = wl_registry_bind(reg, name,
		    &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
}

static void
global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

static void
on_signal(int sig)
{
	done = 1;
}

/*===========================================================================*
 *    main                                                                   *
 *===========================================================================*/
static int
text_mode(int looks, int interval)
{
	struct nt_event ev[16];
	struct nt_card *c;
	char when[16];
	int i, j, n;

	nt_card_init(screen_h);
	if (nt_look(ev, 16) < 0) {
		fprintf(stderr, "meechowm-notify: no /proc/service\n");
		return 1;
	}
	printf("services %d\n", nt_services());
	fflush(stdout);
	for (i = 0; !done && (looks <= 0 || i < looks); i++) {
		usleep((useconds_t)interval * 1000);
		n = nt_look(ev, 16);
		clock_text(when, sizeof(when));
		for (j = 0; j < n; j++)
			if ((c = nt_card_new(&ev[j], when)) != NULL) {
				nt_card_text(c, stdout);
				nt_card_free(c);
			}
		fflush(stdout);
	}
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: meechowm-notify [-i ms] [-t ms]\n"
	    "       meechowm-notify -d [-n looks] [-i ms]\n"
	    "       meechowm-notify -m looks\n"
	    "       meechowm-notify -R [-H height] -o file kind label\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct nt_event ev[16];
	struct pollfd pfd;
	const char *out = NULL;
	uint64_t t, next, wake;
	int c, i, n, interval = 1000, shown_ms = 8000, looks = 0;
	int dumps = 0, measure_looks = 0, ref = 0, wait;
	bool changed;

	while ((c = getopt(argc, argv, "di:t:n:m:RH:o:")) != -1) {
		switch (c) {
		case 'd': dumps = 1; break;
		case 'i': interval = atoi(optarg); break;
		case 't': shown_ms = atoi(optarg); break;
		case 'n': looks = atoi(optarg); break;
		case 'm': measure_looks = atoi(optarg); break;
		case 'R': ref = 1; break;
		case 'H': screen_h = atoi(optarg); break;
		case 'o': out = optarg; break;
		default: usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (interval < 100)
		interval = 100;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	if (ref) {
		if (argc != 2 || out == NULL)
			usage();
		return reference(argv[0], argv[1], out);
	}
	if (measure_looks > 0)
		return measure(measure_looks);
	if (dumps)
		return text_mode(looks, interval);

	if ((dpy = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "meechowm-notify: no compositor (%s)\n",
		    strerror(errno));
		return 1;
	}
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	wl_display_roundtrip(dpy);	/* the output's mode, the seat's caps */
	if (compositor == NULL || shm == NULL || layer_shell == NULL) {
		fprintf(stderr, "meechowm-notify: the compositor has no %s\n",
		    layer_shell == NULL ? "layer shell" : "shm");
		return 1;
	}
	nt_card_init(screen_h);
	if (nt_look(ev, 16) < 0) {
		fprintf(stderr, "meechowm-notify: no /proc/service\n");
		return 1;
	}

	next = now_ms() + (uint64_t)interval;
	while (!done) {
		t = now_ms();
		changed = expire(t, clicked);
		clicked = false;
		if (t >= next) {
			n = nt_look(ev, 16);
			for (i = 0; i < n; i++)
				add_card(&ev[i], t + (uint64_t)shown_ms);
			changed |= n > 0;
			next = t + (uint64_t)interval;
		}
		if (changed)
			show();
		wake = next;
		for (i = 0; i < ncards; i++)
			if (cards[i].until < wake)
				wake = cards[i].until;
		wait = wake > now_ms() ? (int)(wake - now_ms()) : 0;

		while (wl_display_prepare_read(dpy) != 0)
			wl_display_dispatch_pending(dpy);
		wl_display_flush(dpy);
		pfd.fd = wl_display_get_fd(dpy);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, wait) > 0 && (pfd.revents & POLLIN))
			wl_display_read_events(dpy);
		else
			wl_display_cancel_read(dpy);
		if (wl_display_dispatch_pending(dpy) < 0)
			break;
	}
	drop_surface();
	wl_display_roundtrip(dpy);
	wl_display_disconnect(dpy);
	return 0;
}
