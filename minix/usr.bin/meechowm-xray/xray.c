/*
 * meechowm-xray: the x-ray view of the desktop (xray.h says what it shows
 * and where that comes from).  A layer surface over everything
 * (wlr-layer-shell, the overlay layer, all of the screen, the keyboard to
 * itself); the compositor starts it on Super+x and ends it on the next
 * (meechowm -m xray).  Esc ends it too.  A click on a window asks the
 * compositor for that window's passport (meechowm -m passport PID), which
 * the compositor draws in the window's frame - the passport is the
 * compositor's word, not this program's.
 *
 *   meechowm-xray [-i ms]        the view; a sample every ms (1000)
 *   meechowm-xray -d [-n N] [-i ms]
 *                                no view: N samples (1) as text, ms apart
 *
 * -d prints what the view would draw, a line each:
 *
 *   window PID slot SLOT title TITLE
 *   wrect PID X Y W H focus|-                  where it is drawn, gone back
 *   line PID LABEL ENDPOINT COUNT RATE WIDTH   what the window's process
 *                                              sent to that process
 *   sum PID SENT LINES KERNEL                  its row's sum, the lines',
 *                                              and how much of it the
 *                                              kernel sent in its name
 *   node LABEL ENDPOINT PID KIND DEPTH RATE X Y W H     shown nodes, laid
 *                                              out for a screen of the
 *                                              compositor's size
 *   flash LABEL ENDPOINT MS                    a service restarted lately
 *   service LABEL ENDPOINT RESTARTS FLASH_MS   /proc/service, last look
 *   edge FROM TO COUNT RATE WIDTH              shown edges, by label
 *
 * between "XRAY begin N" and "XRAY end N".
 */
#include <sys/mman.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xray.h"

#define KEY_ESC		1	/* evdev codes, as wl_keyboard has them */
#define KEY_Q		16

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static int width, height, configured;
static volatile sig_atomic_t done;
static double ptr_x, ptr_y;
static int clicked = -1;	/* a window index clicked, to be sent */

static struct xr_model model;
static struct xr_draw *draw;

static uint64_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/*===========================================================================*
 *    -d                                                                     *
 *===========================================================================*/
static const char *
kind_name(enum xr_kind k)
{
	static const char *const names[] = { "window", "desk", "service",
	    "kernel", "other" };

	return names[k];
}

static void
dump(int seq)
{
	const struct xr_node *n, *a, *b;
	const struct xr_edge *e;
	uint64_t lines;
	int i, j;

	xr_layout(draw, &model, model.sw, model.sh);
	printf("XRAY begin %d screen %dx%d windows %d nodes %d rate %.1f\n",
	    seq, model.sw, model.sh, model.nwin, model.nnode, model.rate);
	for (i = 0; i < model.nwin; i++) {
		if (model.win[i].node < 0) {
			printf("window %d slot - title %s\n",
			    (int)model.win[i].pid, model.win[i].title);
			continue;
		}
		n = &model.node[model.win[i].node];
		printf("window %d slot %d title %s\n", (int)n->pid, n->slot,
		    model.win[i].title);
		printf("wrect %d %d %d %d %d %s\n", (int)n->pid,
		    model.win[i].dx, model.win[i].dy, model.win[i].dw,
		    model.win[i].dh, model.win[i].focus ? "focus" : "-");
		lines = 0;
		for (j = 0; j < model.nedge; j++) {
			e = &model.edge[j];
			if (e->from != model.win[i].node)
				continue;
			b = &model.node[e->to];
			printf("line %d %s %d %llu %.2f %.1f\n", (int)n->pid,
			    b->label, (int)b->ep, (unsigned long long)e->count,
			    e->rate, e->shown ? xr_line_width(e->rate) : 0.0);
			lines += e->count;
		}
		printf("sum %d %llu %llu %llu\n", (int)n->pid,
		    (unsigned long long)n->sent, (unsigned long long)lines,
		    (unsigned long long)n->kernel);
	}
	for (i = 0; i < model.nnode; i++) {
		n = &model.node[i];
		if (!n->shown)
			continue;
		printf("node %s %d %d %s %d %.2f %d %d %d %d\n", n->label,
		    (int)n->ep, (int)n->pid, kind_name(n->kind), n->depth,
		    n->rate, n->x, n->y, n->w, n->h);
	}
	for (i = 0; i < model.nnode; i++)
		if (model.node[i].flash_ms > 0)
			printf("flash %s %d %u\n", model.node[i].label,
			    (int)model.node[i].ep, model.node[i].flash_ms);
	xr_print_services(stdout);
	for (i = 0; i < model.nedge; i++) {
		e = &model.edge[i];
		if (!e->shown)
			continue;
		a = &model.node[e->from];
		b = &model.node[e->to];
		printf("edge %s %s %llu %.2f %.1f\n", a->label, b->label,
		    (unsigned long long)e->count, e->rate,
		    xr_line_width(e->rate));
	}
	printf("XRAY end %d\n", seq);
	fflush(stdout);
}

