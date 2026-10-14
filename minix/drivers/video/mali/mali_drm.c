/*
 * The device: /dev/dri/renderD128, the interface Mesa's Panfrost driver
 * talks to (panfrost_drm.h, and the few DRM calls every render node
 * has).  Stage 11.6.
 *
 * A client opens the render node and gets a context of its own - a cloned
 * minor, the way /dev/ptmx gives a terminal of one's own - and each
 * context is what a DRM file is under Linux: its own GPU address space,
 * its own handles for buffers and sync objects, its own jobs.  Nothing in
 * one context can name anything in another - except what it was given as
 * a descriptor.
 *
 * What is here, and why that much (PORTING-LOG, "Этап 11.6"):
 *
 *   VERSION, GET_CAP              what Mesa asks first: the name picks
 *                                 the driver
 *   GEM_CLOSE                     a handle goes
 *   PANFROST_GET_PARAM            the GPU's identity and features
 *   PANFROST_CREATE_BO            a buffer, placed in the address space
 *   PANFROST_MMAP_BO + mmap(2)    the token, then the mapping: mmap(2) of
 *                                 a character device asks the driver
 *                                 (MIOCMMAP, <sys/ioc_mmap.h>)
 *   PANFROST_GET_BO_OFFSET        where a buffer is on the GPU
 *   PANFROST_SUBMIT               a job chain, the buffers it uses,
 *                                 sync objects to wait for and to signal
 *   PANFROST_WAIT_BO              until no job uses a buffer
 *   PANFROST_MADVISE              nothing is ever purged: "retained"
 *   SYNCOBJ_CREATE, _DESTROY, _WAIT, _RESET, _SIGNAL
 *                                 Mesa's fences; it makes two at context
 *                                 creation and waits on them
 *   PRIME_HANDLE_TO_FD, _FD_TO_HANDLE
 *                                 a buffer as a descriptor (a dma-buf) and
 *                                 back - how a frame reaches the
 *                                 compositor
 *   SYNCOBJ_HANDLE_TO_FD, _FD_TO_HANDLE
 *                                 a fence as a descriptor (a sync file),
 *                                 or a whole sync object; Mesa does both
 *                                 for every fence it makes
 *
 * Descriptors (the second part of 11.6).  A dma-buf, a sync file and a
 * sync object given away are minors of this device too, from OBJ_MINOR
 * on: VFS makes the process a descriptor of the minor (cdevfd(2)), and
 * names the minor of a descriptor the process gives back.  So they are
 * ordinary files - fork, dup, exec, SCM_RIGHTS over a socket - and what
 * one may do with them is what Linux lets one do: mmap(2) of a dma-buf
 * and lseek(2) to its end (its size, which is how Mesa learns it),
 * DMA_BUF_IOCTL_SYNC and _EXPORT/_IMPORT_SYNC_FILE on it; poll(2) of a
 * sync file, SYNC_IOC_MERGE and _FILE_INFO.  Holding the descriptor is
 * the right: a context imports what a descriptor of the process that asks
 * names, never a number from the request.
 *
 * Fences and order.  Jobs of one context run in the order given.  Jobs of
 * different contexts that share a buffer run in the order given too, as
 * panfrost's implicit fences order them: every job leaves its fence on
 * each buffer it names, and the next job of another context to name the
 * buffer waits for it; so does WAIT_BO, and a processor that begins
 * access with DMA_BUF_IOCTL_SYNC.  A job that waits for a fence not yet
 * over - another context's, by a sync file or a shared sync object -
 * stays here, with every later job of its context behind it, until the
 * fence is over; then it goes to the slots (mali_sched.c).
 *
 * Every argument is the size the caller's request says (it is in the
 * ioctl number): shorter than this service's structure, it is extended
 * with zeros, longer, cut - DRM's own rule, which is what lets a newer
 * Mesa's 48-byte SUBMIT reach a 40-byte one.  Arrays a request points to
 * (the buffers of a job, the sync objects of a wait) are read from the
 * caller's own memory, and strings written there, with sys_vircopy() and
 * the caller VFS names - never an endpoint from the request.
 *
 * Errors are Linux's for the same request, with one addition: a job that
 * failed - its chain broke, its address space faulted - makes the wait
 * on its sync object fail with EIO, where Linux returns 0 and leaves the
 * error in the fence for a sync file to show (here it shows there too).
 * WAIT_BO stays as Linux has it, 0 once the buffer is idle, because Mesa
 * asserts that it only ever fails with ETIMEDOUT or EBUSY.  The job's
 * slot status and fault go to the log.
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/ds.h>
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

#include "mali.h"
#include "malireg.h"
#include "drm.h"
#include "panfrost_drm.h"
#include "meecho_drm.h"
#include <linux/dma-buf.h>
#include <linux/sync_file.h>

#define RENDER_MINOR	128		/* /dev/dri/renderD128 */
#define MAX_CTX		16		/* contexts, minors 0..15 */
#define MAX_HANDLES	65536		/* buffers, and sync objects, each */
#define MAX_JOB_BOS	4096		/* buffers one job may name */
#define MAX_SYNCS	64		/* sync objects one call may name */
#define MAX_QUEUED	256		/* jobs a context may have waiting */

/*
 * Descriptors' minors.  libchardriver serves a minor only after it has
 * seen it opened, and remembers every one it has seen, 256 at most
 * (MAX_NR_OPEN_DEVICES): 16 contexts and the node take 17.  A minor is
 * reused lowest first, so the minors ever seen are as many as the most
 * descriptors' objects there were at once.
 */
#define OBJ_MINOR	256
#define MAX_OBJ		224

/* ---- fences and sync objects ------------------------------------------ */

/*
 * Something that becomes over once, with an error or without: a job's
 * end, or a merge of such - over when all of its are.
 */
struct fence {
	unsigned refs;
	int signaled;
	int error;			/* 0, or why: EIO - the job failed */
	void *owner;			/* the context whose job it is, or NULL */
	unsigned nchild;		/* a merge: these, none of them a merge */
	struct fence **child;
};

struct syncobj {
	unsigned refs;			/* handles, waits, a descriptor */
	struct fence *fence;		/* NULL: none */
	int minor;			/* as a descriptor, or -1 */
};

static struct fence *
fence_new(int signaled, void *owner)
{
	struct fence *f;

	if ((f = calloc(1, sizeof(*f))) == NULL)
		return NULL;
	f->refs = 1;
	f->signaled = signaled;
	f->owner = owner;
	return f;
}

static struct fence *
fence_ref(struct fence *f)
{
	if (f != NULL)
		f->refs++;
	return f;
}

static void
fence_unref(struct fence *f)
{
	unsigned i;

	if (f != NULL && --f->refs == 0) {
		for (i = 0; i < f->nchild; i++)
			fence_unref(f->child[i]);
		free(f->child);
		free(f);
	}
}

/* Over?  A merge is over when all its fences are, failed if any failed. */
static int
fence_done(struct fence *f)
{
	unsigned i;

	if (f == NULL || f->signaled)
		return 1;
	if (f->nchild == 0)
		return 0;
	for (i = 0; i < f->nchild; i++)
		if (!f->child[i]->signaled)
			return 0;
	f->signaled = 1;
	for (i = 0; i < f->nchild && f->error == 0; i++)
		f->error = f->child[i]->error;
	return 1;
}

/* How many fences f is: a merge's, or one. */
static unsigned
fence_count(struct fence *f)
{
	return f->nchild > 0 ? f->nchild : 1;
}

/*
 * The fences of a and b, those not yet over, as one: none - a fence that
 * is over; one - that one; more - a merge.  Linux's dma_fence_unwrap_merge()
 * does the same, and a sync file merged from one that is over and one that
 * is not is that one.  Either may be NULL.
 */
static struct fence *
fence_merge(struct fence *a, struct fence *b)
{
	struct fence *in[2] = { a, b }, *f, **c, *leaf;
	unsigned n = 0, i, k, m;

	for (i = 0; i < 2; i++)
		if (in[i] != NULL)
			n += fence_count(in[i]);
	if ((c = calloc(n > 0 ? n : 1, sizeof(*c))) == NULL)
		return NULL;
	for (i = 0, n = 0; i < 2; i++) {
		if (in[i] == NULL)
			continue;
		for (k = 0; k < fence_count(in[i]); k++) {
			leaf = in[i]->nchild > 0 ? in[i]->child[k] : in[i];
			if (leaf->signaled)
				continue;
			for (m = 0; m < n && c[m] != leaf; m++)
				;
			if (m == n)
				c[n++] = leaf;
		}
	}
	if (n <= 1) {
		f = n == 1 ? fence_ref(c[0]) : fence_new(1, NULL);
		free(c);
		return f;
	}
	if ((f = fence_new(0, NULL)) == NULL) {
		free(c);
		return NULL;
	}
	for (i = 0; i < n; i++)
		fence_ref(c[i]);
	f->child = c;
	f->nchild = n;
	return f;
}

