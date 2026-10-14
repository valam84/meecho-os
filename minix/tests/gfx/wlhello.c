/*
 * wlhello - the smallest Wayland client that shows something: a window of
 * one colour, drawn into shared memory.
 *
 * Every piece of stage 11 that runs before the screen does is in this
 * path: the socket to the compositor (uds), the buffer made with
 * memfd_create(2) and its descriptor passed over that socket (11.2),
 * libwayland's event loop on both sides (libepoll-shim, 11.3).  It
 * connects, binds wl_compositor, wl_shm and xdg_wm_base, makes a toplevel,
 * waits for its configure, attaches a 256x256 buffer filled with one
 * colour, commits, and waits for the compositor's frame callback - which
 * says the compositor has drawn it.  Then it prints
 *
 *   WLHELLO drawn WxH colour 0xAARRGGBB frames N
 *
 * and exits after SECONDS (default 3), or at once with -1.
 *
 * With -f the window fills the size the compositor gives it, and draws
 * itself again at each new one, printing "WLHELLO resized WxH": a
 * compositor that lays windows out (meechowm's strip, 11.7) can then be
 * seen doing it, a colour per window, on the screen.  -T names the window.
 *
 * With -L top or -L bottom it is no window but a panel (wlr-layer-shell):
 * a strip HEIGHT high (-H, 32) across that edge of the screen, on the top
 * layer, which takes its height off what windows may use - its exclusive
 * zone.  It draws at the size the compositor gives it.
 *
 * With -q it keeps no time: it waits for the compositor without a timeout
 * and lives until it is closed.  Between two events it then sends no
 * message at all - what the x-ray stand (port/gfx/test/qemu-xray.py)
 * needs to read its row of the message matrix and /proc/ktrace as counts
 * of one moment.  Without -q it wakes five times a second to look at the
 * clock.
 *
 *   wlhello [-f] [-q] [-c 0xAARRGGBB] [-t seconds] [-T title]
 *   wlhello -L top|bottom [-H height] [-c 0xAARRGGBB] [-t seconds]
 */
#include <sys/mman.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-client-protocol.h"

#define W	256
#define H	256

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct zwlr_layer_shell_v1 *layer_shell;
static int configured, frames;
static uint32_t colour = 0xff3060c0;

/* -f: the size asked for, the size drawn, and whether they differ. */
static int fill, want_w, want_h, cur_w = W, cur_h = H, resize;

static void
global(void *data, struct wl_registry *reg, uint32_t name,
	const char *iface, uint32_t version)
{
	if (strcmp(iface, wl_compositor_interface.name) == 0)
		compositor = wl_registry_bind(reg, name,
		    &wl_compositor_interface, 4);
	else if (strcmp(iface, wl_shm_interface.name) == 0)
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	else if (strcmp(iface, xdg_wm_base_interface.name) == 0)
		wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
	else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
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
wm_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void
surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	xdg_surface_ack_configure(s, serial);
	configured = 1;
	if (fill && want_w > 0 && want_h > 0 &&
	    (want_w != cur_w || want_h != cur_h))
		resize = 1;
}

static const struct xdg_surface_listener surface_listener = {
	.configure = surface_configure,
};

static void
toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
	struct wl_array *states)
{
	/* Zero: the client's choice - it keeps the size it has. */
	if (w > 0 && h > 0) {
		want_w = w;
		want_h = h;
	}
}

