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
 * the screen is never the one being written.  A frame the GPU drew is a
 * dma-buf, read through its mapping once the GPU is done (render.c).
 *
 * Only what changed is copied.  A scanout buffer misses every change made
 * while another one was being shown, so each keeps its own region of what
 * it lacks: a frame's damage goes into all of them, and the buffer about
 * to be shown is brought up to date from the frame - which is whole, so
 * any region of it may be taken - and its region emptied.
 *
 * Without the device (no display: QEMU without virtual=1) there is no
 * screen, and the compositor runs headless.
 *
 * The cursor is the display controller's own window over the frame
 * (FBIOC_CURSOR_MAP, FBIOC_CURSOR) when the driver has one: wlroots then
 * takes it for a hardware cursor and leaves it out of the frames, and a
 * move costs one request instead of a frame drawn, copied, cleaned and
 * flipped.  mc_fb_cursor_sync() hands the driver what wlroots last set.
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
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "fb_ioc.h"
#include "meechowm.h"

struct mc_fb {
	int fd;
	struct fb_map map;
	uint32_t hz;			/* the mode's rate; 0 if not known */
	uint32_t *buf[FB_MAX_BUFFERS];
	pixman_region32_t lacks[FB_MAX_BUFFERS];
	uint32_t shown;
	unsigned long frames, flips;

	/* The cursor's window, if the driver has one (cmap.nbuf > 0). */
	struct fb_cursor_map cmap;
	struct fb_cursor cur;		/* what the driver was told last */
	uint32_t img[FB_CURSOR_MAX * FB_CURSOR_MAX];	/* the picture in cur.buf */
	unsigned long cursor_updates;
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
	{
		/* The rate, for the output's mode; a driver without the
		 * list (before mode changes) leaves it unknown. */
		struct fb_modes ms;

		if (ioctl(fb->fd, FBIOC_MODES, &ms) == 0 && ms.current < ms.n)
			fb->hz = ms.mode[ms.current].hz;
	}
	/*
	 * The cursor's window.  A driver without one (ENODEV), or before it
	 * (ENOTTY), leaves the cursor to be drawn into the frames.
	 */
	if (ioctl(fb->fd, FBIOC_CURSOR_MAP, &fb->cmap) != 0 ||
	    fb->cmap.format != FB_FORMAT_ARGB8888 || fb->cmap.nbuf == 0 ||
	    fb->cmap.max_w > FB_CURSOR_MAX || fb->cmap.max_h > FB_CURSOR_MAX) {
		memset(&fb->cmap, 0, sizeof(fb->cmap));
		wlr_log(WLR_INFO, "screen %s: no cursor window, the cursor is "
		    "drawn into the frames", path);
	}
	wlr_log(WLR_INFO, "screen %s: %ux%u, stride %u, %u scanout buffers, "
	    "cursor window %ux%u", path, fb->map.width, fb->map.height,
	    fb->map.stride, fb->map.nbuf, fb->cmap.max_w, fb->cmap.max_h);
	return fb;
}

bool
mc_fb_has_cursor(const struct mc_fb *fb)
{
	return fb != NULL && fb->cmap.nbuf > 0;
}

/*
 * The cursor wlroots has set on the output - the hardware cursor, which it
 * leaves out of the frames - to the driver's window: hidden, moved, or with
 * a new picture, whatever changed since the last call.  Called on every
 * frame; wlroots asks for one whenever it sets or moves the cursor.
 *
 * The picture is compared, not only its buffer: wlroots draws each new one
 * into the next buffer of a small swapchain, and two changes in one frame
 * bring the same buffer back with other pixels.  A new picture goes into
 * the driver's other buffer - the one named last is read until the next
 * frame start.  Returns -1 if the picture does not fit the window: the
 * caller draws the cursor into the frames from then on.
 */
