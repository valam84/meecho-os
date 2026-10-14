/*
 * Who draws the frames: the GPU through Mesa (wlroots' GLES2 renderer, on
 * the Mali of the CB2 - Panfrost over the mali service), or the CPU with
 * pixman.
 *
 * MEECHOWM_RENDERER says which: "gles2", "pixman", or nothing - GLES2 if
 * the render node opens and Mesa takes it, pixman otherwise (QEMU, or the
 * mali service not running), with a line in the log saying why.  An asked
 * for GLES2 that fails is an error, not pixman: a measurement must not
 * measure the other one by mistake.
 *
 * With GLES2 the frames are GBM buffers - dma-bufs of the mali service -
 * and fbout.c copies each into a scanout buffer of the frame buffer driver
 * with the CPU.  So the buffers the output draws into (and its cursor's)
 * must be readable as rows: the allocator here passes the GBM allocator
 * only the LINEAR modifier, where Panfrost would otherwise pick a tiled or
 * compressed (AFBC) layout.  mc_buffer_begin() then reads such a buffer
 * through mmap(2) of its dma-buf, kept for the buffer's life, after
 * DMA_BUF_IOCTL_SYNC has waited for the GPU to finish writing it.
 */
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/gles2.h>
#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>

#include "meechowm.h"

struct mc_frame_stats mc_stats;

uint64_t
mc_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* ---- the allocator: GBM, LINEAR only ------------------------------- */

struct linear_allocator {
	struct wlr_allocator base;
	struct wlr_allocator *gbm;
};

static struct wlr_buffer *
linear_create_buffer(struct wlr_allocator *alloc, int width, int height,
	const struct wlr_drm_format *format)
{
	struct linear_allocator *la = (struct linear_allocator *)alloc;
	uint64_t linear = DRM_FORMAT_MOD_LINEAR;
	struct wlr_drm_format f = {
		.format = format->format,
		.len = 1,
		.capacity = 1,
		.modifiers = &linear,
	};
	size_t i;

	for (i = 0; i < format->len; i++)
		if (format->modifiers[i] == DRM_FORMAT_MOD_LINEAR)
			break;
	if (i == format->len) {
		wlr_log(WLR_ERROR, "a buffer of format 0x%08x without LINEAR "
		    "among its modifiers: the screen could not read it",
		    format->format);
		return NULL;
	}
	return wlr_allocator_create_buffer(la->gbm, width, height, &f);
}

static void
linear_destroy(struct wlr_allocator *alloc)
{
	struct linear_allocator *la = (struct linear_allocator *)alloc;

	wlr_allocator_destroy(la->gbm);
	free(la);
}

static const struct wlr_allocator_interface linear_impl = {
	.create_buffer = linear_create_buffer,
	.destroy = linear_destroy,
};

static struct wlr_allocator *
linear_allocator_create(struct wlr_allocator *gbm)
{
	struct linear_allocator *la;

	if ((la = calloc(1, sizeof(*la))) == NULL) {
		wlr_allocator_destroy(gbm);
		return NULL;
	}
	wlr_allocator_init(&la->base, &linear_impl, gbm->buffer_caps);
	la->gbm = gbm;
	return &la->base;
}

/* ---- choosing ------------------------------------------------------ */

static struct wlr_renderer *
gles2_create(void)
{
	const char *node = getenv("MEECHOWM_DRI");
	struct wlr_renderer *r;
	int fd;

	if (node == NULL)
		node = "/dev/dri/renderD128";
	if ((fd = open(node, O_RDWR | O_CLOEXEC)) < 0) {
		wlr_log(WLR_INFO, "no GPU: %s: %s", node, strerror(errno));
		return NULL;
	}
	r = wlr_gles2_renderer_create_with_drm_fd(fd);
	close(fd);	/* EGL and GBM open the node again for themselves */
	if (r == NULL)
		wlr_log(WLR_ERROR, "%s opens, but GLES2 on it does not", node);
	return r;
}

bool
mc_render_create(struct wlr_backend *backend, struct wlr_renderer **rp,
	struct wlr_allocator **ap)
{
	const char *want = getenv("MEECHOWM_RENDERER");
	struct wlr_renderer *r = NULL;
	struct wlr_allocator *a;

	if (want != NULL && strcmp(want, "gles2") != 0 &&
	    strcmp(want, "pixman") != 0) {
		wlr_log(WLR_ERROR, "MEECHOWM_RENDERER=%s: gles2 or pixman", want);
		return false;
	}
	if (want == NULL || strcmp(want, "gles2") == 0) {
		if ((r = gles2_create()) != NULL) {
			a = wlr_allocator_autocreate(backend, r);
			if (a != NULL && (a->buffer_caps & WLR_BUFFER_CAP_DMABUF))
				a = linear_allocator_create(a);
			else {
				wlr_log(WLR_ERROR, "GLES2 without a GBM allocator");
				if (a != NULL)
					wlr_allocator_destroy(a);
				a = NULL;
			}
			if (a == NULL) {
				wlr_renderer_destroy(r);
				r = NULL;
			}
		}
		if (r == NULL && want != NULL)
			return false;	/* asked for, and not there */
	}
	if (r == NULL) {
		if ((r = wlr_pixman_renderer_create()) == NULL)
			return false;
		if ((a = wlr_allocator_autocreate(backend, r)) == NULL) {
			wlr_renderer_destroy(r);
			return false;
		}
		mc_stats.renderer = "pixman";
	} else
		mc_stats.renderer = "gles2";
	wlr_log(WLR_INFO, "frames drawn by %s", mc_stats.renderer);
	*rp = r;
	*ap = a;
	return true;
}

