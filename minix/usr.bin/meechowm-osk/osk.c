/*
 * meechowm-osk: the on-screen keyboard of the desktop (stage 11.9).
 *
 * libosk's keyboard (<minix/libosk.h>: its layers, what a touch does, the
 * modifiers that stick) on a layer surface along the bottom of the screen,
 * whose height the strip gives up while it is there (its exclusive zone),
 * so that no window goes under it.  It never takes the keyboard focus: the
 * window being typed into keeps it.
 *
 * Up and down: the program is meechowm's input method (input-method-v2),
 * and it is up while that is active - which the compositor says while the
 * focused window has a text field that wants text (foot does, through
 * text-input-v3), or when it is asked for it (Super+t, "meechowm -m osk",
 * the palette; textinput.c).
 *
 * Keys: libosk gives HID usages, a US keyboard's, as a USB keyboard gives
 * them; they go out through a virtual keyboard (virtual-keyboard-v1) as
 * evdev codes - meechowm's own table, hid2evdev.c - with the US keymap
 * that the compositor gives its other keyboards, and the modifiers that
 * keymap makes of them.  So a window cannot tell the keys on the screen
 * from a keyboard's.
 *
 * Touches and the pointer both work it: a touch is libosk's touch, the
 * left button a touch of its own.  A pointer that leaves the keyboard with
 * the button down lets its key go.
 *
 *   meechowm-osk [-v]
 *
 * -v: what it is told and what it sends, a line each, on stdout:
 * "osk up", "osk down", "key USAGE CODE down|up", "mods D L K G".
 */
#include <sys/mman.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <minix/libosk.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "osk.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define BTN_LEFT	0x110
#define PTR_SLOT	(OSK_MAX_TOUCH - 1)	/* the pointer's touch */

static struct wl_display *dpy;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_output *output;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zwp_virtual_keyboard_manager_v1 *vk_mgr;
static struct zwp_input_method_manager_v2 *im_mgr;

static struct zwp_virtual_keyboard_v1 *vk;
static struct zwp_input_method_v2 *im;
static struct wl_pointer *pointer;
static struct wl_touch *touch;

static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static int width, height, configured;
static int out_h;			/* the screen's height */
static bool active_pending, shown;
static int verbose;
static volatile sig_atomic_t done;

static struct osk kb;
static double ptr_x, ptr_y;
static bool ptr_down;
static int32_t touch_id[PTR_SLOT];	/* the touch in each slot, or -1 */

static struct xkb_state *xkb;
static xkb_mod_mask_t sent[4];		/* the modifiers last sent */

static uint32_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*===========================================================================*
 *    keys out                                                               *
 *===========================================================================*/