static void
syncobj_set(struct syncobj *so, struct fence *f)
{
	fence_unref(so->fence);
	so->fence = fence_ref(f);
}

static void
syncobj_unref(struct syncobj *so)
{
	if (--so->refs == 0) {
		fence_unref(so->fence);
		free(so);
	}
}

/* ---- descriptors' objects ---------------------------------------------- */

#define OBJ_FREE	0
#define OBJ_DMABUF	1		/* a buffer's memory */
#define OBJ_SYNCFILE	2		/* a fence */
#define OBJ_SYNCOBJ	3		/* a sync object */

struct obj {
	int kind;
	unsigned opens;			/* descriptors VFS made, not closed */
	union {
		struct mali_mem *mem;
		struct fence *fence;
		struct syncobj *so;
	} u;
	endpoint_t sel_endpt;		/* a select that waits, or NONE */
	unsigned sel_ops;
};

static struct obj objs[MAX_OBJ];

static struct obj *
obj_get(devminor_t minor, int kind)
{
	struct obj *o;

	if (minor < OBJ_MINOR || minor >= OBJ_MINOR + MAX_OBJ)
		return NULL;
	o = &objs[minor - OBJ_MINOR];
	return o->kind == kind || (kind == OBJ_FREE && o->kind != OBJ_FREE) ?
	    o : NULL;
}

static devminor_t
obj_minor(const struct obj *o)
{
	return OBJ_MINOR + (devminor_t)(o - objs);
}

/* The last descriptor of it is closed: what it holds goes. */
static void
obj_free(struct obj *o)
{
	switch (o->kind) {
	case OBJ_DMABUF:
		o->u.mem->minor = -1;
		mali_mem_unref(o->u.mem);
		break;
	case OBJ_SYNCFILE:
		fence_unref(o->u.fence);
		break;
	case OBJ_SYNCOBJ:
		o->u.so->minor = -1;
		syncobj_unref(o->u.so);
		break;
	}
	memset(o, 0, sizeof(*o));
}

/* ---- contexts --------------------------------------------------------- */

#define CTX_FREE	0
#define CTX_OPEN	1
#define CTX_CLOSING	2		/* closed, its last job not over */

struct pjob;

struct ctx {
	int state;
	int minor;
	char name[16];
	struct mali_vm vm;
	void **bo;			/* buffer with handle h: bo[h - 1] */
	unsigned nbo;
	void **so;			/* sync object with handle h: so[h - 1] */
	unsigned nso;
	unsigned jobs;			/* given and not over */
	unsigned failed;		/* jobs that failed, for the log */
	TAILQ_HEAD(, pjob) held;	/* waiting for others' fences */
};

static struct ctx ctxs[MAX_CTX];

/* A job of a client: the job, and what it holds. */
struct pjob {
	struct mali_job job;		/* first: done() gets this */
	struct ctx *ctx;
	struct fence *out;		/* over when it is */
	TAILQ_ENTRY(pjob) hold;		/* in ctx->held */
	struct fence **in;		/* what it waits for, still held here */
	unsigned nin;
	struct mali_bo *bos[];
};

/* A call that waits: WAIT_BO, SYNCOBJ_WAIT, or DMA_BUF_IOCTL_SYNC. */
struct wait {
	TAILQ_ENTRY(wait) link;
	struct ctx *ctx;		/* NULL: a dma-buf's */
	endpoint_t endpt;		/* VFS, to reply to */
	cdev_id_t id;
	cp_grant_id_t grant;		/* for SYNCOBJ_WAIT's first_signaled */
	size_t copyout;
	int64_t deadline;		/* ns of CLOCK_MONOTONIC; INT64_MAX */
	struct fence *fence;		/* WAIT_BO, DMA_BUF_IOCTL_SYNC */
	struct syncobj *so[MAX_SYNCS];	/* SYNCOBJ_WAIT */
	unsigned count;
	uint32_t flags;
	struct drm_syncobj_wait args;
};

static TAILQ_HEAD(, wait) waits = TAILQ_HEAD_INITIALIZER(waits);
static minix_timer_t wait_timer;

static void changed(void);

/* A free handle: the lowest, from 1, as Linux's idr gives them. */
static int
handle_new(void ***tab, unsigned *n, void *p, uint32_t *handle)
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

static struct mali_bo *
bo_get(struct ctx *c, uint32_t h)
{
	return (h == 0 || h > c->nbo) ? NULL : (struct mali_bo *)c->bo[h - 1];
}

static struct syncobj *
so_get(struct ctx *c, uint32_t h)
{
	return (h == 0 || h > c->nso) ? NULL : (struct syncobj *)c->so[h - 1];
}

/* Everything the context holds goes; nothing of it is on the GPU. */
static void
ctx_free(struct ctx *c)
{
	unsigned i;

	for (i = 0; i < c->nbo; i++)
		if (c->bo[i] != NULL)
			mali_bo_unref((struct mali_bo *)c->bo[i]);
	for (i = 0; i < c->nso; i++)
		if (c->so[i] != NULL)
			syncobj_unref((struct syncobj *)c->so[i]);
	free(c->bo);
	free(c->so);
	c->bo = NULL;
	c->so = NULL;
	c->nbo = c->nso = 0;
	mali_vm_fini(&c->vm);
	log_info(&mali_log, "%s is gone (%u failed job(s))\n", c->name,
	    c->failed);
	c->state = CTX_FREE;
}

/* ---- time ------------------------------------------------------------- */

/* CLOCK_MONOTONIC as PM gives it to the client: boot time plus uptime. */
static int64_t
now_ns(void)
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

/* ---- the calls -------------------------------------------------------- */

struct call {
	struct ctx *ctx;		/* NULL: a descriptor's object */
	struct obj *obj;
	endpoint_t endpt;		/* VFS */
	cp_grant_id_t grant;
	size_t usize;			/* the argument's size, caller's */
	endpoint_t user;		/* the process that asked */
	cdev_id_t id;
};

/* From the caller's own memory: an array a request points to. */
static int
from_user(const struct call *c, uint64_t ptr, void *buf, size_t len)
{
	if (len == 0)
		return OK;
	if (ptr == 0)
		return EFAULT;
	return sys_vircopy(c->user, (vir_bytes)ptr, SELF, (vir_bytes)buf,
	    len, 0) == OK ? OK : EFAULT;
}

static int
to_user(const struct call *c, const void *buf, uint64_t ptr, size_t len)
{
	if (len == 0)
		return OK;
	return sys_vircopy(SELF, (vir_bytes)buf, c->user, (vir_bytes)ptr,
	    len, 0) == OK ? OK : EFAULT;
}

/* DRM's drm_copy_field(): as much as fits, and the whole length back. */
static int
put_string(const struct call *c, const char *s, __kernel_size_t *len,
	char *ptr)
{
	size_t n = strlen(s);

	if (n > *len)
		n = *len;
	*len = strlen(s);
	if (n > 0 && ptr != NULL)
		return to_user(c, s, (uint64_t)(uintptr_t)ptr, n);
	return OK;
}

/*
 * A descriptor of the thing p of this kind, in the process that asks
 * (cdevfd(2)): a dma-buf or a sync object has one minor for as long as any
 * descriptor of it is open, so that every one is the same buffer to whoever
 * gets it (Linux gives the same file); a sync file is new each time.  The
 * open is counted before VFS is asked: a close of another descriptor of it
 * may come meanwhile.  OK and the descriptor in *fd, or VFS's error as VFS
 * gives it (its sign is not looked at: the stand's errors are the host's).
 */