/*===========================================================================*
 *    the surface                                                            *
 *===========================================================================*/
struct buffer {
	struct wl_buffer *wl;
	uint32_t *px;
	size_t size;
	int w, h;
	bool busy;
};
static struct buffer bufs[2];

static void
buffer_release(void *data, struct wl_buffer *wl)
{
	((struct buffer *)data)->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static struct buffer *
get_buffer(void)
{
	struct wl_shm_pool *pool;
	struct buffer *b;
	int i, fd;

	for (i = 0; i < 2; i++)
		if (!bufs[i].busy)
			break;
	if (i == 2)
		return NULL;
	b = &bufs[i];
	if (b->wl != NULL && (b->w != width || b->h != height)) {
		wl_buffer_destroy(b->wl);
		munmap(b->px, b->size);
		b->wl = NULL;
	}
	if (b->wl == NULL) {
		b->size = (size_t)width * height * 4;
		if ((fd = memfd_create("meechowm-xray", MFD_CLOEXEC)) < 0 ||
		    ftruncate(fd, (off_t)b->size) != 0) {
			perror("meechowm-xray: memfd");
			exit(1);
		}
		b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, 0);
		if (b->px == MAP_FAILED) {
			perror("meechowm-xray: mmap");
			exit(1);
		}
		pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
		b->wl = wl_shm_pool_create_buffer(pool, 0, width, height,
		    width * 4, WL_SHM_FORMAT_ARGB8888);
		wl_shm_pool_destroy(pool);
		close(fd);
		wl_buffer_add_listener(b->wl, &buffer_listener, b);
		b->w = width;
		b->h = height;
	}
	return b;
}

static void
redraw(void)
{
	struct buffer *b;

	if (!configured || width <= 0 || height <= 0 ||
	    (b = get_buffer()) == NULL)
		return;
	xr_layout(draw, &model, width, height);
	xr_render(draw, &model, b->px, width, height, width * 4);
	b->busy = true;
	wl_surface_attach(surface, b->wl, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, width, height);
	wl_surface_commit(surface);
}

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
	uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	width = (int)w;
	height = (int)h;
	configured = 1;
	redraw();
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	done = 1;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

/*===========================================================================*
 *    the pointer and the keys                                               *
 *===========================================================================*/
static void
ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
	struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
	ptr_x = wl_fixed_to_double(x);
	ptr_y = wl_fixed_to_double(y);
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
	ptr_x = wl_fixed_to_double(x);
	ptr_y = wl_fixed_to_double(y);
}

static void
ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		clicked = xr_hit_window(draw, &model, (int)ptr_x, (int)ptr_y);
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

/*
 * Every event of the version bound (wl_seat 5, so wl_pointer 5), even
 * those not wanted: libwayland aborts the client on an event whose
 * listener is NULL - which the first click, with its frame, did.
 */
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
kb_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
	uint32_t size)
{
	close(fd);	/* the two keys looked for are evdev codes */
}

static void
kb_enter(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *s, struct wl_array *keys)
{
}

static void
kb_leave(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *s)
{
}