int
mc_fb_cursor_sync(struct mc_fb *fb, struct wlr_output *output)
{
	struct wlr_output_cursor *oc = output->hardware_cursor;
	struct wlr_buffer *b = output->cursor_front_buffer;
	struct fb_cursor c;
	uint32_t format, *dst;
	size_t stride;
	void *data;
	int y, image = 0;

	if (!mc_fb_has_cursor(fb))
		return -1;
	memset(&c, 0, sizeof(c));
	c.buf = fb->cur.buf;
	if (oc != NULL && b != NULL && oc->enabled &&
	    output->software_cursor_locks == 0) {
		if (b->width > (int)fb->cmap.max_w ||
		    b->height > (int)fb->cmap.max_h) {
			wlr_log(WLR_INFO, "a cursor of %dx%d does not fit the "
			    "window (%ux%u): drawn into the frames", b->width,
			    b->height, fb->cmap.max_w, fb->cmap.max_h);
			return -1;
		}
		if (!mc_buffer_begin(b, &data, &format, &stride))
			return 0;	/* the next frame, then */
		if (format != DRM_FORMAT_ARGB8888) {
			mc_buffer_end(b);
			wlr_log(WLR_ERROR, "a cursor in format 0x%08x: drawn "
			    "into the frames", format);
			return -1;
		}
		c.flags = FB_CURSOR_SHOW;
		c.w = (uint32_t)b->width;
		c.h = (uint32_t)b->height;
		if (c.w != fb->cur.w || c.h != fb->cur.h ||
		    !(fb->cur.flags & FB_CURSOR_SHOW))
			image = 1;
		for (y = 0; y < b->height && !image; y++)
			image = memcmp((char *)data + (size_t)y * stride,
			    fb->img + (size_t)y * c.w, c.w * 4) != 0;
		if (image) {
			c.buf = (fb->cur.buf + 1) % fb->cmap.nbuf;
			dst = (uint32_t *)(uintptr_t)fb->cmap.addr[c.buf];
			for (y = 0; y < b->height; y++) {
				memcpy((char *)dst + (size_t)y *
				    fb->cmap.stride, (char *)data +
				    (size_t)y * stride, c.w * 4);
				memcpy(fb->img + (size_t)y * c.w,
				    (char *)data + (size_t)y * stride, c.w * 4);
			}
			c.flags |= FB_CURSOR_IMAGE;
		}
		mc_buffer_end(b);
		c.x = (int32_t)oc->x - oc->hotspot_x;
		c.y = (int32_t)oc->y - oc->hotspot_y;
	}
	if (!image && (c.flags & FB_CURSOR_SHOW) == (fb->cur.flags &
	    FB_CURSOR_SHOW) && (!(c.flags & FB_CURSOR_SHOW) ||
	    (c.x == fb->cur.x && c.y == fb->cur.y)))
		return 0;		/* nothing new */
	if (ioctl(fb->fd, FBIOC_CURSOR, &c) != 0) {
		wlr_log_errno(WLR_ERROR, "FBIOC_CURSOR");
		return 0;
	}
	fb->cur = c;
	fb->cur.flags &= FB_CURSOR_SHOW;
	fb->cursor_updates++;
	wlr_log(WLR_DEBUG, "hardware cursor %s at %d,%d, %ux%u%s",
	    (c.flags & FB_CURSOR_SHOW) ? "shown" : "hidden", c.x, c.y, c.w,
	    c.h, (c.flags & FB_CURSOR_IMAGE) ? ", new picture" : "");
	return 0;
}

void
mc_fb_size(const struct mc_fb *fb, int *width, int *height)
{
	*width = (int)fb->map.width;
	*height = (int)fb->map.height;
}

unsigned
mc_fb_hz(const struct mc_fb *fb)
{
	return fb->hz;
}

/* The modes the screen can be given (FBIOC_MODES); -1 and errno if not. */
int
mc_fb_modes(struct mc_fb *fb, struct fb_modes *ms)
{
	return ioctl(fb->fd, FBIOC_MODES, ms);
}

/*
 * Another mode (FBIOC_SETMODE).  The driver changes it under the buffers
 * mapped here - they are made for the largest mode of its list - and says
 * the geometry; every buffer then lacks all of the new frame.  -1 and
 * errno if the driver refused: EINVAL, not a mode of the list; EIO, the
 * hardware did not take it and the old one is back.
 */