static int
obj_export(const struct call *c, int kind, void *p, int flags, uint64_t size,
	int32_t *fd)
{
	struct obj *o = NULL;
	int i, r;

	if (kind == OBJ_DMABUF && ((struct mali_mem *)p)->minor >= 0)
		o = obj_get(((struct mali_mem *)p)->minor, kind);
	else if (kind == OBJ_SYNCOBJ && ((struct syncobj *)p)->minor >= 0)
		o = obj_get(((struct syncobj *)p)->minor, kind);
	if (o == NULL) {
		for (i = 0; i < MAX_OBJ && objs[i].kind != OBJ_FREE; i++)
			;
		if (i == MAX_OBJ) {
			log_warn(&mali_log, "every descriptor's minor is "
			    "taken (%d)\n", MAX_OBJ);
			return ENFILE;
		}
		o = &objs[i];
		o->kind = kind;
		o->sel_endpt = NONE;
		switch (kind) {
		case OBJ_DMABUF:
			o->u.mem = p;
			mali_mem_ref(o->u.mem);
			o->u.mem->minor = obj_minor(o);
			break;
		case OBJ_SYNCFILE:
			o->u.fence = fence_ref(p);
			break;
		case OBJ_SYNCOBJ:
			o->u.so = p;
			o->u.so->refs++;
			o->u.so->minor = obj_minor(o);
			break;
		}
	}
	o->opens++;
	if ((r = chardriver_open_minor(obj_minor(o))) == OK &&
	    (r = cdevfd_new(c->user, obj_minor(o), flags, size)) >= 0) {
		*fd = r;
		return OK;
	}
	if (--o->opens == 0)
		obj_free(o);
	return r;
}

/*
 * The object of this kind a descriptor of the process that asks is: OK,
 * or VFS's error (EBADF: no such descriptor), or EINVAL (another device's,
 * or another kind).
 */
static int
obj_import(const struct call *c, int fd, int kind, struct obj **o)
{
	int minor;

	if ((minor = cdevfd_get(c->user, fd)) < 0)
		return minor;
	if ((*o = obj_get(minor, kind)) == NULL)
		return EINVAL;
	return OK;
}

static int
do_version(struct call *c, void *arg)
{
	struct drm_version *v = arg;
	int r;

	/* What the board's panfrost says (port/cb2-gpu/ref-api.txt). */
	v->version_major = 1;
	v->version_minor = 2;
	v->version_patchlevel = 0;
	if ((r = put_string(c, "panfrost", &v->name_len, v->name)) != OK ||
	    (r = put_string(c, "20180908", &v->date_len, v->date)) != OK ||
	    (r = put_string(c, "panfrost DRM", &v->desc_len, v->desc)) != OK)
		return r;
	return OK;
}

/* As the board's panfrost answers (ref-api.txt). */
static int
do_get_cap(struct call *c, void *arg)
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
do_gem_close(struct call *c, void *arg)
{
	struct drm_gem_close *g = arg;
	struct mali_bo *bo;

	if ((bo = bo_get(c->ctx, g->handle)) == NULL)
		return EINVAL;
	c->ctx->bo[g->handle - 1] = NULL;
	mali_bo_unref(bo);
	return OK;
}

/*
 * PANFROST_GET_PARAM: what panfrost answers, read from the same
 * registers (panfrost_gpu.c's feature list: the bitmaps 64 bits wide,
 * the revision the low half of GPU_ID, the core groups one per L2).
 * Checked against the board's answers in port/cb2-gpu/ref-api.txt.
 */
static uint64_t
rd64(unsigned lo)
{
	return mali_rd(&mali.info.gpu, lo) |
	    ((uint64_t)mali_rd(&mali.info.gpu, lo + 4) << 32);
}

static uint64_t
mali_param(unsigned param, int *ok)
{
	const struct mali_regs *gpu = &mali.info.gpu;
	uint64_t l2;

	*ok = 1;
	switch (param) {
	case 0:	return mali.gpu_id >> 16;		/* GPU_PROD_ID */
	case 1:	return mali.gpu_id & 0xffff;		/* GPU_REVISION */
	case 2:	return rd64(GPU_SHADER_PRESENT_LO);
	case 3:	return rd64(GPU_TILER_PRESENT_LO);
	case 4:	return rd64(GPU_L2_PRESENT_LO);
	case 5:	return rd64(GPU_STACK_PRESENT_LO);
	case 6:	return mali.as_present;
	case 7:	return mali.js_present;
	case 8:	return mali.l2_features;
	case 9:	return mali.core_features;
	case 10: return mali.tiler_features;
	case 11: return mali.mem_features;
	case 12: return mali.mmu_features;
	case 13: return mali.thread_features;
	case 14: return mali.thread_max_threads;
	case 15: return mali_rd(gpu, GPU_THREAD_MAX_WORKGROUP);
	case 16: return mali_rd(gpu, GPU_THREAD_MAX_BARRIER);
	case 17: return mali.coherency_features;
	case 18: case 19: case 20: case 21:		/* TEXTURE_FEATURES0-3 */
		return mali_rd(gpu, GPU_TEXTURE_FEATURES(param - 18));
	case 38:					/* NR_CORE_GROUPS */
		l2 = rd64(GPU_L2_PRESENT_LO);
		return (uint64_t)__builtin_popcountll(l2);
	case 39: return mali_rd(gpu, GPU_THREAD_TLS_ALLOC);
	case 40: return mali_rd(gpu, GPU_AFBC_FEATURES);
	default:
		if (param >= 22 && param <= 37)		/* JS_FEATURES0-15 */
			return mali_rd(gpu, GPU_JS_FEATURES(param - 22));
		*ok = 0;
		return 0;
	}
}

static int
do_get_param(struct call *c, void *arg)
{
	struct drm_panfrost_get_param *p = arg;
	int ok;

	if (p->pad != 0)
		return EINVAL;
	p->value = mali_param(p->param, &ok);
	return ok ? OK : EINVAL;
}

static int
do_create_bo(struct call *c, void *arg)
{
	struct drm_panfrost_create_bo *a = arg;
	struct mali_bo *bo;
	int r;

	if (a->size == 0 || a->pad != 0 ||
	    (a->flags & ~(PANFROST_BO_NOEXEC | PANFROST_BO_HEAP)))
		return EINVAL;
	/* Heaps are never code. */
	if ((a->flags & PANFROST_BO_HEAP) && !(a->flags & PANFROST_BO_NOEXEC))
		return EINVAL;
	if ((bo = mali_bo_new(&c->ctx->vm, a->size, a->flags, &r)) == NULL)
		return r;
	if ((r = handle_new(&c->ctx->bo, &c->ctx->nbo, bo,
	    &a->handle)) != OK) {
		mali_bo_unref(bo);
		return r;
	}
	a->offset = bo->va;
	return OK;
}

/*
 * DRM_IOCTL_MEECHO_SCANOUT_IMPORT (meecho_drm.h): a scanout buffer of the
 * frame buffer driver, named by the caller's own mapping of it, as a
 * buffer of the caller's context.  The pages must lie in one of the
 * buffers the driver published in DS: the caller proves it may draw there
 * by having them mapped (only the owner of the screen is given them), and
 * the driver's word that they are its scanout buffers says they stay -
 * nothing else a process has mapped is safe to give the GPU, since the
 * process could free it under a job.
 */