/* libosk's key, to the virtual keyboard, and the modifiers it makes. */
static void
emit(void *arg, unsigned usage, int down)
{
	uint32_t code = mc_hid_to_evdev(usage);
	xkb_mod_mask_t m[4];

	if (code == 0 || vk == NULL)
		return;
	zwp_virtual_keyboard_v1_key(vk, now_ms(), code, down ?
	    WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	if (verbose)
		printf("key 0x%02x %u %s\n", usage, code, down ? "down" : "up");
	if (xkb == NULL)
		return;
	xkb_state_update_key(xkb, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
	m[0] = xkb_state_serialize_mods(xkb, XKB_STATE_MODS_DEPRESSED);
	m[1] = xkb_state_serialize_mods(xkb, XKB_STATE_MODS_LATCHED);
	m[2] = xkb_state_serialize_mods(xkb, XKB_STATE_MODS_LOCKED);
	m[3] = xkb_state_serialize_layout(xkb, XKB_STATE_LAYOUT_EFFECTIVE);
	if (memcmp(m, sent, sizeof(m)) == 0)
		return;
	memcpy(sent, m, sizeof(m));
	zwp_virtual_keyboard_v1_modifiers(vk, m[0], m[1], m[2], m[3]);
	if (verbose)
		printf("mods %u %u %u %u\n", m[0], m[1], m[2], m[3]);
}

/*
 * The US keymap, as the compositor makes it for its own keyboards
 * (xkbcommon's defaults, XKB_CONFIG_ROOT), handed to the virtual keyboard
 * as text in a memory file.
 */
static int
keymap(void)
{
	struct xkb_context *ctx;
	struct xkb_keymap *map;
	char *text;
	void *p;
	size_t size;
	int fd;

	if ((ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS)) == NULL ||
	    (map = xkb_keymap_new_from_names(ctx, NULL,
	    XKB_KEYMAP_COMPILE_NO_FLAGS)) == NULL) {
		fprintf(stderr, "meechowm-osk: no keymap (XKB_CONFIG_ROOT?)\n");
		return -1;
	}
	text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
	size = strlen(text) + 1;
	/* Into the memory file through a mapping: MEECHO's memfd has no
	 * read(2) or write(2) (servers/vfs/memfd.c). */
	if ((fd = memfd_create("meechowm-osk-keymap", MFD_CLOEXEC)) < 0 ||
	    ftruncate(fd, (off_t)size) != 0 ||
	    (p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0)) == MAP_FAILED) {
		perror("meechowm-osk: keymap");
		return -1;
	}
	memcpy(p, text, size);
	munmap(p, size);
	zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
	    (uint32_t)size);
	close(fd);
	free(text);
	xkb = xkb_state_new(map);
	xkb_keymap_unref(map);
	xkb_context_unref(ctx);
	return 0;
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
		if ((fd = memfd_create("meechowm-osk", MFD_CLOEXEC)) < 0 ||
		    ftruncate(fd, (off_t)b->size) != 0) {
			perror("meechowm-osk: memfd");
			exit(1);
		}
		b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, 0);
		if (b->px == MAP_FAILED) {
			perror("meechowm-osk: mmap");
			exit(1);
		}
		pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
		b->wl = wl_shm_pool_create_buffer(pool, 0, width, height,
		    width * 4, WL_SHM_FORMAT_XRGB8888);
		wl_shm_pool_destroy(pool);
		close(fd);
		wl_buffer_add_listener(b->wl, &buffer_listener, b);
		b->w = width;
		b->h = height;
	}
	return b;
}

/* Is it worth a frame: the next one, once a buffer comes back. */
static bool dirty;

static void
redraw(void)
{
	struct osk_view v;
	struct buffer *b;

	dirty = true;
	if (!shown || !configured || width <= 0 || height <= 0 ||
	    (b = get_buffer()) == NULL)
		return;
	dirty = false;
	osk_view(&kb, &v);
	kb_draw(b->px, width, height, width * 4, &v);
	b->busy = true;
	wl_surface_attach(surface, b->wl, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, width, height);
	wl_surface_commit(surface);
}

/* The keyboard's height on a screen h high: libosk's, whole rows. */
static int
kb_height(void)
{
	return (int)osk_console_height(out_h > 0 ? (unsigned)out_h : 720);
}

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
	uint32_t serial, uint32_t w, uint32_t h)
{
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	width = (int)w;
	height = (int)h;
	configured = 1;
	osk_resize(&kb, w, h);
	redraw();
}

