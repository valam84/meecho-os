/*
 * meechowm: the compositor of stage 11.4, tinywl (wlroots, CC0) with
 * MEECHO's screen and input.  The parts that are MEECHO's own:
 *
 *   fbout.c      the screen: the frame buffer driver's scanout buffers
 *                (FBIOC_MAP), what was drawn copied into the one not
 *                shown, and shown (FBIOC_FLIP)
 *   input.c      the keyboard and the mouse: the input server's devices
 *                (/dev/kbdmux, /dev/mousemux) as wlroots input devices
 *   hid2evdev.c  HID keyboard usages to evdev key codes
 *
 * wlroots itself draws with pixman into the buffers of a headless output
 * the size of the screen; fbout.c takes each frame from there.
 */
#ifndef MEECHOWM_H
#define MEECHOWM_H

#include <stdbool.h>
#include <stdint.h>

#include <pixman.h>

struct wlr_buffer;
struct wlr_input_device;
struct wl_event_loop;

/* fbout.c */
struct mc_fb;
struct mc_fb *mc_fb_open(const char *path);
void mc_fb_size(const struct mc_fb *fb, int *width, int *height);
/* damage: what changed in the frame, NULL for all of it */
void mc_fb_present(struct mc_fb *fb, struct wlr_buffer *buffer,
	const pixman_region32_t *damage);
void mc_fb_close(struct mc_fb *fb);

/* input.c */
typedef void (*mc_new_input_fn)(void *data, struct wlr_input_device *dev);
bool mc_input_start(struct wl_event_loop *loop, mc_new_input_fn new_input,
	void *data);

/* hid2evdev.c */
uint32_t mc_hid_to_evdev(unsigned usage);

#endif
