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
#include <linux/dma-buf.h>
#include <xf86drm.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "fb_ioc.h"
#include "meecho_drm.h"
#include "meechowm.h"

struct scanout_buffer;

struct mc_fb {
	int fd;
	struct fb_map map;
	uint32_t hz;			/* the mode's rate; 0 if not known */
	uint32_t *buf[FB_MAX_BUFFERS];
	pixman_region32_t lacks[FB_MAX_BUFFERS];
	uint32_t shown;
	unsigned long frames, flips;

	/*
	 * The GPU drawing into the scanout buffers themselves (see
	 * mc_fb_scanout_allocator()): which wlr_buffer each buffer is, the
	 * one shown before the one shown now (not to be drawn into until the
	 * next flip has been taken), and the locks that keep both alive.
	 */
	struct scanout_buffer *as[FB_MAX_BUFFERS];
	int before;			/* -1: none */
	struct wlr_buffer *held[2];	/* shown, shown before */

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
	fb->before = -1;
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

/* ---- the GPU drawing into the scanout buffers ---------------------- */

/*
 * With GLES2 the frames need not be copied at all: the mali service takes
 * the scanout buffers themselves for GPU buffers (meecho_drm.h - the pages
 * we have mapped, if the frame buffer driver published them as its own),
 * and wlroots draws into them as into any dma-buf.  The allocator here
 * hands out the scanout buffers for frames of the screen's size, and
 * everything else (the cursor's) to the allocator it wraps.  A buffer is
 * handed out only if no live wlr_buffer is it, it is not on the screen,
 * and it was not on the screen before that - the controller may still be
 * reading it until the next frame start (fb_ioc.h, FBIOC_FLIP); with three
 * buffers that leaves one, and wlroots' swapchain cycles through three.
 * mc_fb_present() keeps the two last shown locked, so that the swapchain
 * does not draw into them, waits for the GPU and flips with FB_FLIP_GPU:
 * nothing of the frame is in the processor's caches, nothing is cleaned.
 * What each buffer lacks is wlroots' to know (its damage ring, per
 * buffer), so lacks[] is not used here.
 */
struct scanout_buffer {
	struct wlr_buffer base;
	struct mc_fb *fb;
	int index;
	struct wlr_dmabuf_attributes dmabuf;
};

struct scanout_allocator {
	struct wlr_allocator base;
	struct wlr_allocator *inner;	/* the rest: the cursor's buffers */
	struct mc_fb *fb;
	int drm;			/* a context of our own on the GPU */
};

static void
scanout_buffer_destroy(struct wlr_buffer *b)
{
	struct scanout_buffer *sb = wl_container_of(b, sb, base);

	if (sb->fb->as[sb->index] == sb)
		sb->fb->as[sb->index] = NULL;
	close(sb->dmabuf.fd[0]);
	free(sb);
}

static bool
scanout_buffer_get_dmabuf(struct wlr_buffer *b,
	struct wlr_dmabuf_attributes *attribs)
{
	struct scanout_buffer *sb = wl_container_of(b, sb, base);

