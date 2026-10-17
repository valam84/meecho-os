/*
 * The render node: contexts, the calls every DRM device answers alike, and
 * the machinery of the driver's own (minix/drmdriver.h).
 *
 * A client opens the render node and gets a context of its own - a cloned
 * minor, the way /dev/ptmx gives a terminal of one's own - and each
 * context is what a DRM file is under Linux: its own handles for buffers
 * and sync objects, its own jobs, and the driver's half - its own GPU
 * address space.  Nothing in one context can name anything in another -
 * except what it was given as a descriptor.
 *
 * Every argument is the size the caller's request says (it is in the
 * ioctl number): shorter than the structure here, it is extended with
 * zeros, longer, cut - DRM's own rule, which is what lets a newer Mesa's
 * 48-byte SUBMIT reach a 40-byte one.  Arrays a request points to (the
 * buffers of a job, the sync objects of a wait) are read from the caller's
 * own memory, and strings written there, with sys_vircopy() and the caller
 * VFS names - never an endpoint from the request.
 *
 * Moved here from the mali service (mali_drm.c, stage 11.6) when the
 * adreno service became the second render node (hw-gpu.md, step 7).
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/ioc_mmap.h>
#include <sys/ioccom.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drm_priv.h"
#include <linux/dma-buf.h>
#include <linux/sync_file.h>

#define PAGE		4096u

struct drmdrv_ctx drm_ctxs[DRMDRV_MAX_CTX];
const struct drmdrv *drm_drv;
struct log *drm_log;		/* the driver's, from drmdrv_init() */

/* ---- contexts --------------------------------------------------------- */

/* A free handle: the lowest, from 1, as Linux's idr gives them. */
int
drm_handle_new(void ***tab, unsigned *n, void *p, uint32_t *handle)
{
	void **t;
	unsigned i, m;

	for (i = 0; i < *n; i++)
		if ((*tab)[i] == NULL)
			break;
	if (i == *n) {
		if (*n >= MAX_HANDLES)
			return ENOSPC;
		m = *n ? *n * 2 : 16;
		if (m > MAX_HANDLES)
			m = MAX_HANDLES;
		if ((t = realloc(*tab, m * sizeof(*t))) == NULL)
			return ENOMEM;
		memset(t + *n, 0, (m - *n) * sizeof(*t));
		*tab = t;
		*n = m;
	}
	(*tab)[i] = p;
	*handle = i + 1;
	return OK;
}

int
drmdrv_bo_handle(struct drmdrv_ctx *c, void *bo, uint32_t *handle)
{
	return drm_handle_new(&c->bo, &c->nbo, bo, handle);
}

void *
drmdrv_bo_get(struct drmdrv_ctx *c, uint32_t h)
{
	return (h == 0 || h > c->nbo) ? NULL : c->bo[h - 1];
}

struct drmdrv_syncobj *
drmdrv_syncobj_get(struct drmdrv_ctx *c, uint32_t h)
{
	return (h == 0 || h > c->nso) ? NULL :
	    (struct drmdrv_syncobj *)c->so[h - 1];
}

struct drmdrv_ctx *
drmdrv_ctx(int i)
{
	return (i >= 0 && i < DRMDRV_MAX_CTX) ? &drm_ctxs[i] : NULL;
}

/* Everything the context holds goes; nothing of it is on the GPU. */
void
drm_ctx_free(struct drmdrv_ctx *c)
{
	unsigned i;

	for (i = 0; i < c->nbo; i++)
		if (c->bo[i] != NULL)
			drm_drv->bo_unref(c->bo[i]);
	for (i = 0; i < c->nso; i++)
		if (c->so[i] != NULL)
			drm_syncobj_unref((struct drmdrv_syncobj *)c->so[i]);
	free(c->bo);
	free(c->so);
	c->bo = NULL;
	c->so = NULL;
	c->nbo = c->nso = 0;
	drm_drv->ctx_close(c);
	log_info(drm_log, "%s is gone (%u failed job(s))\n", c->name,
	    c->failed);
	c->state = DRMDRV_CTX_FREE;
}

/* ---- time ------------------------------------------------------------- */

/* CLOCK_MONOTONIC as PM gives it to the client: boot time plus uptime. */
int64_t
drmdrv_now_ns(void)
{
	clock_t ticks, realtime;
	time_t boottime;
	uint32_t hz = sys_hz();

	if (getuptime(&ticks, &realtime, &boottime) != OK)
		return 0;
	return (int64_t)boottime * 1000000000LL +
	    (int64_t)(ticks / hz) * 1000000000LL +
	    (int64_t)(ticks % hz) * 1000000000LL / hz;
}