int
mc_fb_set_mode(struct mc_fb *fb, unsigned width, unsigned height,
	unsigned hz)
{
	struct fb_setmode s;
	uint32_t i;

	memset(&s, 0, sizeof(s));
	s.width = width;
	s.height = height;
	s.hz = hz;
	if (ioctl(fb->fd, FBIOC_SETMODE, &s) != 0)
		return -1;
	fb->map.width = s.width;
	fb->map.height = s.height;
	fb->map.stride = s.stride;
	fb->hz = s.hz;
	for (i = 0; i < fb->map.nbuf; i++) {
		pixman_region32_fini(&fb->lacks[i]);
		pixman_region32_init_rect(&fb->lacks[i], 0, 0, s.width,
		    s.height);
	}
	wlr_log(WLR_INFO, "screen: %ux%u at %u Hz, stride %u", s.width,
	    s.height, s.hz, s.stride);
	return 0;
}

void
mc_fb_present(struct mc_fb *fb, struct wlr_buffer *buffer,
	const pixman_region32_t *damage)
{
	struct fb_flip flip;
	pixman_box32_t *box, ext;
	uint32_t next, format, i;
	size_t stride, w;
	uint64_t t0;
	void *data;
	int n, y;

	if (buffer->width != (int)fb->map.width ||
	    buffer->height != (int)fb->map.height) {
		wlr_log(WLR_ERROR, "a frame of %dx%d for a screen of %ux%u",
		    buffer->width, buffer->height, fb->map.width,
		    fb->map.height);
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
	 * the whole buffer out of the cache in the driver).  Asked before the
	 * frame is opened: one the GPU draws would be waited for.
	 */
	if (!pixman_region32_not_empty(&fb->lacks[next]))
		return;
	/* A frame the GPU draws: mc_buffer_begin() waits for it. */
	if (!mc_buffer_begin(buffer, &data, &format, &stride))
		return;
	if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888) {
		wlr_log(WLR_ERROR, "a frame in format 0x%08x; the screen takes "
		    "XRGB8888", format);
		mc_buffer_end(buffer);
		return;
	}
	t0 = mc_now_ns();
	box = pixman_region32_rectangles(&fb->lacks[next], &n);
	for (; n > 0; n--, box++) {
		w = (size_t)(box->x2 - box->x1) * 4;
		for (y = box->y1; y < box->y2; y++)
			memcpy((char *)fb->buf[next] + (size_t)y * fb->map.stride +
			    (size_t)box->x1 * 4,
			    (char *)data + (size_t)y * stride +
			    (size_t)box->x1 * 4, w);
		mc_stats.copied += w * (size_t)(box->y2 - box->y1);
	}
	ext = *pixman_region32_extents(&fb->lacks[next]);
	wlr_log(WLR_DEBUG, "frame %lu: buffer %u, format 0x%08x stride %zu, "
	    "rows %d..%d, columns %d..%d", fb->frames, next, format, stride,
	    ext.y1, ext.y2, ext.x1, ext.x2);
	pixman_region32_clear(&fb->lacks[next]);
	mc_buffer_end(buffer);
	mc_stats.copy_ns += mc_now_ns() - t0;

	memset(&flip, 0, sizeof(flip));
	flip.buf = next;
	flip.y0 = ext.y1 > 0 ? (uint32_t)ext.y1 : 0;
	flip.y1 = ext.y2 > ext.y1 ? (uint32_t)ext.y2 : 0;
	t0 = mc_now_ns();
	if (ioctl(fb->fd, FBIOC_FLIP, &flip) != 0) {
		wlr_log_errno(WLR_ERROR, "FBIOC_FLIP to buffer %u", next);
		return;
	}
	mc_stats.flip_ns += mc_now_ns() - t0;
	mc_stats.shown++;
	fb->shown = next;
	fb->flips++;
}

void
mc_fb_close(struct mc_fb *fb)
{
	uint32_t i;

	if (fb == NULL)
		return;
	wlr_log(WLR_INFO, "screen: %lu frame(s), %lu flip(s), %lu cursor "
	    "update(s)", fb->frames, fb->flips, fb->cursor_updates);
	for (i = 0; i < fb->map.nbuf; i++)
		pixman_region32_fini(&fb->lacks[i]);
	close(fb->fd);		/* the driver gives the console the screen */
	free(fb);
}