static void hide(void);

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	hide();		/* until the input method is active again */
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void
show(void)
{
	int h = kb_height();

	if (shown)
		return;
	shown = true;
	configured = 0;
	surface = wl_compositor_create_surface(compositor);
	layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface,
	    NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "osk");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(layer,
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(layer, 0, (uint32_t)h);
	zwlr_layer_surface_v1_set_exclusive_zone(layer, h);
	zwlr_layer_surface_v1_set_keyboard_interactivity(layer,
	    ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(surface);
	if (verbose)
		printf("osk up %d\n", h);
}

static void
hide(void)
{
	if (!shown)
		return;
	/* Nothing stays down behind a keyboard that is gone. */
	osk_reset(&kb);
	ptr_down = false;
	memset(touch_id, 0xff, sizeof(touch_id));
	zwlr_layer_surface_v1_destroy(layer);
	wl_surface_destroy(surface);
	layer = NULL;
	surface = NULL;
	shown = false;
	configured = 0;
	if (verbose)
		printf("osk down\n");
}

/*===========================================================================*
 *    the input method: up and down                                          *
 *===========================================================================*/
static void
im_activate(void *data, struct zwp_input_method_v2 *m)
{
	active_pending = true;
}

static void
im_deactivate(void *data, struct zwp_input_method_v2 *m)
{
	active_pending = false;
}

static void
im_surrounding_text(void *data, struct zwp_input_method_v2 *m,
	const char *text, uint32_t cursor, uint32_t anchor)
{
}

static void
im_text_change_cause(void *data, struct zwp_input_method_v2 *m,
	uint32_t cause)
{
}

static void
im_content_type(void *data, struct zwp_input_method_v2 *m, uint32_t hint,
	uint32_t purpose)
{
}

static void
im_done(void *data, struct zwp_input_method_v2 *m)
{
	if (active_pending)
		show();
	else
		hide();
	fflush(stdout);
}

static void
im_unavailable(void *data, struct zwp_input_method_v2 *m)
{
	fprintf(stderr, "meechowm-osk: the seat has an input method already\n");
	done = 1;
}

static const struct zwp_input_method_v2_listener im_listener = {
	.activate = im_activate,
	.deactivate = im_deactivate,
	.surrounding_text = im_surrounding_text,
	.text_change_cause = im_text_change_cause,
	.content_type = im_content_type,
	.done = im_done,
	.unavailable = im_unavailable,
};

/*===========================================================================*
 *    touches and the pointer in                                             *
 *===========================================================================*/
static void
touched(unsigned slot, int event, double x, double y)
{
	if (osk_touch(&kb, slot, event, x < 0 ? 0 : (unsigned)x,
	    y < 0 ? 0 : (unsigned)y))
		redraw();
	fflush(stdout);
}

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
	if (ptr_down) {
		ptr_down = false;
		if (osk_lost(&kb, PTR_SLOT))
			redraw();
	}
}

static void
ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	wl_fixed_t y)
{
	ptr_x = wl_fixed_to_double(x);
	ptr_y = wl_fixed_to_double(y);
	if (ptr_down)
		touched(PTR_SLOT, OSK_TOUCH_CONTACT, ptr_x, ptr_y);
}

static void
ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	if (button != BTN_LEFT || !shown)
		return;
	ptr_down = state == WL_POINTER_BUTTON_STATE_PRESSED;
	touched(PTR_SLOT, ptr_down ? OSK_TOUCH_DOWN : OSK_TOUCH_UP, ptr_x,
	    ptr_y);
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

/* Every event of the version bound (wl_seat 5): libwayland aborts the
 * client on an event whose listener is NULL. */
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

/* The slot of touch id; a new one takes a free slot (-1: none free). */
static int
slot_of(int32_t id, bool new)
{
	int i;

	for (i = 0; i < PTR_SLOT; i++)
		if (touch_id[i] == id)
			return i;
	if (!new)
		return -1;
	for (i = 0; i < PTR_SLOT; i++)
		if (touch_id[i] == -1) {
			touch_id[i] = id;
			return i;
		}
	return -1;
}

static void
touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
	struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	int slot = slot_of(id, true);

	if (slot >= 0 && shown)
		touched((unsigned)slot, OSK_TOUCH_DOWN, wl_fixed_to_double(x),
		    wl_fixed_to_double(y));
}

static void
touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
	int32_t id)
{
	int slot = slot_of(id, false);

	if (slot < 0)
		return;
	touch_id[slot] = -1;
	touched((unsigned)slot, OSK_TOUCH_UP, 0, 0);
}