/* ---- the caller's memory ------------------------------------------------ */

/* From the caller's own memory: an array a request points to. */
int
drmdrv_from_user(const struct drmdrv_call *c, uint64_t ptr, void *buf,
	size_t len)
{
	if (len == 0)
		return OK;
	if (ptr == 0)
		return EFAULT;
	return sys_vircopy(c->user, (vir_bytes)ptr, SELF, (vir_bytes)buf,
	    len, 0) == OK ? OK : EFAULT;
}

int
drmdrv_to_user(const struct drmdrv_call *c, const void *buf, uint64_t ptr,
	size_t len)
{
	if (len == 0)
		return OK;
	return sys_vircopy(SELF, (vir_bytes)buf, c->user, (vir_bytes)ptr,
	    len, 0) == OK ? OK : EFAULT;
}

/* DRM's drm_copy_field(): as much as fits, and the whole length back. */
static int
put_string(const struct drmdrv_call *c, const char *s, __kernel_size_t *len,
	char *ptr)
{
	size_t n = strlen(s);

	if (n > *len)
		n = *len;
	*len = strlen(s);
	if (n > 0 && ptr != NULL)
		return drmdrv_to_user(c, s, (uint64_t)(uintptr_t)ptr, n);
	return OK;
}

/* ---- the calls every render node answers ------------------------------ */

static int
do_version(struct drmdrv_call *c, void *arg)
{
	struct drm_version *v = arg;
	int r;

	v->version_major = drm_drv->major;
	v->version_minor = drm_drv->minor;
	v->version_patchlevel = drm_drv->patchlevel;
	if ((r = put_string(c, drm_drv->name, &v->name_len, v->name)) != OK ||
	    (r = put_string(c, drm_drv->date, &v->date_len, v->date)) != OK ||
	    (r = put_string(c, drm_drv->desc, &v->desc_len, v->desc)) != OK)
		return r;
	return OK;
}

/*
 * Sync objects without timelines, PRIME both ways: as the CB2's panfrost
 * answers (port/cb2-gpu/ref-api.txt), and as msm does but for timelines,
 * which no driver here has.
 */
static int
do_get_cap(struct drmdrv_call *c, void *arg)
{
	struct drm_get_cap *g = arg;

	switch (g->capability) {
	case DRM_CAP_SYNCOBJ:
	case DRM_CAP_TIMESTAMP_MONOTONIC:
		g->value = 1;
		return OK;
	case DRM_CAP_PRIME:
		g->value = DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT;
		return OK;
	case DRM_CAP_SYNCOBJ_TIMELINE:
		g->value = 0;
		return OK;
	default:
		return EOPNOTSUPP;
	}
}

static int
do_gem_close(struct drmdrv_call *c, void *arg)
{
	struct drm_gem_close *g = arg;
	void *bo;

	if ((bo = drmdrv_bo_get(c->ctx, g->handle)) == NULL)
		return EINVAL;
	c->ctx->bo[g->handle - 1] = NULL;
	drm_drv->bo_unref(bo);
	return OK;
}

/*
 * PRIME_HANDLE_TO_FD: the buffer as a dma-buf descriptor.  DRM_CLOEXEC and
 * DRM_RDWR are O_CLOEXEC and O_RDWR (drm.h).
 */