static int
do_scanout_import(struct call *c, void *arg)
{
	struct drm_meecho_scanout_import *a = arg;
	struct mali_bo *bo;
	phys_bytes phys;
	endpoint_t fb;
	u32_t n, size, base, vir;
	char key[DS_MAX_KEYLEN];
	int i, r;

	if (a->pad != 0 || a->size == 0 || (a->size & (MALI_PAGE - 1)) ||
	    (a->addr & (MALI_PAGE - 1)))
		return EINVAL;
	if (ds_retrieve_u32(MEECHO_DS_SCANOUT_N, &n) != OK ||
	    ds_retrieve_u32(MEECHO_DS_SCANOUT_SIZE, &size) != OK)
		return ENODEV;		/* no frame buffer driver, or before it */
	/*
	 * The caller's pages, one call: the kernel answers only for a
	 * physically contiguous range.  Remote - in another process's address
	 * space - since sys_umap answers for the caller's own or a grant.
	 */
	if ((r = mali_umap_remote(c->user, (vir_bytes)a->addr,
	    (vir_bytes)a->size, &phys)) != OK || phys == 0)
		return EFAULT;
	/*
	 * A whole buffer: the driver's region of it is what a client maps
	 * (mali_bo_new_phys()), and VM lends regions whole.
	 */
	for (i = 0; i < (int)n; i++) {
		snprintf(key, sizeof(key), MEECHO_DS_SCANOUT, i);
		if (ds_retrieve_u32(key, &base) == OK && phys == base &&
		    a->size == ((size + MALI_PAGE - 1) & ~(MALI_PAGE - 1)))
			break;
	}
	if (i == (int)n) {
		log_warn(&mali_log, "%s: 0x%lx, %llu bytes, is no scanout "
		    "buffer\n", c->ctx->name, (unsigned long)phys,
		    (unsigned long long)a->size);
		return EPERM;
	}
	snprintf(key, sizeof(key), MEECHO_DS_SCANOUT_VIR, i);
	if (ds_retrieve_u32(key, &vir) != OK ||
	    ds_retrieve_label_endpt(MEECHO_DS_FB_LABEL, &fb) != OK)
		fb = NONE;
	if ((bo = mali_bo_new_phys(&c->ctx->vm, phys, fb, (vir_bytes)vir,
	    (size_t)a->size, &r)) == NULL)
		return r;
	if ((r = handle_new(&c->ctx->bo, &c->ctx->nbo, bo,
	    &a->handle)) != OK) {
		mali_bo_unref(bo);
		return r;
	}
	a->offset = bo->va;
	log_info(&mali_log, "%s: scanout buffer %d (0x%lx, %llu bytes) at GPU "
	    "0x%llx\n", c->ctx->name, i, (unsigned long)phys,
	    (unsigned long long)a->size, (unsigned long long)bo->va);
	return OK;
}