static void
kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state)
{
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED &&
	    (key == KEY_ESC || key == KEY_Q))
		done = 1;
}

static void
kb_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
	uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group)
{
}

static void
kb_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat,
};

static void
seat_caps(void *data, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && pointer == NULL) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard == NULL) {
		keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
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
global(void *data, struct wl_registry *reg, uint32_t name,
	const char *iface, uint32_t version)
{
	if (strcmp(iface, wl_compositor_interface.name) == 0)
		compositor = wl_registry_bind(reg, name,
		    &wl_compositor_interface, 4);
	else if (strcmp(iface, wl_shm_interface.name) == 0)
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	else if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(reg, name, &wl_seat_interface,
		    version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
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

/* The click, as a command to the compositor: its passport, drawn there. */
static void
send_click(void)
{
	char cmd[64], reply[256];

	if (clicked < 0 || clicked >= model.nwin)
		return;
	snprintf(cmd, sizeof(cmd), "passport %d", (int)model.win[clicked].pid);
	clicked = -1;
	if (xr_command(cmd, reply, sizeof(reply)) < 0)
		fprintf(stderr, "meechowm-xray: %s: no compositor\n", cmd);
}

int
main(int argc, char **argv)
{
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct zwlr_layer_surface_v1 *ls;
	struct pollfd pfd;
	uint64_t last, t, next, tick;
	int c, dumps = 0, interval = 1000, i, wait;

	while ((c = getopt(argc, argv, "dn:i:")) != -1) {
		if (c == 'd')
			dumps = dumps > 0 ? dumps : 1;
		else if (c == 'n')
			dumps = atoi(optarg);
		else if (c == 'i')
			interval = atoi(optarg);
		else {
			fprintf(stderr, "usage: meechowm-xray [-i ms] | "
			    "-d [-n samples] [-i ms]\n");
			return 2;
		}
	}
	if (interval < 100)
		interval = 100;
	if ((draw = xr_draw_new()) == NULL)
		return 1;

	if (dumps > 0) {
		last = now_ms();
		for (i = 0; i < dumps; i++) {
			if (i > 0)
				usleep((useconds_t)interval * 1000);
			t = now_ms();
			if (!xr_sample(&model, (unsigned)(t - last))) {
				fprintf(stderr, "meechowm-xray: no compositor "
				    "(%s)\n", xr_sock_path() != NULL ?
				    xr_sock_path() : "no socket path");
				return 1;
			}
			last = t;
			dump(i + 1);
		}
		return 0;
	}

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);
	if ((dpy = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "meechowm-xray: no compositor (%s)\n",
		    strerror(errno));
		return 1;
	}
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	if (compositor == NULL || shm == NULL || layer_shell == NULL) {
		fprintf(stderr, "meechowm-xray: the compositor has no %s\n",
		    layer_shell == NULL ? "layer shell" : "shm");
		return 1;
	}
	last = now_ms();
	if (!xr_sample(&model, 0)) {
		fprintf(stderr, "meechowm-xray: no answer from meechowm -m\n");
		return 1;
	}

	surface = wl_compositor_create_surface(compositor);
	ls = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
	    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "meechowm-xray");
	zwlr_layer_surface_v1_add_listener(ls, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(ls,
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(ls,
	    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
	wl_surface_commit(surface);

	next = last + (uint64_t)interval;
	tick = last;
	while (!done) {
		t = now_ms();
		xr_tick(&model, (unsigned)(t - tick));
		tick = t;
		if (t >= next) {
			xr_sample(&model, (unsigned)(t - last));
			last = t;
			next = t + (uint64_t)interval;
			redraw();
		} else if (xr_flashing(&model))
			redraw();	/* a flash fades between samples */
		if (clicked >= 0)
			send_click();
		wait = (int)(next - now_ms());
		if (xr_flashing(&model) && wait > 100)
			wait = 100;
		if (wait < 0)
			wait = 0;
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
	zwlr_layer_surface_v1_destroy(ls);
	wl_surface_destroy(surface);
	wl_display_roundtrip(dpy);
	wl_display_disconnect(dpy);
	return 0;
}