static int
do_prime_handle_to_fd(struct drmdrv_call *c, void *arg)
{
	struct drm_prime_handle *a = arg;
	struct drmdrv_mem *mem;
	void *bo;

	if (a->flags & ~(DRM_CLOEXEC | DRM_RDWR))
		return EINVAL;
	if ((bo = drmdrv_bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	mem = drm_drv->bo_mem(bo);
	return drm_obj_export(c, OBJ_DMABUF, mem, (a->flags & DRM_CLOEXEC) |
	    ((a->flags & DRM_RDWR) ? O_RDWR : O_RDONLY),
	    (uint64_t)mem->size, &a->fd);
}

/*
 * PRIME_FD_TO_HANDLE: a dma-buf descriptor of the caller's as a buffer of
 * this context: the handle it has for that memory already, or a new one
 * with the memory placed in this context's address space.
 */
static int
do_prime_fd_to_handle(struct drmdrv_call *c, void *arg)
{
	struct drm_prime_handle *a = arg;
	struct drmdrv_obj *o;
	void *bo;
	unsigned i;
	int r;

	if ((r = drm_obj_import(c, a->fd, OBJ_DMABUF, &o)) != OK)
		return r;
	for (i = 1; i <= c->ctx->nbo; i++)
		if ((bo = drmdrv_bo_get(c->ctx, i)) != NULL &&
		    drm_drv->bo_mem(bo) == o->u.mem) {
			a->handle = i;
			return OK;
		}
	if ((bo = drm_drv->bo_import(c->ctx, o->u.mem, &r)) == NULL)
		return r;
	if ((r = drmdrv_bo_handle(c->ctx, bo, &a->handle)) != OK) {
		drm_drv->bo_unref(bo);
		return r;
	}
	return OK;
}

/*
 * The timeline calls: no driver here has timeline sync objects
 * (DRIVER_SYNCOBJ_TIMELINE), and Linux answers these EOPNOTSUPP for such.
 */
static int
do_timeline(struct drmdrv_call *c, void *arg)
{
	return EOPNOTSUPP;
}

/* ---- the tables -------------------------------------------------------- */

/* A context's: DRM's group 'd', below DRM_COMMAND_BASE. */
static const struct drmdrv_calldef ctx_calls[] = {
	{ 0x00, sizeof(struct drm_version),		do_version },
	{ 0x09, sizeof(struct drm_gem_close),		do_gem_close },
	{ 0x0c, sizeof(struct drm_get_cap),		do_get_cap },
	{ 0x2d, sizeof(struct drm_prime_handle),	do_prime_handle_to_fd },
	{ 0x2e, sizeof(struct drm_prime_handle),	do_prime_fd_to_handle },
	{ 0xbf, sizeof(struct drm_syncobj_create),	drm_syncobj_create },
	{ 0xc0, sizeof(struct drm_syncobj_destroy),	drm_syncobj_destroy },
	{ 0xc1, sizeof(struct drm_syncobj_handle),	drm_syncobj_handle_to_fd },
	{ 0xc2, sizeof(struct drm_syncobj_handle),	drm_syncobj_fd_to_handle },
	{ 0xc3, sizeof(struct drm_syncobj_wait),	drm_syncobj_wait },
	{ 0xc4, sizeof(struct drm_syncobj_array),	drm_syncobj_reset },
	{ 0xc5, sizeof(struct drm_syncobj_array),	drm_syncobj_signal },
	{ 0xca, 0,					do_timeline },
	{ 0xcb, 0,					do_timeline },
	{ 0xcc, 0,					do_timeline },
	{ 0xcd, 0,					do_timeline },
	{ 0xcf, 0,					do_timeline },
	{ 0, 0, NULL }
};

/* A sync file's: group '>'. */
static const struct drmdrv_calldef syncfile_calls[] = {
	{ 3, sizeof(struct sync_merge_data),		drm_sync_merge },
	{ 4, sizeof(struct sync_file_info),		drm_sync_file_info },
	{ 0, 0, NULL }
};

/* A dma-buf's: group 'b'. */
static const struct drmdrv_calldef dmabuf_calls[] = {
	{ 0, sizeof(struct dma_buf_sync),		drm_dmabuf_sync },
	{ 2, sizeof(struct dma_buf_export_sync_file),	drm_dmabuf_export },
	{ 3, sizeof(struct dma_buf_import_sync_file),	drm_dmabuf_import },
	{ 0, 0, NULL }
};

/*
 * mmap(2) of the device: of a context, the buffer whose mmap token
 * cm_offset is; of a dma-buf, its buffer from offset 0.  Whole, shared,
 * not executable: VM lends a region whole.
 */
static int
do_mmap(struct drmdrv_call *c)
{
	struct cdev_mmap cm;
	struct drmdrv_mem *mem = NULL;
	uint64_t off;
	size_t len;
	void *bo;
	unsigned i;
	int r;

	if (c->usize < sizeof(cm))
		return EINVAL;
	if ((r = sys_safecopyfrom(c->endpt, c->grant, 0, (vir_bytes)&cm,
	    sizeof(cm))) != OK)
		return r;
	if (c->ctx != NULL) {
		log_debug(drm_log, "%s: mmap of 0x%llx, %zu bytes, prot 0x%x "
		    "flags 0x%x, for %d\n", c->ctx->name,
		    (unsigned long long)cm.cm_offset, cm.cm_len, cm.cm_prot,
		    cm.cm_flags, c->user);
		for (i = 1; i <= c->ctx->nbo && mem == NULL; i++)
			if ((bo = drmdrv_bo_get(c->ctx, i)) != NULL &&
			    (off = drm_drv->bo_mmap_offset(bo)) != 0 &&
			    off == (uint64_t)cm.cm_offset)
				mem = drm_drv->bo_mem(bo);
	} else if (c->obj->kind == OBJ_DMABUF && cm.cm_offset == 0)
		mem = c->obj->u.mem;
	if (mem == NULL)
		return EINVAL;
	len = (cm.cm_len + PAGE - 1) & ~((size_t)PAGE - 1);
	if (len != mem->size || !(cm.cm_flags & MAP_SHARED) ||
	    (cm.cm_flags & MAP_PRIVATE) || (cm.cm_prot & PROT_EXEC))
		return EINVAL;
	if ((r = drmdrv_mem_map_into(mem, c->user,
	    (cm.cm_flags & MAP_FIXED) ? cm.cm_addr : NULL,
	    (cm.cm_prot & PROT_WRITE) != 0, &cm.cm_ret)) != OK)
		return r;
	return sys_safecopyto(c->endpt, c->grant, 0, (vir_bytes)&cm,
	    sizeof(cm));
}

int
drmdrv_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int UNUSED(flags), endpoint_t user, cdev_id_t id)
{
	union {
		struct drm_version version;
		struct drm_syncobj_wait wait;
		struct sync_file_info info;
		char raw[128];
	} arg;
	const struct drmdrv_calldef *t;
	struct drmdrv_call c;
	size_t n;
	unsigned nr;
	unsigned long group;
	int r;

	memset(&c, 0, sizeof(c));
	if (minor >= 0 && minor < DRMDRV_MAX_CTX &&
	    drm_ctxs[minor].state == DRMDRV_CTX_OPEN) {
		c.ctx = &drm_ctxs[minor];
		group = DRM_IOCTL_BASE;
		t = ctx_calls;
	} else if ((c.obj = drm_obj_get(minor, OBJ_FREE)) != NULL) {
		switch (c.obj->kind) {
		case OBJ_DMABUF:
			group = DMA_BUF_BASE;
			t = dmabuf_calls;
			break;
		case OBJ_SYNCFILE:
			group = SYNC_IOC_MAGIC;
			t = syncfile_calls;
			break;
		default:
			return ENOTTY;	/* a sync object: nothing to ask */
		}
	} else
		return ENXIO;
	c.endpt = endpt;
	c.grant = grant;
	c.usize = IOCPARM_LEN(request);
	c.user = user;
	c.id = id;

	if (IOCBASECMD(request) == IOCBASECMD(MIOCMMAP))
		return do_mmap(&c);
	if (IOCGROUP(request) != group) {
		log_debug(drm_log, "minor %d: ioctl 0x%lx is not this "
		    "device's\n", minor, request);
		return ENOTTY;
	}

	/* The driver's own are DRM_COMMAND_BASE..END; the core's around them. */
	nr = request & 0xff;
	if (c.ctx != NULL && nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END) {
		t = drm_drv->calls;
		nr -= DRM_COMMAND_BASE;
	}
	for (; t->fn != NULL; t++)
		if (t->nr == nr)
			break;
	if (t->fn == NULL)
		return c.ctx != NULL ? EINVAL : ENOTTY;
	if (t->size > sizeof(arg)) {
		log_warn(drm_log, "call 0x%x: %zu bytes, more than %zu\n", nr,
		    t->size, sizeof(arg));
		return EINVAL;
	}

	memset(&arg, 0, sizeof(arg));
	n = c.usize < t->size ? c.usize : t->size;
	if ((request & IOC_IN) && n > 0 && sys_safecopyfrom(endpt, grant, 0,
	    (vir_bytes)&arg, n) != OK)
		return EFAULT;

	r = t->fn(&c, &arg);

	/*
	 * Out whatever the answer, as drm_ioctl() does - a wait that failed
	 * still says which object it found - unless the call waits; then
	 * its answer copies it (drm_sync.c).
	 */
	if (r != EDONTREPLY && (request & IOC_OUT) && n > 0 &&
	    sys_safecopyto(endpt, grant, 0, (vir_bytes)&arg, n) != OK &&
	    r == OK)
		r = EFAULT;
	return r;
}

/* ---- open, close, cancel, select ------------------------------------- */

int
drmdrv_open(devminor_t minor, int UNUSED(access), endpoint_t user)
{
	struct drmdrv_ctx *c;
	int i, r;

	if (minor != DRMDRV_RENDER_MINOR)
		return ENXIO;
	for (i = 0; i < DRMDRV_MAX_CTX; i++)
		if (drm_ctxs[i].state == DRMDRV_CTX_FREE)
			break;
	if (i == DRMDRV_MAX_CTX)
		return ENFILE;
	c = &drm_ctxs[i];
	memset(c, 0, sizeof(*c));
	c->minor = i;
	TAILQ_INIT(&c->held);
	snprintf(c->name, sizeof(c->name), "client %d", i);
	if ((r = drm_drv->ctx_open(c)) != OK)
		return r;
	c->state = DRMDRV_CTX_OPEN;
	log_debug(drm_log, "%s opened by %d\n", c->name, user);
	return CDEV_CLONED | i;
}

/*
 * The last descriptor of a context closed.  What has not started is
 * cancelled; what runs runs to its end, and the context goes with its
 * last job (drmdrv_job_done()): its buffers and tables must not go while
 * the GPU may use them.  The last descriptor of a descriptor's object
 * closed: what it holds goes.
 */
int
drmdrv_close(devminor_t minor)
{
	struct drmdrv_ctx *c;
	struct drmdrv_obj *o;

	if ((o = drm_obj_get(minor, OBJ_FREE)) != NULL) {
		if (o->opens > 0 && --o->opens == 0)
			drm_obj_free(o);
		return OK;
	}
	if (minor < 0 || minor >= DRMDRV_MAX_CTX ||
	    drm_ctxs[minor].state != DRMDRV_CTX_OPEN)
		return ENXIO;
	c = &drm_ctxs[minor];
	drm_waits_cancel_ctx(c);
	c->state = DRMDRV_CTX_CLOSING;
	c->jobs++;			/* not freed under drm_cancel_held() */
	drm_cancel_held(c);
	c->jobs--;
	drm_drv->ctx_cancel(c);		/* may free c, through drmdrv_job_done() */
	if (c->state == DRMDRV_CTX_CLOSING && c->jobs == 0)
		drm_ctx_free(c);
	return OK;
}

/* A signal for a process that waits: the wait ends, EINTR. */
int
drmdrv_cancel(devminor_t UNUSED(minor), endpoint_t endpt, cdev_id_t id)
{
	return drm_waits_cancel(endpt, id);
}

/*
 * select(2) and poll(2): a sync file is readable once its fence is over;
 * a dma-buf readable and writable once no job uses it; a context never
 * (a render node has no events).  Asked to tell, it tells once.
 */
int
drmdrv_select(devminor_t minor, unsigned int ops, endpoint_t endpt)
{
	struct drmdrv_obj *o;
	unsigned want = ops & (CDEV_OP_RD | CDEV_OP_WR | CDEV_OP_ERR), ready;

	if ((o = drm_obj_get(minor, OBJ_FREE)) == NULL)
		return (minor >= 0 && minor < DRMDRV_MAX_CTX) ? 0 : ENXIO;
	ready = drm_obj_ready(o, want);
	if (ready == 0 && (ops & CDEV_NOTIFY)) {
		o->sel_endpt = endpt;
		o->sel_ops |= want;
	}
	return ready;
}

void
drmdrv_init(const struct drmdrv *d)
{
	int i;

	drm_drv = d;
	drm_log = d->log;
	drm_waits_init();
	for (i = 0; i < DRMDRV_MAX_CTX; i++)
		TAILQ_INIT(&drm_ctxs[i].held);
}

/* The service is going: every wait answered, every context freed. */
void
drmdrv_shutdown(void)
{
	int i;

	drm_waits_shutdown();
	for (i = 0; i < DRMDRV_MAX_CTX; i++) {
		if (drm_ctxs[i].state == DRMDRV_CTX_FREE)
			continue;
		drm_ctxs[i].jobs++;
		drm_cancel_held(&drm_ctxs[i]);
		drm_ctxs[i].jobs--;
		if (drm_ctxs[i].jobs == 0)
			drm_ctx_free(&drm_ctxs[i]);
	}
}
