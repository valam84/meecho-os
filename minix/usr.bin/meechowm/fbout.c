/*
 * The screen: the frame buffer driver's scanout buffers.
 *
 * FBIOC_MAP maps the driver's buffers - physically contiguous memory the
 * display controller fetches by address - into this process, and makes it
 * the owner of the screen until it closes the device.  Each frame wlroots
 * has drawn into the buffer of its headless output is copied into a
 * scanout buffer that is not on the screen, and FBIOC_FLIP shows that one:
 * the driver cleans the rows out of the cache and returns once the
 * controller has taken the new address at a frame start, so the frame on
 * the screen is never the one being written.
 *
 * Only what changed is copied.  A scanout buffer misses every change made
 * while another one was being shown, so each keeps its own region of what
 * it lacks: a frame's damage goes into all of them, and the buffer about
 * to be shown is brought up to date from the frame - which is whole, so
 * any region of it may be taken - and its region emptied.
 *
 * Without the device (no display: QEMU without virtual=1) there is no
 * screen, and the compositor runs headless.
 */
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include "fb_ioc.h"
#include "meechowm.h"

struct mc_fb {
	int fd;
	struct fb_map map;
	uint32_t *buf[FB_MAX_BUFFERS];
	pixman_region32_t lacks[FB_MAX_BUFFERS];
	uint32_t shown;
	unsigned long frames, flips;
};

struct mc_fb *
mc_fb_open(const char *path)
{
	struct mc_fb *fb;
	uint32_t i;

	if ((fb = calloc(1, sizeof(*fb))) == NULL)
		return NULL;
	if ((fb->fd = open(path, O_RDWR | O_CLOEXEC)) < 0) {
		wlr_log_errno(WLR_ERROR, "no screen: %s", path);
		free(fb);
		return NULL;
	}
	if (ioctl(fb->fd, FBIOC_MAP, &fb->map) != 0) {
		wlr_log_errno(WLR_ERROR, "%s: FBIOC_MAP", path);
		close(fb->fd);
		free(fb);
		return NULL;
	}
	if (fb->map.format != FB_FORMAT_XRGB8888 || fb->map.nbuf < 2) {
		wlr_log(WLR_ERROR, "%s: format 0x%08x, %u buffer(s): not "
		    "usable", path, fb->map.format, fb->map.nbuf);
		close(fb->fd);
		free(fb);
		return NULL;
	}
	fb->shown = fb->map.shown;
	for (i = 0; i < fb->map.nbuf; i++) {
		fb->buf[i] = (uint32_t *)(uintptr_t)fb->map.addr[i];
		/* Everything is missing from every buffer at first. */
		pixman_region32_init_rect(&fb->lacks[i], 0, 0,
		    fb->map.width, fb->map.height);
	}
	wlr_log(WLR_INFO, "screen %s: %ux%u, stride %u, %u scanout buffers",
	    path, fb->map.width, fb->map.height, fb->map.stride,
	    fb->map.nbuf);
	return fb;
}

void
mc_fb_size(const struct mc_fb *fb, int *width, int *height)
{
	*width = (int)fb->map.width;
	*height = (int)fb->map.height;
}

void
mc_fb_present(struct mc_fb *fb, struct wlr_buffer *buffer,
	const pixman_region32_t *damage)
{
	struct fb_flip flip;
	pixman_box32_t *box, ext;
	uint32_t next, format, i;
	size_t stride, w;
	void *data;
	int n, y;

	if (!wlr_buffer_begin_data_ptr_access(buffer,
	    WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format, &stride))
		return;
	if (buffer->width != (int)fb->map.width ||
	    buffer->height != (int)fb->map.height) {
		wlr_log(WLR_ERROR, "a frame of %dx%d for a screen of %ux%u",
		    buffer->width, buffer->height, fb->map.width,
		    fb->map.height);
		wlr_buffer_end_data_ptr_access(buffer);
		return;
	}
	if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888) {
		wlr_log(WLR_ERROR, "a frame in format 0x%08x; the screen takes "
		    "XRGB8888", format);
		wlr_buffer_end_data_ptr_access(buffer);
		return;
	}

	for (i = 0; i < fb->map.nbuf; i++) {
		if (damage != NULL)
			pixman_region32_union(&fb->lacks[i], &fb->lacks[i],
			    damage);
		else
			pixman_region32_union_rect(&fb->lacks[i], &fb->lacks[i],
			    0, 0, fb->map.width, fb->map.height);
	}

	next = (fb->shown + 1) % fb->map.nbuf;
	pixman_region32_intersect_rect(&fb->lacks[next], &fb->lacks[next], 0, 0,
	    fb->map.width, fb->map.height);
	fb->frames++;
	/*
	 * Nothing new for the buffer to be shown: the screen stays as it is.
	 * wlroots hands over such frames - a client's commit without damage
	 * still makes one, for its frame callback - and a flip of nothing
	 * would cost a frame's wait (and, with no rows named, a cleaning of
	 * the whole buffer out of the cache in the driver).
	 */
	if (!pixman_region32_not_empty(&fb->lacks[next])) {
		wlr_buffer_end_data_ptr_access(buffer);
		return;
	}
	box = pixman_region32_rectangles(&fb->lacks[next], &n);
	for (; n > 0; n--, box++) {
		w = (size_t)(box->x2 - box->x1) * 4;
		for (y = box->y1; y < box->y2; y++)
			memcpy((char *)fb->buf[next] + (size_t)y * fb->map.stride +
			    (size_t)box->x1 * 4,
			    (char *)data + (size_t)y * stride +
			    (size_t)box->x1 * 4, w);
	}
	ext = *pixman_region32_extents(&fb->lacks[next]);
	wlr_log(WLR_DEBUG, "frame %lu: buffer %u, format 0x%08x stride %zu, "
	    "rows %d..%d, columns %d..%d", fb->frames, next, format, stride,
	    ext.y1, ext.y2, ext.x1, ext.x2);
	pixman_region32_clear(&fb->lacks[next]);
	wlr_buffer_end_data_ptr_access(buffer);

	memset(&flip, 0, sizeof(flip));
	flip.buf = next;
	flip.y0 = ext.y1 > 0 ? (uint32_t)ext.y1 : 0;
	flip.y1 = ext.y2 > ext.y1 ? (uint32_t)ext.y2 : 0;
	if (ioctl(fb->fd, FBIOC_FLIP, &flip) != 0) {
		wlr_log_errno(WLR_ERROR, "FBIOC_FLIP to buffer %u", next);
		return;
	}
	fb->shown = next;
	fb->flips++;
}

void
mc_fb_close(struct mc_fb *fb)
{
	uint32_t i;

	if (fb == NULL)
		return;
	wlr_log(WLR_INFO, "screen: %lu frame(s), %lu flip(s)", fb->frames,
	    fb->flips);
	for (i = 0; i < fb->map.nbuf; i++)
		pixman_region32_fini(&fb->lacks[i]);
	close(fb->fd);		/* the driver gives the console the screen */
	free(fb);
}