/* ---- reading a frame ------------------------------------------------ */

/* A dma-buf's mapping, for as long as its buffer lives. */
struct dmabuf_map {
	struct wlr_addon addon;
	void *addr;
	size_t len;
	int fd;			/* the buffer's own; not ours to close */
	uint32_t format, offset, stride;
};

static void
dmabuf_map_destroy(struct wlr_addon *addon)
{
	struct dmabuf_map *m = wl_container_of(addon, m, addon);

	munmap(m->addr, m->len);
	wlr_addon_finish(&m->addon);
	free(m);
}

static const struct wlr_addon_interface dmabuf_map_impl = {
	.name = "meechowm-dmabuf-map",
	.destroy = dmabuf_map_destroy,
};

static struct dmabuf_map *
dmabuf_map_get(struct wlr_buffer *buffer)
{
	struct wlr_dmabuf_attributes at;
	struct wlr_addon *addon;
	struct dmabuf_map *m;
	off_t size;

	if ((addon = wlr_addon_find(&buffer->addons, &dmabuf_map_impl,
	    &dmabuf_map_impl)) != NULL)
		return wl_container_of(addon, m, addon);
	if (!wlr_buffer_get_dmabuf(buffer, &at))
		return NULL;
	if (at.n_planes != 1 || at.modifier != DRM_FORMAT_MOD_LINEAR) {
		wlr_log(WLR_ERROR, "a dma-buf frame of %d planes, modifier "
		    "0x%016llx: not readable as rows", at.n_planes,
		    (unsigned long long)at.modifier);
		return NULL;
	}
	/* The size of a dma-buf is where SEEK_END takes it (Mesa asks so). */
	if ((size = lseek(at.fd[0], 0, SEEK_END)) <= 0) {
		wlr_log_errno(WLR_ERROR, "dma-buf size");
		return NULL;
	}
	if ((m = calloc(1, sizeof(*m))) == NULL)
		return NULL;
	m->len = (size_t)size;
	m->addr = mmap(NULL, m->len, PROT_READ, MAP_SHARED, at.fd[0], 0);
	if (m->addr == MAP_FAILED) {
		wlr_log_errno(WLR_ERROR, "mmap of a dma-buf frame (%zu bytes)",
		    m->len);
		free(m);
		return NULL;
	}
	m->fd = at.fd[0];
	m->format = at.format;
	m->offset = at.offset[0];
	m->stride = at.stride[0];
	wlr_addon_init(&m->addon, &buffer->addons, &dmabuf_map_impl,
	    &dmabuf_map_impl);
	wlr_log(WLR_DEBUG, "dma-buf frame %dx%d mapped: %zu bytes, stride %u",
	    buffer->width, buffer->height, m->len, m->stride);
	return m;
}

bool
mc_buffer_begin(struct wlr_buffer *buffer, void **data, uint32_t *format,
	size_t *stride)
{
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START |
	    DMA_BUF_SYNC_READ };
	struct dmabuf_map *m;
	uint64_t t0;

	if (wlr_buffer_begin_data_ptr_access(buffer,
	    WLR_BUFFER_DATA_PTR_ACCESS_READ, data, format, stride))
		return true;
	if ((m = dmabuf_map_get(buffer)) == NULL)
		return false;
	/* Until the GPU has written it: the jobs that draw it may still run. */
	t0 = mc_now_ns();
	if (ioctl(m->fd, DMA_BUF_IOCTL_SYNC, &sync) != 0)
		wlr_log_errno(WLR_ERROR, "DMA_BUF_IOCTL_SYNC");
	mc_stats.sync_ns += mc_now_ns() - t0;
	*data = (char *)m->addr + m->offset;
	*format = m->format;
	*stride = m->stride;
	return true;
}

void
mc_buffer_end(struct wlr_buffer *buffer)
{
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_END |
	    DMA_BUF_SYNC_READ };
	struct wlr_addon *addon;
	struct dmabuf_map *m;

	if ((addon = wlr_addon_find(&buffer->addons, &dmabuf_map_impl,
	    &dmabuf_map_impl)) == NULL) {
		wlr_buffer_end_data_ptr_access(buffer);
		return;
	}
	m = wl_container_of(addon, m, addon);
	(void)ioctl(m->fd, DMA_BUF_IOCTL_SYNC, &sync);
}
