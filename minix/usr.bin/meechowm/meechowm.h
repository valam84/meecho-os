/*
 * meechowm: the compositor of stage 11.4, tinywl (wlroots, CC0) with
 * MEECHO's screen and input.  The parts that are MEECHO's own:
 *
 *   fbout.c      the screen: the frame buffer driver's scanout buffers
 *                (FBIOC_MAP), what was drawn copied into the one not
 *                shown, and shown (FBIOC_FLIP); the cursor in the display
 *                controller's own window (FBIOC_CURSOR)
 *   input.c      the keyboard and the mouse: the input server's devices
 *                (/dev/kbdmux, /dev/mousemux) as wlroots input devices
 *   hid2evdev.c  HID keyboard usages to evdev key codes
 *   render.c     who draws: the GPU (GLES2 over Mesa and the mali
 *                service) or the CPU (pixman); a frame read by the CPU
 *
 * wlroots itself draws into the buffers of a headless output the size of
 * the screen; fbout.c takes each frame from there.
 */
#ifndef MEECHOWM_H
#define MEECHOWM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pixman.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_buffer;
struct wlr_input_device;
struct wlr_output;
struct wlr_renderer;
struct wl_event_loop;

/* render.c */
bool mc_render_create(struct wlr_backend *backend, struct wlr_renderer **r,
	struct wlr_allocator **a);
/* A frame's pixels for the CPU, a wlr_buffer of memory or a dma-buf. */
bool mc_buffer_begin(struct wlr_buffer *buffer, void **data,
	uint32_t *format, size_t *stride);
void mc_buffer_end(struct wlr_buffer *buffer);
uint64_t mc_now_ns(void);
/* What the frames cost, for "meechowm -m frames". */
struct mc_frame_stats {
	const char *renderer;	/* "gles2" or "pixman" */
	const char *path;	/* to the screen: "copy" or "direct" */
	unsigned long built;	/* frames wlroots drew */
	unsigned long shown;	/* ... and flipped onto the screen */
	uint64_t render_ns;	/* drawing: wlr_scene_output_build_state() */
	uint64_t sync_ns;	/* waiting for the GPU to finish a frame */
	uint64_t copy_ns;	/* copying into a scanout buffer */
	uint64_t flip_ns;	/* FBIOC_FLIP */
	uint64_t copied;	/* bytes copied */
};
extern struct mc_frame_stats mc_stats;

/* fbout.c */
struct mc_fb;
struct mc_fb *mc_fb_open(const char *path);
void mc_fb_size(const struct mc_fb *fb, int *width, int *height);
unsigned mc_fb_hz(const struct mc_fb *fb);	/* 0: not known */
struct fb_modes;
int mc_fb_modes(struct mc_fb *fb, struct fb_modes *modes);
int mc_fb_set_mode(struct mc_fb *fb, unsigned width, unsigned height,
	unsigned hz);
/* damage: what changed in the frame, NULL for all of it */
void mc_fb_present(struct mc_fb *fb, struct wlr_buffer *buffer,
	const pixman_region32_t *damage);
/* The cursor in the controller's window: has the driver one; the output's
 * hardware cursor to it (-1: it does not fit, draw it into the frames). */
bool mc_fb_has_cursor(const struct mc_fb *fb);
int mc_fb_cursor_sync(struct mc_fb *fb, struct wlr_output *output);
/* With GLES2: the GPU draws into the scanout buffers themselves - NULL if
 * it cannot, and the frames are copied (inner stays the caller's). */
struct wlr_allocator *mc_fb_scanout_allocator(struct mc_fb *fb,
	struct wlr_allocator *inner);
void mc_fb_close(struct mc_fb *fb);

/* input.c */
typedef void (*mc_new_input_fn)(void *data, struct wlr_input_device *dev);
bool mc_input_start(struct wl_event_loop *loop, mc_new_input_fn new_input,
	void *data);

/* hid2evdev.c */
uint32_t mc_hid_to_evdev(unsigned usage);

#endif
