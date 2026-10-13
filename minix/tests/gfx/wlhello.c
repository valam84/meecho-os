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
 *   wlhello [-c 0xAARRGGBB] [-t seconds]
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

#define W	256
#define H	256

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static int configured, frames;
static uint32_t colour = 0xff3060c0;

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
}

static const struct xdg_surface_listener surface_listener = {
	.configure = surface_configure,
};

static void
toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
	struct wl_array *states)
{
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

static void
frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	frames++;
	wl_callback_destroy(cb);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static struct wl_buffer *
make_buffer(void)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buf;
	uint32_t *px;
	size_t size = W * H * 4, i;
	int fd;

	if ((fd = memfd_create("wlhello", MFD_CLOEXEC)) < 0 ||
	    ftruncate(fd, size) != 0) {
		perror("wlhello: memfd");
		exit(1);
	}
	px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (px == MAP_FAILED) {
		perror("wlhello: mmap");
		exit(1);
	}
	for (i = 0; i < W * H; i++)
		px[i] = colour;
	pool = wl_shm_create_pool(shm, fd, size);
	buf = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4,
	    WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buf;
}

int
main(int argc, char **argv)
{
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct wl_surface *surface;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	struct wl_callback *cb;
	time_t end;
	int c, secs = 3;

	while ((c = getopt(argc, argv, "c:t:")) != -1) {
		if (c == 'c')
			colour = (uint32_t)strtoul(optarg, NULL, 0);
		else if (c == 't')
			secs = atoi(optarg);
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
	xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xs, &surface_listener, NULL);
	top = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(top, &toplevel_listener, NULL);
	xdg_toplevel_set_title(top, "wlhello");
	wl_surface_commit(surface);

	while (!configured && wl_display_dispatch(dpy) != -1)
		;

	wl_surface_attach(surface, make_buffer(), 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	cb = wl_surface_frame(surface);
	wl_callback_add_listener(cb, &frame_listener, NULL);
	wl_surface_commit(surface);

	while (frames == 0 && wl_display_dispatch(dpy) != -1)
		;
	printf("WLHELLO drawn %dx%d colour 0x%08x frames %d\n", W, H,
	    (unsigned)colour, frames);
	fflush(stdout);

	end = time(NULL) + (secs < 0 ? 0 : secs);
	while (time(NULL) < end) {
		struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };

		wl_display_flush(dpy);
		if (poll(&p, 1, 200) > 0 && wl_display_dispatch(dpy) == -1)
			break;
	}
	wl_display_disconnect(dpy);
	return 0;
}