	*attribs = sb->dmabuf;
	return true;
}

static const struct wlr_buffer_impl scanout_buffer_impl = {
	.destroy = scanout_buffer_destroy,
	.get_dmabuf = scanout_buffer_get_dmabuf,
};

/* Scanout buffer i as a dma-buf of the GPU service; -1 if not. */
static int
scanout_import(int drm, const struct mc_fb *fb, int i)
{
	struct drm_meecho_scanout_import a;
	volatile const char *p = (const char *)(uintptr_t)fb->map.addr[i];
	size_t off;
	int fd;

	/*
	 * The service finds the pages through our page table (sys_umap),
	 * and VM fills that in for a shared mapping only page by page, as
	 * pages are touched: a buffer never copied into has none yet, and
	 * the request was EFAULT.  One byte read per page.
	 */
	for (off = 0; off < fb->map.size; off += 4096)
		(void)p[off];
	memset(&a, 0, sizeof(a));
	a.addr = fb->map.addr[i];
	a.size = fb->map.size;
	if (drmIoctl(drm, DRM_IOCTL_MEECHO_SCANOUT_IMPORT, &a) != 0)
		return -1;
	if (drmPrimeHandleToFD(drm, a.handle, DRM_CLOEXEC | DRM_RDWR,
	    &fd) != 0)
		fd = -1;
	/* The dma-buf holds the buffer now; the handle is not needed. */
	(void)drmCloseBufferHandle(drm, a.handle);
	return fd;
}

static struct wlr_buffer *
scanout_create_buffer(struct wlr_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format)
{
	struct scanout_allocator *sa = (struct scanout_allocator *)alloc;
	struct mc_fb *fb = sa->fb;
	struct scanout_buffer *sb;
	size_t k;
	int i, fd;

	for (k = 0; k < format->len; k++)
		if (format->modifiers[k] == DRM_FORMAT_MOD_LINEAR)
			break;
	if (width != (int)fb->map.width || height != (int)fb->map.height ||
	    format->format != DRM_FORMAT_XRGB8888 || k == format->len)
		return wlr_allocator_create_buffer(sa->inner, width, height,
		    format);
	for (i = 0; i < (int)fb->map.nbuf; i++)
		if (fb->as[i] == NULL && i != (int)fb->shown && i != fb->before)
			break;
	if (i == (int)fb->map.nbuf) {
		wlr_log(WLR_ERROR, "no scanout buffer free for a frame");
		return NULL;
	}
	if ((fd = scanout_import(sa->drm, fb, i)) < 0) {
		wlr_log_errno(WLR_ERROR, "scanout buffer %d to the GPU", i);
		return NULL;
	}
	if ((sb = calloc(1, sizeof(*sb))) == NULL) {
		close(fd);
		return NULL;
	}
	wlr_buffer_init(&sb->base, &scanout_buffer_impl, width, height);
	sb->fb = fb;
	sb->index = i;
	sb->dmabuf.width = width;
	sb->dmabuf.height = height;
	sb->dmabuf.format = DRM_FORMAT_XRGB8888;
	sb->dmabuf.modifier = DRM_FORMAT_MOD_LINEAR;
	sb->dmabuf.n_planes = 1;
	sb->dmabuf.offset[0] = 0;
	sb->dmabuf.stride[0] = fb->map.stride;
	sb->dmabuf.fd[0] = fd;
	fb->as[i] = sb;
	wlr_log(WLR_DEBUG, "scanout buffer %d: a GPU frame of %dx%d", i,
	    width, height);
	return &sb->base;
}

static void
scanout_destroy(struct wlr_allocator *alloc)
{
	struct scanout_allocator *sa = (struct scanout_allocator *)alloc;

	wlr_allocator_destroy(sa->inner);
	close(sa->drm);
	free(sa);
}

static const struct wlr_allocator_interface scanout_impl = {
	.create_buffer = scanout_create_buffer,
	.destroy = scanout_destroy,
};

/*
 * The allocator for the screen's frames when the GPU draws them: inner,
 * wrapped, if the GPU service takes the scanout buffers - tried with one -
 * or NULL, and the frames are copied (inner stays the caller's).
 */
struct wlr_allocator *
mc_fb_scanout_allocator(struct mc_fb *fb, struct wlr_allocator *inner)
{
	const char *node = getenv("MEECHOWM_DRI");
	struct scanout_allocator *sa;
	int drm, fd;

	if (fb == NULL)
		return NULL;
	if (node == NULL)
		node = "/dev/dri/renderD128";
	if ((drm = open(node, O_RDWR | O_CLOEXEC)) < 0)
		return NULL;
	if ((fd = scanout_import(drm, fb, (int)(fb->shown + 1) %
	    (int)fb->map.nbuf)) < 0) {
		wlr_log_errno(WLR_INFO, "the GPU does not take the scanout "
		    "buffers: frames are copied");
		close(drm);
		return NULL;
	}
	close(fd);
	if ((sa = calloc(1, sizeof(*sa))) == NULL) {
		close(drm);
		return NULL;
	}
	wlr_allocator_init(&sa->base, &scanout_impl, inner->buffer_caps);
	sa->inner = inner;
	sa->fb = fb;
	sa->drm = drm;
	wlr_log(WLR_INFO, "the GPU draws into the scanout buffers");
	return &sa->base;
}

/* A frame drawn into a scanout buffer: on the screen, nothing copied. */
static void
present_scanout(struct mc_fb *fb, struct scanout_buffer *sb,
	const pixman_region32_t *damage)
{
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START |
	    DMA_BUF_SYNC_READ };
	struct fb_flip flip;
	uint64_t t0;

	fb->frames++;
	if (damage != NULL && !pixman_region32_not_empty(damage))
		return;
	/* Until the GPU has drawn it: its jobs may still run. */
	t0 = mc_now_ns();
	if (ioctl(sb->dmabuf.fd[0], DMA_BUF_IOCTL_SYNC, &sync) != 0)
		wlr_log_errno(WLR_ERROR, "DMA_BUF_IOCTL_SYNC");
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	(void)ioctl(sb->dmabuf.fd[0], DMA_BUF_IOCTL_SYNC, &sync);
	mc_stats.sync_ns += mc_now_ns() - t0;

	memset(&flip, 0, sizeof(flip));
	flip.buf = (uint32_t)sb->index;
	flip.flags = FB_FLIP_GPU;
	t0 = mc_now_ns();
	if (ioctl(fb->fd, FBIOC_FLIP, &flip) != 0) {
		wlr_log_errno(WLR_ERROR, "FBIOC_FLIP to buffer %d", sb->index);
		return;
	}
	mc_stats.flip_ns += mc_now_ns() - t0;
	mc_stats.shown++;
	fb->flips++;
	/* The one shown before the last may be drawn into again. */
	if (fb->held[1] != NULL)
		wlr_buffer_unlock(fb->held[1]);
	fb->held[1] = fb->held[0];
	fb->held[0] = wlr_buffer_lock(&sb->base);
	fb->before = (int)fb->shown;
	fb->shown = (uint32_t)sb->index;
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
	if (buffer->impl == &scanout_buffer_impl) {
		struct scanout_buffer *sb = wl_container_of(buffer, sb, base);

		present_scanout(fb, sb, damage);
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
	/* The scanout buffers the GPU drew last: theirs go with them. */
	for (i = 0; i < 2; i++)
		if (fb->held[i] != NULL)
			wlr_buffer_unlock(fb->held[i]);
	for (i = 0; i < fb->map.nbuf; i++)
		pixman_region32_fini(&fb->lacks[i]);
	close(fb->fd);		/* the driver gives the console the screen */
	free(fb);
}