static void
touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id,
	wl_fixed_t x, wl_fixed_t y)
{
	int slot = slot_of(id, false);

	if (slot >= 0)
		touched((unsigned)slot, OSK_TOUCH_CONTACT,
		    wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void
touch_frame(void *data, struct wl_touch *t)
{
}

/* The compositor took the touches for itself: every one is lost. */
static void
touch_cancel(void *data, struct wl_touch *t)
{
	int i, changed = 0;

	for (i = 0; i < PTR_SLOT; i++)
		if (touch_id[i] != -1) {
			touch_id[i] = -1;
			changed |= osk_lost(&kb, (unsigned)i);
		}
	if (changed)
		redraw();
}

static const struct wl_touch_listener touch_listener = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = touch_frame,
	.cancel = touch_cancel,
};

static void
seat_caps(void *data, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && pointer == NULL) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && touch == NULL) {
		touch = wl_seat_get_touch(s);
		wl_touch_add_listener(touch, &touch_listener, NULL);
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

/*===========================================================================*
 *    the screen                                                             *
 *===========================================================================*/
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
	if (!(flags & WL_OUTPUT_MODE_CURRENT) || h == out_h)
		return;
	out_h = h;
	/* Another mode, the keyboard's height with it. */
	if (shown) {
		zwlr_layer_surface_v1_set_size(layer, 0, (uint32_t)kb_height());
		zwlr_layer_surface_v1_set_exclusive_zone(layer, kb_height());
		wl_surface_commit(surface);
	}
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
		output = wl_registry_bind(reg, name, &wl_output_interface, 2);
		wl_output_add_listener(output, &output_listener, NULL);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0)
		layer_shell = wl_registry_bind(reg, name,
		    &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
	else if (strcmp(iface,
	    zwp_virtual_keyboard_manager_v1_interface.name) == 0)
		vk_mgr = wl_registry_bind(reg, name,
		    &zwp_virtual_keyboard_manager_v1_interface, 1);
	else if (strcmp(iface, zwp_input_method_manager_v2_interface.name) == 0)
		im_mgr = wl_registry_bind(reg, name,
		    &zwp_input_method_manager_v2_interface, 1);
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

int
main(int argc, char **argv)
{
	struct sigaction sa;
	struct wl_registry *reg;
	struct pollfd pfd;
	int c;

	while ((c = getopt(argc, argv, "v")) != -1) {
		if (c == 'v')
			verbose = 1;
		else {
			fprintf(stderr, "usage: meechowm-osk [-v]\n");
			return 2;
		}
	}
	memset(touch_id, 0xff, sizeof(touch_id));
	osk_init(&kb, 1, 1, emit, NULL);

	/* Without SA_RESTART: a signal ends the wait in poll(). */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	if ((dpy = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "meechowm-osk: no compositor (%s)\n",
		    strerror(errno));
		return 1;
	}
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	if (compositor == NULL || shm == NULL || seat == NULL ||
	    layer_shell == NULL || vk_mgr == NULL || im_mgr == NULL) {
		fprintf(stderr, "meechowm-osk: the compositor has no %s\n",
		    layer_shell == NULL ? "layer shell" : vk_mgr == NULL ?
		    "virtual keyboard" : im_mgr == NULL ? "input method" :
		    "seat or shm");
		return 1;
	}
	vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vk_mgr,
	    seat);
	if (keymap() != 0)
		return 1;
	im = zwp_input_method_manager_v2_get_input_method(im_mgr, seat);
	zwp_input_method_v2_add_listener(im, &im_listener, NULL);
	wl_display_roundtrip(dpy);

	while (!done) {
		if (dirty)
			redraw();
		while (wl_display_prepare_read(dpy) != 0)
			wl_display_dispatch_pending(dpy);
		fflush(stdout);
		wl_display_flush(dpy);
		pfd.fd = wl_display_get_fd(dpy);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, -1) > 0 && (pfd.revents & POLLIN))
			wl_display_read_events(dpy);
		else
			wl_display_cancel_read(dpy);
		if ((pfd.revents & (POLLHUP | POLLERR)) ||
		    wl_display_dispatch_pending(dpy) < 0)
			break;
	}
	hide();
	zwp_input_method_v2_destroy(im);
	zwp_virtual_keyboard_v1_destroy(vk);
	wl_display_roundtrip(dpy);
	wl_display_disconnect(dpy);
	return 0;
}