static void
toplevel_close(void *data, struct xdg_toplevel *t)
{
	exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

/* -L: the panel's configure, the size to draw at. */
static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
	uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	configured = 1;
	if (w > 0 && h > 0) {
		want_w = (int)w;
		want_h = (int)h;
		if (want_w != cur_w || want_h != cur_h)
			resize = 1;
	}
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	exit(0);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void
frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	frames++;
	wl_callback_destroy(cb);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

/* A buffer's memory goes when the compositor lets go of the buffer. */
struct mapping {
	void *px;
	size_t size;
};

static void
buffer_release(void *data, struct wl_buffer *buf)
{
	struct mapping *m = data;

	munmap(m->px, m->size);
	free(m);
	wl_buffer_destroy(buf);
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static struct wl_buffer *
make_buffer(int w, int h)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buf;
	struct mapping *m;
	uint32_t *px;
	size_t size = (size_t)w * h * 4, i;
	int fd;

	if ((fd = memfd_create("wlhello", MFD_CLOEXEC)) < 0 ||
	    ftruncate(fd, size) != 0) {
		perror("wlhello: memfd");
		exit(1);
	}
	px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (px == MAP_FAILED || (m = malloc(sizeof(*m))) == NULL) {
		perror("wlhello: mmap");
		exit(1);
	}
	for (i = 0; i < (size_t)w * h; i++)
		px[i] = colour;
	pool = wl_shm_create_pool(shm, fd, size);
	buf = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4,
	    WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	m->px = px;
	m->size = size;
	wl_buffer_add_listener(buf, &buffer_listener, m);
	return buf;
}

static void
draw(struct wl_surface *surface)
{
	struct wl_callback *cb;

	wl_surface_attach(surface, make_buffer(cur_w, cur_h), 0, 0);
	wl_surface_damage(surface, 0, 0, cur_w, cur_h);
	cb = wl_surface_frame(surface);
	wl_callback_add_listener(cb, &frame_listener, NULL);
	wl_surface_commit(surface);
}

int
main(int argc, char **argv)
{
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct wl_surface *surface;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	struct zwlr_layer_surface_v1 *ls;
	const char *title = "wlhello", *edge = NULL;
	time_t end;
	int c, secs = 3, height = 32, quiet = 0;

	while ((c = getopt(argc, argv, "c:fqt:T:L:H:")) != -1) {
		if (c == 'c')
			colour = (uint32_t)strtoul(optarg, NULL, 0);
		else if (c == 'f')
			fill = 1;
		else if (c == 'q')
			quiet = 1;
		else if (c == 't')
			secs = atoi(optarg);
		else if (c == 'T')
			title = optarg;
		else if (c == 'L')
			edge = optarg;
		else if (c == 'H')
			height = atoi(optarg);
	}
	if (edge != NULL && ((strcmp(edge, "top") != 0 &&
	    strcmp(edge, "bottom") != 0) || height < 1)) {
		fprintf(stderr, "wlhello: -L top|bottom, -H height\n");
		return 2;
	}

	if ((dpy = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "wlhello: no compositor (%s)\n",
		    strerror(errno));
		return 1;
	}
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	if (!compositor || !shm || !wm_base) {
		fprintf(stderr, "wlhello: compositor %p shm %p xdg %p\n",
		    (void *)compositor, (void *)shm, (void *)wm_base);
		return 1;
	}
	xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);

	surface = wl_compositor_create_surface(compositor);
	if (edge != NULL) {
		if (layer_shell == NULL) {
			fprintf(stderr, "wlhello: the compositor has no layer "
			    "shell\n");
			return 1;
		}
		ls = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface,
		    NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wlhello");
		zwlr_layer_surface_v1_add_listener(ls, &layer_listener, NULL);
		zwlr_layer_surface_v1_set_size(ls, 0, (uint32_t)height);
		zwlr_layer_surface_v1_set_anchor(ls,
		    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
		    (strcmp(edge, "top") == 0 ?
		    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP :
		    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM));
		zwlr_layer_surface_v1_set_exclusive_zone(ls, height);
	} else {
		xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
		xdg_surface_add_listener(xs, &surface_listener, NULL);
		top = xdg_surface_get_toplevel(xs);
		xdg_toplevel_add_listener(top, &toplevel_listener, NULL);
		xdg_toplevel_set_title(top, title);
		xdg_toplevel_set_app_id(top, "wlhello");
	}
	wl_surface_commit(surface);

	while (!configured && wl_display_dispatch(dpy) != -1)
		;

	if (resize) {
		cur_w = want_w;
		cur_h = want_h;
		resize = 0;
	}
	draw(surface);

	while (frames == 0 && wl_display_dispatch(dpy) != -1)
		;
	printf("WLHELLO drawn %dx%d colour 0x%08x frames %d\n", cur_w, cur_h,
	    (unsigned)colour, frames);
	fflush(stdout);

	end = time(NULL) + (secs < 0 ? 0 : secs);
	while (quiet || time(NULL) < end) {
		struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };

		if (resize) {
			cur_w = want_w;
			cur_h = want_h;
			resize = 0;
			draw(surface);
			printf("WLHELLO resized %dx%d\n", cur_w, cur_h);
			fflush(stdout);
		}
		wl_display_flush(dpy);
		if (poll(&p, 1, quiet ? -1 : 200) > 0 &&
		    wl_display_dispatch(dpy) == -1)
			break;
	}
	wl_display_disconnect(dpy);
	return 0;
}