static int
do_mmap_bo(struct call *c, void *arg)
{
	struct drm_panfrost_mmap_bo *a = arg;
	struct mali_bo *bo;

	if (a->flags != 0)
		return EINVAL;
	if ((bo = bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	/* A heap's pages are not for the processor (as under Linux). */
	if (bo->flags & PANFROST_BO_HEAP)
		return EINVAL;
	a->offset = bo->mmap_offset;
	return OK;
}

static int
do_get_bo_offset(struct call *c, void *arg)
{
	struct drm_panfrost_get_bo_offset *a = arg;
	struct mali_bo *bo;

	if ((bo = bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	a->offset = bo->va;
	return OK;
}

static int
do_madvise(struct call *c, void *arg)
{
	struct drm_panfrost_madvise *a = arg;

	if (bo_get(c->ctx, a->handle) == NULL)
		return ENOENT;
	/*
	 * Nothing is purged here: the pages are always still there.  Any
	 * advice is taken, as the board's panfrost takes it (madvise 7:
	 * retained 1, ref-api.txt).
	 */
	a->retained = 1;
	return OK;
}

/* ---- jobs ------------------------------------------------------------- */

static void
pjob_release_in(struct pjob *p)
{
	unsigned i;

	for (i = 0; i < p->nin; i++)
		fence_unref(p->in[i]);
	free(p->in);
	p->in = NULL;
	p->nin = 0;
}

/* A job of a client is over. */
static void
pjob_done(struct mali_job *j)
{
	struct pjob *p = (struct pjob *)j;
	struct ctx *c = p->ctx;
	unsigned i;

	if (j->result != OK) {
		if (c->failed < 4 || c->failed % 100 == 0) {
			if (j->fault)
				log_warn(&mali_log, "%s: the job at 0x%08llx "
				    "failed, slot status 0x%02x; GPU address "
				    "space fault 0x%08x at 0x%08llx\n", c->name,
				    (unsigned long long)j->jc, j->js_status,
				    j->fault_status,
				    (unsigned long long)j->fault_addr);
			else
				log_warn(&mali_log, "%s: the job at 0x%08llx "
				    "failed: %d, slot status 0x%02x\n", c->name,
				    (unsigned long long)j->jc, j->result,
				    j->js_status);
		}
		c->failed++;
	}
	/*
	 * The fence's error is panfrost's for a job that failed - EINVAL
	 * (ref-prime.txt: a sync file of a faulting job says -22); a wait on
	 * it still fails EIO (wait_state()).
	 */
	p->out->signaled = 1;
	p->out->error = j->result == OK ? 0 : j->result == EIO ? EINVAL :
	    j->result;
	fence_unref(p->out);
	pjob_release_in(p);
	for (i = 0; i < j->nbos; i++)
		mali_bo_unref(p->bos[i]);
	free(p);
	c->jobs--;
	if (c->state == CTX_CLOSING && c->jobs == 0)
		ctx_free(c);
	changed();
}

/* Every fence the job waits for is over: it may go to the slots. */
static int
pjob_ready(struct pjob *p)
{
	unsigned i;

	for (i = 0; i < p->nin; i++)
		if (!fence_done(p->in[i]))
			return 0;
	return 1;
}

/* Held jobs whose fences are over go to the slots, in each context's order. */
static void
release_held(void)
{
	struct pjob *p;
	int i;

	for (i = 0; i < MAX_CTX; i++)
		while ((p = TAILQ_FIRST(&ctxs[i].held)) != NULL &&
		    pjob_ready(p)) {
			TAILQ_REMOVE(&ctxs[i].held, p, hold);
			pjob_release_in(p);
			mali_job_submit(&p->job);
		}
}

/* The context is going: its held jobs never run, and fail. */
static void
cancel_held(struct ctx *c)
{
	struct pjob *p;

	while ((p = TAILQ_FIRST(&c->held)) != NULL) {
		TAILQ_REMOVE(&c->held, p, hold);
		p->job.result = ECANCELED;
		pjob_done(&p->job);
	}
}

/* Wait for f too, unless it is over or the context's own (in order). */
static int
pjob_wait_for(struct pjob *p, struct fence *f, unsigned max)
{
	unsigned i;

	if (f == NULL || fence_done(f) || f->owner == p->ctx)
		return OK;
	for (i = 0; i < p->nin; i++)
		if (p->in[i] == f)
			return OK;
	if (p->nin == max)
		return ENOMEM;
	p->in[p->nin++] = fence_ref(f);
	return OK;
}

static int
do_submit(struct call *c, void *arg)
{
	struct drm_panfrost_submit *a = arg;
	uint32_t handles[MAX_SYNCS], *bh = NULL;
	struct syncobj *out = NULL, *so;
	struct pjob *p;
	unsigned i, max;
	int r;

	if (a->jc == 0)
		return EINVAL;
	if (a->requirements & ~PANFROST_JD_REQ_FS)
		return EINVAL;
	if (a->jm_ctx_handle != 0 || a->pad != 0)
		return EINVAL;		/* no contexts of priority here */
	if (a->bo_handle_count > MAX_JOB_BOS || a->in_sync_count > MAX_SYNCS)
		return EINVAL;
	if (c->ctx->jobs >= MAX_QUEUED)
		return EAGAIN;

	/* What it waits for: a sync object without a fence is an error. */
	if ((r = from_user(c, a->in_syncs, handles,
	    a->in_sync_count * sizeof(handles[0]))) != OK)
		return r;
	for (i = 0; i < a->in_sync_count; i++) {
		if ((so = so_get(c->ctx, handles[i])) == NULL)
			return ENOENT;
		if (so->fence == NULL)
			return EINVAL;
	}
	if (a->out_sync != 0 && (out = so_get(c->ctx, a->out_sync)) == NULL)
		return ENODEV;

	if ((p = calloc(1, sizeof(*p) + a->bo_handle_count *
	    sizeof(p->bos[0]))) == NULL)
		return ENOMEM;
	max = a->in_sync_count + a->bo_handle_count;
	if ((max > 0 && (p->in = calloc(max, sizeof(*p->in))) == NULL) ||
	    (a->bo_handle_count > 0 && (bh = malloc(a->bo_handle_count *
	    sizeof(*bh))) == NULL)) {
		free(p->in);
		free(p);
		return ENOMEM;
	}
	if ((r = from_user(c, a->bo_handles, bh, a->bo_handle_count *
	    sizeof(*bh))) != OK) {
		free(bh);
		free(p->in);
		free(p);
		return r;
	}
	for (i = 0; i < a->bo_handle_count; i++) {
		if ((p->bos[i] = bo_get(c->ctx, bh[i])) == NULL) {
			while (i-- > 0)
				mali_bo_unref(p->bos[i]);
			free(bh);
			free(p->in);
			free(p);
			return ENOENT;
		}
		mali_bo_ref(p->bos[i]);
	}
	free(bh);
	p->ctx = c->ctx;

	if ((p->out = fence_new(0, c->ctx)) == NULL) {
		for (i = 0; i < a->bo_handle_count; i++)
			mali_bo_unref(p->bos[i]);
		free(p->in);
		free(p);
		return ENOMEM;
	}
	/*
	 * Others' fences it waits for: the sync objects it names, and - the
	 * implicit order of shared buffers - the last job of another context
	 * on each of its buffers.  Then it is the last job on each.
	 */
	for (i = 0; i < a->in_sync_count; i++)
		(void)pjob_wait_for(p, so_get(c->ctx, handles[i])->fence, max);
	for (i = 0; i < a->bo_handle_count; i++) {
		(void)pjob_wait_for(p, p->bos[i]->mem->fence, max);
		fence_unref(p->bos[i]->mem->fence);
		p->bos[i]->mem->fence = fence_ref(p->out);
	}
	if (out != NULL)
		syncobj_set(out, p->out);

	p->job.vm = &c->ctx->vm;
	p->job.jc = a->jc;
	p->job.req = a->requirements;
	p->job.slot = (a->requirements & PANFROST_JD_REQ_FS) ? 0 : 1;
	p->job.bos = p->bos;
	p->job.nbos = a->bo_handle_count;
	p->job.done = pjob_done;
	p->job.owner = c->ctx;
	c->ctx->jobs++;

	/* Behind a held job of its own, or held itself, or to the slots. */
	if (!TAILQ_EMPTY(&c->ctx->held) || !pjob_ready(p))
		TAILQ_INSERT_TAIL(&c->ctx->held, p, hold);
	else {
		pjob_release_in(p);
		mali_job_submit(&p->job);
	}
	/* A wait for a fence to appear may be satisfied now. */
	changed();
	return OK;
}

/* ---- waiting ---------------------------------------------------------- */

static void
wait_free(struct wait *w)
{
	unsigned i;

	fence_unref(w->fence);
	for (i = 0; i < w->count; i++)
		syncobj_unref(w->so[i]);
	free(w);
}

/*
 * Where a wait stands: EDONTREPLY while it waits, else its answer.  A
 * sync object is looked at afresh each time: WAIT_FOR_SUBMIT waits for
 * a fence to be put in it, and RESET or a new SUBMIT changes it.  A wait
 * for a buffer waits for the fence it had when the wait began: a job
 * after that one is after it too.
 */
static int
wait_state(struct wait *w, int64_t now)
{
	struct fence *f;
	unsigned i, done = 0, first = UINT_MAX;
	int error = 0;

	if (w->count == 0) {
		if (fence_done(w->fence))
			return OK;
		return now >= w->deadline ? ETIMEDOUT : EDONTREPLY;
	}

	for (i = 0; i < w->count; i++) {
		f = w->so[i]->fence;
		if (f == NULL && !(w->flags &
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
			return EINVAL;
		if (f != NULL && fence_done(f)) {
			done++;
			if (first == UINT_MAX)
				first = i;
			if (f->error != 0 && error == 0)
				error = EIO;
		}
	}
	if ((w->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) ? done == w->count :
	    done > 0) {
		w->args.first_signaled = first;
		/* The one addition to Linux: a failed job fails the wait. */
		if (w->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)
			return error;
		f = w->so[first]->fence;
		return f->error != 0 ? EIO : OK;
	}
	return now >= w->deadline ? ETIME : EDONTREPLY;
}

static void
wait_timer_fired(int UNUSED(arg))
{
	changed();
}

/* Answer every wait that has its answer; the timer for the next one. */
static void
waits_check(void)
{
	struct wait *w, *next;
	int64_t now = now_ns(), soonest = INT64_MAX, d;
	clock_t ticks;
	int r;

	for (w = TAILQ_FIRST(&waits); w != NULL; w = next) {
		next = TAILQ_NEXT(w, link);
		if ((r = wait_state(w, now)) == EDONTREPLY) {
			if (w->deadline < soonest)
				soonest = w->deadline;
			continue;
		}
		TAILQ_REMOVE(&waits, w, link);
		if (w->copyout > 0 && sys_safecopyto(w->endpt, w->grant, 0,
		    (vir_bytes)&w->args, w->copyout) != OK && r == OK)
			r = EFAULT;
		chardriver_reply_task(w->endpt, w->id, r);
		wait_free(w);
	}

	cancel_timer(&wait_timer);
	if (soonest == INT64_MAX)
		return;
	d = soonest - now;
	if (d < 0)
		d = 0;
	d = d / (1000000000LL / sys_hz()) + 1;
	ticks = d > INT_MAX / 2 ? INT_MAX / 2 : (clock_t)d;
	set_timer(&wait_timer, ticks, wait_timer_fired, 0);
}

/* What a select on a descriptor's object finds ready of ops. */
static unsigned
obj_ready(struct obj *o, unsigned ops)
{
	struct fence *f;

	switch (o->kind) {
	case OBJ_SYNCFILE:
		/* POLLIN once the fence is over, failed or not (Linux). */
		return fence_done(o->u.fence) ? (ops & CDEV_OP_RD) : 0;
	case OBJ_DMABUF:
		/* Read and write once no job uses the buffer (Linux). */
		f = o->u.mem->fence;
		return fence_done(f) ? (ops & (CDEV_OP_RD | CDEV_OP_WR)) : 0;
	default:
		return 0;
	}
}

/* Selects that waited and are answered now. */
static void
selects_check(void)
{
	unsigned i, ready;

	for (i = 0; i < MAX_OBJ; i++) {
		if (objs[i].kind == OBJ_FREE || objs[i].sel_endpt == NONE)
			continue;
		if ((ready = obj_ready(&objs[i], objs[i].sel_ops)) != 0) {
			chardriver_reply_select(objs[i].sel_endpt,
			    obj_minor(&objs[i]), ready);
			objs[i].sel_endpt = NONE;
			objs[i].sel_ops = 0;
		}
	}
}

/*
 * A fence may be over, or a sync object may hold another: held jobs that
 * may go now go, waits and selects that have their answer get it.  A job
 * given to the slots may be over at once (no window for it), and come
 * back here; the outer call then goes round again.
 */
static void
changed(void)
{
	static int busy, again;

	if (busy) {
		again = 1;
		return;
	}
	busy = 1;
	do {
		again = 0;
		release_held();
		waits_check();
		selects_check();
	} while (again);
	busy = 0;
}

/* Wait now, or later: queue w and answer EDONTREPLY. */
static int
wait_start(struct call *c, struct wait *w)
{
	int r;

	w->ctx = c->ctx;
	w->endpt = c->endpt;
	w->id = c->id;
	w->grant = c->grant;
	if ((r = wait_state(w, now_ns())) != EDONTREPLY)
		return r;		/* the caller copies the argument out */
	TAILQ_INSERT_TAIL(&waits, w, link);
	waits_check();
	return EDONTREPLY;
}

/* Until the buffer's memory is idle: its last job over (WAIT_BO). */
static int
wait_mem(struct call *c, struct mali_mem *mem, int64_t deadline)
{
	struct wait *w;
	int r;

	if (fence_done(mem->fence))
		return OK;
	/* A deadline already past is "not now", EBUSY; one reached is ETIMEDOUT. */
	if (deadline <= now_ns())
		return EBUSY;
	if ((w = calloc(1, sizeof(*w))) == NULL)
		return ENOMEM;
	w->fence = fence_ref(mem->fence);
	w->deadline = deadline;
	if ((r = wait_start(c, w)) != EDONTREPLY)
		wait_free(w);
	return r;
}

static int
do_wait_bo(struct call *c, void *arg)
{
	struct drm_panfrost_wait_bo *a = arg;
	struct mali_bo *bo;

	if (a->pad != 0)
		return EINVAL;
	if ((bo = bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	return wait_mem(c, bo->mem, a->timeout_ns);
}

static int
do_syncobj_create(struct call *c, void *arg)
{
	struct drm_syncobj_create *a = arg;
	struct syncobj *so;
	int r;

	if (a->flags & ~DRM_SYNCOBJ_CREATE_SIGNALED)
		return EINVAL;
	if ((so = calloc(1, sizeof(*so))) == NULL)
		return ENOMEM;
	so->refs = 1;
	so->minor = -1;
	if ((a->flags & DRM_SYNCOBJ_CREATE_SIGNALED) &&
	    (so->fence = fence_new(1, NULL)) == NULL) {
		free(so);
		return ENOMEM;
	}
	if ((r = handle_new(&c->ctx->so, &c->ctx->nso, so,
	    &a->handle)) != OK) {
		syncobj_unref(so);
		return r;
	}
	return OK;
}

static int
do_syncobj_destroy(struct call *c, void *arg)
{
	struct drm_syncobj_destroy *a = arg;
	struct syncobj *so;

	if (a->pad != 0)
		return EINVAL;
	if ((so = so_get(c->ctx, a->handle)) == NULL)
		return EINVAL;
	c->ctx->so[a->handle - 1] = NULL;
	syncobj_unref(so);
	return OK;
}

/* The handles an array request names, each to a sync object. */
static int
syncobj_array(struct call *c, uint64_t ptr, uint32_t count,
	struct syncobj **so)
{
	uint32_t h[MAX_SYNCS];
	unsigned i;
	int r;

	if (count == 0 || count > MAX_SYNCS)
		return EINVAL;
	if ((r = from_user(c, ptr, h, count * sizeof(h[0]))) != OK)
		return r;
	for (i = 0; i < count; i++)
		if ((so[i] = so_get(c->ctx, h[i])) == NULL)
			return ENOENT;
	return OK;
}

static int
do_syncobj_wait(struct call *c, void *arg)
{
	struct drm_syncobj_wait *a = arg;
	struct wait *w;
	unsigned i;
	int r;

	if (a->flags & ~(DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
	    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
	    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE))
		return EINVAL;
	if ((w = calloc(1, sizeof(*w))) == NULL)
		return ENOMEM;
	if ((r = syncobj_array(c, a->handles, a->count_handles, w->so)) !=
	    OK) {
		free(w);
		return r;
	}
	w->count = a->count_handles;
	for (i = 0; i < w->count; i++)
		w->so[i]->refs++;
	w->flags = a->flags;
	w->deadline = a->timeout_nsec;
	w->args = *a;
	w->copyout = c->usize < sizeof(*a) ? c->usize : sizeof(*a);
	if ((r = wait_start(c, w)) != EDONTREPLY) {
		a->first_signaled = w->args.first_signaled;
		wait_free(w);
	}
	return r;
}

static int
do_syncobj_reset_signal(struct call *c, void *arg, int signal)
{
	struct drm_syncobj_array *a = arg;
	struct syncobj *so[MAX_SYNCS];
	struct fence *f = NULL;
	unsigned i;
	int r;

	if (a->pad != 0)
		return EINVAL;
	if ((r = syncobj_array(c, a->handles, a->count_handles, so)) != OK)
		return r;
	if (signal && (f = fence_new(1, NULL)) == NULL)
		return ENOMEM;
	for (i = 0; i < a->count_handles; i++)
		syncobj_set(so[i], f);
	fence_unref(f);
	changed();
	return OK;
}

static int
do_syncobj_reset(struct call *c, void *arg)
{
	return do_syncobj_reset_signal(c, arg, 0);
}

static int
do_syncobj_signal(struct call *c, void *arg)
{
	return do_syncobj_reset_signal(c, arg, 1);
}

/*
 * The timeline calls: panfrost has no timeline sync objects
 * (DRIVER_SYNCOBJ_TIMELINE), and Linux answers these EOPNOTSUPP for it.
 */
static int
do_timeline(struct call *c, void *arg)
{
	return EOPNOTSUPP;
}

/* ---- descriptors: PRIME and sync files ---------------------------------- */

/*
 * PRIME_HANDLE_TO_FD: the buffer as a dma-buf descriptor.  DRM_CLOEXEC and
 * DRM_RDWR are O_CLOEXEC and O_RDWR (drm.h).  A heap is exported as the
 * board's panfrost (6.1) exports it (ref-prime.txt), but nobody can import
 * or map it: its pieces are its context's, there is nothing to share.
 */
static int
do_prime_handle_to_fd(struct call *c, void *arg)
{
	struct drm_prime_handle *a = arg;
	struct mali_bo *bo;

	if (a->flags & ~(DRM_CLOEXEC | DRM_RDWR))
		return EINVAL;
	if ((bo = bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	return obj_export(c, OBJ_DMABUF, bo->mem, (a->flags & DRM_CLOEXEC) |
	    ((a->flags & DRM_RDWR) ? O_RDWR : O_RDONLY),
	    (uint64_t)bo->mem->size, &a->fd);
}

/*
 * PRIME_FD_TO_HANDLE: a dma-buf descriptor of the caller's as a buffer of
 * this context: the handle it has for that memory already, or a new one
 * with the memory placed in this context's address space.
 */
static int
do_prime_fd_to_handle(struct call *c, void *arg)
{
	struct drm_prime_handle *a = arg;
	struct mali_bo *bo;
	struct obj *o;
	unsigned i;
	int r;

	if ((r = obj_import(c, a->fd, OBJ_DMABUF, &o)) != OK)
		return r;
	for (i = 1; i <= c->ctx->nbo; i++)
		if ((bo = bo_get(c->ctx, i)) != NULL && bo->mem == o->u.mem) {
			a->handle = i;
			return OK;
		}
	if ((bo = mali_bo_import(&c->ctx->vm, o->u.mem, &r)) == NULL)
		return r;
	if ((r = handle_new(&c->ctx->bo, &c->ctx->nbo, bo,
	    &a->handle)) != OK) {
		mali_bo_unref(bo);
		return r;
	}
	return OK;
}

/*
 * SYNCOBJ_HANDLE_TO_FD: with EXPORT_SYNC_FILE, the fence the sync object
 * holds as a sync file (none: EINVAL); without, the sync object itself,
 * shared by whoever imports it.  Both close-on-exec, as Linux makes them.
 */
static int
do_syncobj_handle_to_fd(struct call *c, void *arg)
{
	struct drm_syncobj_handle *a = arg;
	struct syncobj *so;

	if (a->pad != 0)
		return EINVAL;
	if (a->flags != 0 &&
	    a->flags != DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return EINVAL;
	so = so_get(c->ctx, a->handle);
	if (a->flags & DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE) {
		if (so == NULL)
			return ENOENT;
		if (so->fence == NULL)
			return EINVAL;
		return obj_export(c, OBJ_SYNCFILE, so->fence,
		    O_RDONLY | O_CLOEXEC, 0, &a->fd);
	}
	if (so == NULL)
		return EINVAL;
	return obj_export(c, OBJ_SYNCOBJ, so, O_RDONLY | O_CLOEXEC, 0, &a->fd);
}

/*
 * SYNCOBJ_FD_TO_HANDLE: with IMPORT_SYNC_FILE, the sync file's fence into
 * the sync object (its fence replaced); without, a handle of this
 * context to the sync object the descriptor is.
 */
static int
do_syncobj_fd_to_handle(struct call *c, void *arg)
{
	struct drm_syncobj_handle *a = arg;
	struct syncobj *so;
	struct obj *o;
	int r;

	if (a->pad != 0)
		return EINVAL;
	if (a->flags != 0 &&
	    a->flags != DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return EINVAL;
	if (a->flags & DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE) {
		if (obj_import(c, a->fd, OBJ_SYNCFILE, &o) != OK)
			return EINVAL;	/* none, or not a sync file */
		if ((so = so_get(c->ctx, a->handle)) == NULL)
			return ENOENT;
		syncobj_set(so, o->u.fence);
		changed();
		return OK;
	}
	if (obj_import(c, a->fd, OBJ_SYNCOBJ, &o) != OK)
		return EINVAL;		/* none, or not a sync object */
	if ((r = handle_new(&c->ctx->so, &c->ctx->nso, o->u.so,
	    &a->handle)) != OK)
		return r;
	o->u.so->refs++;
	return OK;
}

/* SYNC_IOC_MERGE: a new sync file, over when both are. */
static int
do_sync_merge(struct call *c, void *arg)
{
	struct sync_merge_data *a = arg;
	struct fence *f;
	struct obj *o2;
	int r;

	if (a->flags != 0 || a->pad != 0)
		return EINVAL;
	if (obj_import(c, a->fd2, OBJ_SYNCFILE, &o2) != OK)
		return ENOENT;		/* none, or not a sync file */
	if ((f = fence_merge(c->obj->u.fence, o2->u.fence)) == NULL)
		return ENOMEM;
	r = obj_export(c, OBJ_SYNCFILE, f, O_RDONLY | O_CLOEXEC, 0,
	    &a->fence);
	fence_unref(f);
	return r;
}

/* Minus the error, whichever sign the error codes have where this runs. */
static int
neg(int e)
{
	return e < 0 ? e : -e;
}

/*
 * SYNC_IOC_FILE_INFO: status 1 over, 0 not yet, below zero failed (minus
 * the error); with room for them, one entry per fence.
 */
static int
do_sync_file_info(struct call *c, void *arg)
{
	struct sync_file_info *a = arg;
	struct sync_fence_info fi;
	struct fence *f = c->obj->u.fence, *leaf;
	unsigned i, n = fence_count(f);
	int r;

	if (a->flags != 0 || a->pad != 0)
		return EINVAL;
	strlcpy(a->name, "mali", sizeof(a->name));
	a->status = !fence_done(f) ? 0 : f->error != 0 ? neg(f->error) : 1;
	if (a->num_fences == 0) {
		a->num_fences = n;
		return OK;
	}
	if (a->num_fences < n)
		return EINVAL;
	for (i = 0; i < n; i++) {
		leaf = f->nchild > 0 ? f->child[i] : f;
		memset(&fi, 0, sizeof(fi));
		strlcpy(fi.obj_name, "mali-job", sizeof(fi.obj_name));
		strlcpy(fi.driver_name, "mali", sizeof(fi.driver_name));
		fi.status = !leaf->signaled ? 0 : leaf->error != 0 ?
		    neg(leaf->error) : 1;
		if ((r = to_user(c, &fi, a->sync_fence_info + i * sizeof(fi),
		    sizeof(fi))) != OK)
			return r;
	}
	a->num_fences = n;
	return OK;
}

/*
 * DMA_BUF_IOCTL_SYNC: a processor's access to the buffer.  START waits
 * for the jobs that use it, as Linux's dma_buf_begin_cpu_access() waits
 * for its fences; the caches need nothing more - every job that uses a
 * buffer a processor maps is cleaned around (mali_bo.c).
 */
static int
do_dmabuf_sync(struct call *c, void *arg)
{
	struct dma_buf_sync *a = arg;

	if (a->flags & ~DMA_BUF_SYNC_VALID_FLAGS_MASK)
		return EINVAL;
	if ((a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if (a->flags & DMA_BUF_SYNC_END)
		return OK;
	return wait_mem(c, c->obj->u.mem, INT64_MAX);
}

/* DMA_BUF_IOCTL_EXPORT_SYNC_FILE: the jobs that use the buffer, a sync file. */
static int
do_dmabuf_export(struct call *c, void *arg)
{
	struct dma_buf_export_sync_file *a = arg;
	struct fence *f;
	int r;

	if ((a->flags & ~DMA_BUF_SYNC_RW) || (a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if ((f = fence_merge(c->obj->u.mem->fence, NULL)) == NULL)
		return ENOMEM;
	r = obj_export(c, OBJ_SYNCFILE, f, O_RDONLY | O_CLOEXEC, 0, &a->fd);
	fence_unref(f);
	return r;
}

/*
 * DMA_BUF_IOCTL_IMPORT_SYNC_FILE: a fence the next jobs using the buffer
 * wait for, and so does WAIT_BO - added to what the buffer had.
 */
static int
do_dmabuf_import(struct call *c, void *arg)
{
	struct dma_buf_import_sync_file *a = arg;
	struct mali_mem *mem = c->obj->u.mem;
	struct fence *f;
	struct obj *o;

	if ((a->flags & ~DMA_BUF_SYNC_RW) || (a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if (obj_import(c, a->fd, OBJ_SYNCFILE, &o) != OK)
		return EINVAL;		/* none, or not a sync file */
	if ((f = fence_merge(mem->fence, o->u.fence)) == NULL)
		return ENOMEM;
	fence_unref(mem->fence);
	mem->fence = f;
	return OK;
}

/* ---- the table -------------------------------------------------------- */

struct calldef {
	unsigned nr;
	size_t size;			/* this service's structure */
	int (*fn)(struct call *, void *);
};

/* A context's: DRM's group 'd'. */
static const struct calldef ctx_calls[] = {
	{ 0x00, sizeof(struct drm_version),		do_version },
	{ 0x09, sizeof(struct drm_gem_close),		do_gem_close },
	{ 0x0c, sizeof(struct drm_get_cap),		do_get_cap },
	{ 0x2d, sizeof(struct drm_prime_handle),	do_prime_handle_to_fd },
	{ 0x2e, sizeof(struct drm_prime_handle),	do_prime_fd_to_handle },
	{ 0xbf, sizeof(struct drm_syncobj_create),	do_syncobj_create },
	{ 0xc0, sizeof(struct drm_syncobj_destroy),	do_syncobj_destroy },
	{ 0xc1, sizeof(struct drm_syncobj_handle),	do_syncobj_handle_to_fd },
	{ 0xc2, sizeof(struct drm_syncobj_handle),	do_syncobj_fd_to_handle },
	{ 0xc3, sizeof(struct drm_syncobj_wait),	do_syncobj_wait },
	{ 0xc4, sizeof(struct drm_syncobj_array),	do_syncobj_reset },
	{ 0xc5, sizeof(struct drm_syncobj_array),	do_syncobj_signal },
	{ 0xca, 0,					do_timeline },
	{ 0xcb, 0,					do_timeline },
	{ 0xcc, 0,					do_timeline },
	{ 0xcd, 0,					do_timeline },
	{ 0xcf, 0,					do_timeline },
	{ DRM_COMMAND_BASE + DRM_PANFROST_SUBMIT,
	    sizeof(struct drm_panfrost_submit),		do_submit },
	{ DRM_COMMAND_BASE + DRM_PANFROST_WAIT_BO,
	    sizeof(struct drm_panfrost_wait_bo),	do_wait_bo },
	{ DRM_COMMAND_BASE + DRM_PANFROST_CREATE_BO,
	    sizeof(struct drm_panfrost_create_bo),	do_create_bo },
	{ DRM_COMMAND_BASE + DRM_PANFROST_MMAP_BO,
	    sizeof(struct drm_panfrost_mmap_bo),	do_mmap_bo },
	{ DRM_COMMAND_BASE + DRM_PANFROST_GET_PARAM,
	    sizeof(struct drm_panfrost_get_param),	do_get_param },
	{ DRM_COMMAND_BASE + DRM_PANFROST_GET_BO_OFFSET,
	    sizeof(struct drm_panfrost_get_bo_offset),	do_get_bo_offset },
	{ DRM_COMMAND_BASE + DRM_PANFROST_MADVISE,
	    sizeof(struct drm_panfrost_madvise),	do_madvise },
	{ DRM_COMMAND_BASE + DRM_MEECHO_SCANOUT_IMPORT,
	    sizeof(struct drm_meecho_scanout_import),	do_scanout_import },
	{ 0, 0, NULL }
};

/* A sync file's: group '>'. */
static const struct calldef syncfile_calls[] = {
	{ 3, sizeof(struct sync_merge_data),		do_sync_merge },
	{ 4, sizeof(struct sync_file_info),		do_sync_file_info },
	{ 0, 0, NULL }
};

/* A dma-buf's: group 'b'. */
static const struct calldef dmabuf_calls[] = {
	{ 0, sizeof(struct dma_buf_sync),		do_dmabuf_sync },
	{ 2, sizeof(struct dma_buf_export_sync_file),	do_dmabuf_export },
	{ 3, sizeof(struct dma_buf_import_sync_file),	do_dmabuf_import },
	{ 0, 0, NULL }
};

/*
 * mmap(2) of the device: of a context, the buffer whose MMAP_BO token
 * cm_offset is; of a dma-buf, its buffer from offset 0.  Whole, shared,
 * not executable: VM lends a region whole.
 */
static int
do_mmap(struct call *c)
{
	struct cdev_mmap cm;
	struct mali_mem *mem = NULL;
	struct mali_bo *bo;
	size_t len;
	unsigned i;
	int r;

	if (c->usize < sizeof(cm))
		return EINVAL;
	if ((r = sys_safecopyfrom(c->endpt, c->grant, 0, (vir_bytes)&cm,
	    sizeof(cm))) != OK)
		return r;
	if (c->ctx != NULL) {
		log_debug(&mali_log, "%s: mmap of 0x%llx, %zu bytes, prot 0x%x "
		    "flags 0x%x, for %d\n", c->ctx->name,
		    (unsigned long long)cm.cm_offset, cm.cm_len, cm.cm_prot,
		    cm.cm_flags, c->user);
		for (i = 1; i <= c->ctx->nbo && mem == NULL; i++)
			if ((bo = bo_get(c->ctx, i)) != NULL &&
			    bo->mmap_offset == (uint64_t)cm.cm_offset &&
			    !(bo->flags & PANFROST_BO_HEAP))
				mem = bo->mem;
	} else if (c->obj->kind == OBJ_DMABUF && cm.cm_offset == 0)
		mem = c->obj->u.mem;
	if (mem == NULL)
		return EINVAL;
	len = (cm.cm_len + MALI_PAGE - 1) & ~((size_t)MALI_PAGE - 1);
	if (len != mem->size || !(cm.cm_flags & MAP_SHARED) ||
	    (cm.cm_flags & MAP_PRIVATE) || (cm.cm_prot & PROT_EXEC))
		return EINVAL;
	if ((r = mali_mem_map_into(mem, c->user,
	    (cm.cm_flags & MAP_FIXED) ? cm.cm_addr : NULL,
	    (cm.cm_prot & PROT_WRITE) != 0, &cm.cm_ret)) != OK)
		return r;
	return sys_safecopyto(c->endpt, c->grant, 0, (vir_bytes)&cm,
	    sizeof(cm));
}

static int
mali_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int UNUSED(flags), endpoint_t user, cdev_id_t id)
{
	union {
		struct drm_version version;
		struct drm_panfrost_submit submit;
		struct drm_syncobj_wait wait;
		struct sync_file_info info;
		char raw[128];
	} arg;
	const struct calldef *t;
	struct call c;
	size_t n;
	unsigned nr;
	unsigned long group;
	int r;

	memset(&c, 0, sizeof(c));
	if (minor >= 0 && minor < MAX_CTX && ctxs[minor].state == CTX_OPEN) {
		c.ctx = &ctxs[minor];
		group = DRM_IOCTL_BASE;
		t = ctx_calls;
	} else if ((c.obj = obj_get(minor, OBJ_FREE)) != NULL) {
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
		log_debug(&mali_log, "minor %d: ioctl 0x%lx is not this "
		    "device's\n", minor, request);
		return ENOTTY;
	}

	nr = request & 0xff;
	for (; t->fn != NULL; t++)
		if (t->nr == nr)
			break;
	if (t->fn == NULL)
		return c.ctx != NULL ? EINVAL : ENOTTY;

	memset(&arg, 0, sizeof(arg));
	n = c.usize < t->size ? c.usize : t->size;
	if ((request & IOC_IN) && n > 0 && sys_safecopyfrom(endpt, grant, 0,
	    (vir_bytes)&arg, n) != OK)
		return EFAULT;

	r = t->fn(&c, &arg);

	/*
	 * Out whatever the answer, as drm_ioctl() does - a wait that failed
	 * still says which object it found - unless the call waits; then
	 * its answer copies it (waits_check()).
	 */
	if (r != EDONTREPLY && (request & IOC_OUT) && n > 0 &&
	    sys_safecopyto(endpt, grant, 0, (vir_bytes)&arg, n) != OK &&
	    r == OK)
		r = EFAULT;
	return r;
}

/* ---- open, close, cancel, select ------------------------------------- */

static int
mali_open(devminor_t minor, int UNUSED(access), endpoint_t user)
{
	struct ctx *c;
	int i;

	if (minor != RENDER_MINOR)
		return ENXIO;
	for (i = 0; i < MAX_CTX; i++)
		if (ctxs[i].state == CTX_FREE)
			break;
	if (i == MAX_CTX)
		return ENFILE;
	c = &ctxs[i];
	memset(c, 0, sizeof(*c));
	c->minor = i;
	TAILQ_INIT(&c->held);
	snprintf(c->name, sizeof(c->name), "client %d", i);
	if (mali_vm_init(&c->vm, c->name) != OK)
		return ENOMEM;
	c->state = CTX_OPEN;
	log_debug(&mali_log, "%s opened by %d\n", c->name, user);
	return CDEV_CLONED | i;
}

/*
 * The last descriptor of a context closed.  What has not started is
 * cancelled; what runs runs to its end, and the context goes with its
 * last job (pjob_done()): its buffers and tables must not go while the
 * GPU may use them.  The last descriptor of a descriptor's object
 * closed: what it holds goes.
 */
static int
mali_close(devminor_t minor)
{
	struct ctx *c;
	struct obj *o;
	struct wait *w, *next;

	if ((o = obj_get(minor, OBJ_FREE)) != NULL) {
		if (o->opens > 0 && --o->opens == 0)
			obj_free(o);
		return OK;
	}
	if (minor < 0 || minor >= MAX_CTX || ctxs[minor].state != CTX_OPEN)
		return ENXIO;
	c = &ctxs[minor];
	for (w = TAILQ_FIRST(&waits); w != NULL; w = next) {
		next = TAILQ_NEXT(w, link);
		if (w->ctx == c) {
			TAILQ_REMOVE(&waits, w, link);
			chardriver_reply_task(w->endpt, w->id, EINTR);
			wait_free(w);
		}
	}
	c->state = CTX_CLOSING;
	c->jobs++;			/* not freed under cancel_held() */
	cancel_held(c);
	c->jobs--;
	mali_sched_cancel(&c->vm);	/* may free c, through pjob_done() */
	if (c->state == CTX_CLOSING && c->jobs == 0)
		ctx_free(c);
	return OK;
}

/* A signal for a process that waits: the wait ends, EINTR. */
static int
mali_cancel(devminor_t UNUSED(minor), endpoint_t endpt, cdev_id_t id)
{
	struct wait *w;

	TAILQ_FOREACH(w, &waits, link)
		if (w->endpt == endpt && w->id == id) {
			TAILQ_REMOVE(&waits, w, link);
			wait_free(w);
			return EINTR;
		}
	return EDONTREPLY;
}

/*
 * select(2) and poll(2): a sync file is readable once its fence is over;
 * a dma-buf readable and writable once no job uses it; a context never
 * (a render node has no events).  Asked to tell, it tells once.
 */
static int
mali_select(devminor_t minor, unsigned int ops, endpoint_t endpt)
{
	struct obj *o;
	unsigned want = ops & (CDEV_OP_RD | CDEV_OP_WR | CDEV_OP_ERR), ready;

	if ((o = obj_get(minor, OBJ_FREE)) == NULL)
		return (minor >= 0 && minor < MAX_CTX) ? 0 : ENXIO;
	ready = obj_ready(o, want);
	if (ready == 0 && (ops & CDEV_NOTIFY)) {
		o->sel_endpt = endpt;
		o->sel_ops |= want;
	}
	return ready;
}

/* A buffer's memory goes: the fence it held goes with it (mali_bo.c). */
static void
mem_gone(struct mali_mem *mem)
{
	fence_unref(mem->fence);
	mem->fence = NULL;
}

struct chardriver mali_chardriver = {
	.cdr_open	= mali_open,
	.cdr_close	= mali_close,
	.cdr_ioctl	= mali_ioctl,
	.cdr_cancel	= mali_cancel,
	.cdr_select	= mali_select,
};

void
mali_drm_init(void)
{
	int i;

	init_timer(&wait_timer);
	for (i = 0; i < MAX_CTX; i++)
		TAILQ_INIT(&ctxs[i].held);
	mali_mem_gone = mem_gone;
}

/* The service is going: every wait answered, every context freed. */
void
mali_drm_shutdown(void)
{
	struct wait *w;
	int i;

	while ((w = TAILQ_FIRST(&waits)) != NULL) {
		TAILQ_REMOVE(&waits, w, link);
		chardriver_reply_task(w->endpt, w->id, EIO);
		wait_free(w);
	}
	for (i = 0; i < MAX_CTX; i++) {
		if (ctxs[i].state == CTX_FREE)
			continue;
		ctxs[i].jobs++;
		cancel_held(&ctxs[i]);
		ctxs[i].jobs--;
		if (ctxs[i].jobs == 0)
			ctx_free(&ctxs[i]);
	}
}
