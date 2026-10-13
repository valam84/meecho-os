/*
 * The device: /dev/dri/renderD128, the interface Mesa's Panfrost driver
 * talks to (panfrost_drm.h, and the few DRM calls every render node
 * has).  Stage 11.6.
 *
 * A client opens the render node and gets a context of its own - a cloned
 * minor, the way /dev/ptmx gives a terminal of one's own - and each
 * context is what a DRM file is under Linux: its own GPU address space,
 * its own handles for buffers and sync objects, its own jobs.  Nothing in
 * one context can name anything in another.
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
 *
 * Not yet: PRIME (buffers as descriptors, for the compositor), sync
 * files, timeline sync objects, the performance counters; asked for,
 * they say EINVAL, as Linux does for what a driver lacks.
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
 * error in the fence for a sync file to show.  WAIT_BO stays as Linux has
 * it, 0 once the buffer is idle, because Mesa asserts that it only ever
 * fails with ETIMEDOUT or EBUSY.  The job's slot status and fault go to
 * the log.
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/ioc_mmap.h>
#include <sys/ioccom.h>
#include <sys/mman.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"
#include "drm.h"
#include "panfrost_drm.h"

#define RENDER_MINOR	128		/* /dev/dri/renderD128 */
#define MAX_CTX		16		/* contexts, minors 0..15 */
#define MAX_HANDLES	65536		/* buffers, and sync objects, each */
#define MAX_JOB_BOS	4096		/* buffers one job may name */
#define MAX_SYNCS	64		/* sync objects one call may name */
#define MAX_QUEUED	256		/* jobs a context may have waiting */

/* ---- fences and sync objects ------------------------------------------ */

/* Something that becomes signalled once, with an error or without. */
struct fence {
	unsigned refs;
	int signaled;
	int error;			/* 0, or why the job failed */
};

struct syncobj {
	unsigned refs;			/* the handle, and waits */
	struct fence *fence;		/* NULL: none */
};

static struct fence *
fence_new(int signaled)
{
	struct fence *f;

	if ((f = calloc(1, sizeof(*f))) == NULL)
		return NULL;
	f->refs = 1;
	f->signaled = signaled;
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
	if (f != NULL && --f->refs == 0)
		free(f);
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

/* ---- contexts --------------------------------------------------------- */

#define CTX_FREE	0
#define CTX_OPEN	1
#define CTX_CLOSING	2		/* closed, its last job not over */

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
};

static struct ctx ctxs[MAX_CTX];

/* A job of a client: the job, and what it holds. */
struct pjob {
	struct mali_job job;		/* first: done() gets this */
	struct ctx *ctx;
	struct fence *out;		/* signalled when it is over */
	struct mali_bo *bos[];
};

/* A call that waits: WAIT_BO or SYNCOBJ_WAIT. */
struct wait {
	TAILQ_ENTRY(wait) link;
	struct ctx *ctx;
	endpoint_t endpt;		/* VFS, to reply to */
	cdev_id_t id;
	cp_grant_id_t grant;		/* for SYNCOBJ_WAIT's first_signaled */
	size_t copyout;
	int64_t deadline;		/* ns of CLOCK_MONOTONIC; INT64_MAX */
	struct mali_bo *bo;		/* WAIT_BO */
	struct syncobj *so[MAX_SYNCS];	/* SYNCOBJ_WAIT */
	unsigned count;
	uint32_t flags;
	struct drm_syncobj_wait args;
};

static TAILQ_HEAD(, wait) waits = TAILQ_HEAD_INITIALIZER(waits);
static minix_timer_t wait_timer;

static void waits_check(void);

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
	struct ctx *ctx;
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

static int
do_get_cap(struct call *c, void *arg)
{
	struct drm_get_cap *g = arg;

	switch (g->capability) {
	case DRM_CAP_SYNCOBJ:
		g->value = 1;
		return OK;
	case DRM_CAP_PRIME:
		g->value = 0;		/* no buffers as descriptors yet */
		return OK;
	case DRM_CAP_TIMESTAMP_MONOTONIC:
		g->value = 1;
		return OK;
	default:
		return EINVAL;
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
	if (a->madv != PANFROST_MADV_WILLNEED &&
	    a->madv != PANFROST_MADV_DONTNEED)
		return EINVAL;
	/* Nothing is purged here: the pages are always still there. */
	a->retained = 1;
	return OK;
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
	if (p->out != NULL) {
		p->out->signaled = 1;
		p->out->error = j->result == OK ? 0 : EIO;
		fence_unref(p->out);
	}
	for (i = 0; i < j->nbos; i++)
		mali_bo_unref(p->bos[i]);
	free(p);
	c->jobs--;
	if (c->state == CTX_CLOSING && c->jobs == 0)
		ctx_free(c);
	waits_check();
}

static int
do_submit(struct call *c, void *arg)
{
	struct drm_panfrost_submit *a = arg;
	uint32_t handles[MAX_SYNCS], *bh = NULL;
	struct syncobj *out = NULL, *so;
	struct pjob *p;
	unsigned i;
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

	/*
	 * What it waits for.  Jobs of one context run in the order given,
	 * so a fence of the context's own is over before this job starts;
	 * there is nobody else's to wait for (no sync files yet).  A sync
	 * object without a fence is an error, as under Linux.
	 */
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
	if (a->bo_handle_count > 0 && (bh = malloc(a->bo_handle_count *
	    sizeof(*bh))) == NULL) {
		free(p);
		return ENOMEM;
	}
	if ((r = from_user(c, a->bo_handles, bh, a->bo_handle_count *
	    sizeof(*bh))) != OK) {
		free(bh);
		free(p);
		return r;
	}
	for (i = 0; i < a->bo_handle_count; i++) {
		if ((p->bos[i] = bo_get(c->ctx, bh[i])) == NULL) {
			while (i-- > 0)
				mali_bo_unref(p->bos[i]);
			free(bh);
			free(p);
			return ENOENT;
		}
		mali_bo_ref(p->bos[i]);
	}
	free(bh);

	if (out != NULL && (p->out = fence_new(0)) == NULL) {
		for (i = 0; i < a->bo_handle_count; i++)
			mali_bo_unref(p->bos[i]);
		free(p);
		return ENOMEM;
	}
	if (out != NULL)
		syncobj_set(out, p->out);

	p->ctx = c->ctx;
	p->job.vm = &c->ctx->vm;
	p->job.jc = a->jc;
	p->job.req = a->requirements;
	p->job.slot = (a->requirements & PANFROST_JD_REQ_FS) ? 0 : 1;
	p->job.bos = p->bos;
	p->job.nbos = a->bo_handle_count;
	p->job.done = pjob_done;
	p->job.owner = c->ctx;
	c->ctx->jobs++;
	mali_job_submit(&p->job);
	/* A wait for a fence to appear may be satisfied now. */
	waits_check();
	return OK;
}

/* ---- waiting ---------------------------------------------------------- */

static void
wait_free(struct wait *w)
{
	unsigned i;

	if (w->bo != NULL)
		mali_bo_unref(w->bo);
	for (i = 0; i < w->count; i++)
		syncobj_unref(w->so[i]);
	free(w);
}

/*
 * Where a wait stands: EDONTREPLY while it waits, else its answer.  A
 * sync object is looked at afresh each time: WAIT_FOR_SUBMIT waits for
 * a fence to be put in it, and RESET or a new SUBMIT changes it.
 */
static int
wait_state(struct wait *w, int64_t now)
{
	struct fence *f;
	unsigned i, done = 0, first = UINT_MAX;
	int error = 0;

	if (w->bo != NULL) {
		if (w->bo->busy == 0)
			return OK;
		return now >= w->deadline ? ETIMEDOUT : EDONTREPLY;
	}

	for (i = 0; i < w->count; i++) {
		f = w->so[i]->fence;
		if (f == NULL && !(w->flags &
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
			return EINVAL;
		if (f != NULL && f->signaled) {
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
	waits_check();
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

static int
do_wait_bo(struct call *c, void *arg)
{
	struct drm_panfrost_wait_bo *a = arg;
	struct mali_bo *bo;
	struct wait *w;
	int r;

	if (a->pad != 0)
		return EINVAL;
	if ((bo = bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	if (bo->busy == 0)
		return OK;
	/* A deadline already past is "not now", EBUSY; one reached is ETIMEDOUT. */
	if (a->timeout_ns <= now_ns())
		return EBUSY;
	if ((w = calloc(1, sizeof(*w))) == NULL)
		return ENOMEM;
	w->bo = bo;
	mali_bo_ref(bo);
	w->deadline = a->timeout_ns;
	if ((r = wait_start(c, w)) != EDONTREPLY)
		wait_free(w);
	return r;
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
	if ((a->flags & DRM_SYNCOBJ_CREATE_SIGNALED) &&
	    (so->fence = fence_new(1)) == NULL) {
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
	if (signal && (f = fence_new(1)) == NULL)
		return ENOMEM;
	for (i = 0; i < a->count_handles; i++)
		syncobj_set(so[i], f);
	fence_unref(f);
	waits_check();
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

/* ---- the table -------------------------------------------------------- */

static const struct {
	unsigned nr;
	size_t size;			/* this service's structure */
	int (*fn)(struct call *, void *);
} calls[] = {
	{ 0x00, sizeof(struct drm_version),		do_version },
	{ 0x09, sizeof(struct drm_gem_close),		do_gem_close },
	{ 0x0c, sizeof(struct drm_get_cap),		do_get_cap },
	{ 0xbf, sizeof(struct drm_syncobj_create),	do_syncobj_create },
	{ 0xc0, sizeof(struct drm_syncobj_destroy),	do_syncobj_destroy },
	{ 0xc3, sizeof(struct drm_syncobj_wait),	do_syncobj_wait },
	{ 0xc4, sizeof(struct drm_syncobj_array),	do_syncobj_reset },
	{ 0xc5, sizeof(struct drm_syncobj_array),	do_syncobj_signal },
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
};

/* mmap(2) of the device: the buffer whose MMAP_BO token cm_offset is. */
static int
do_mmap(struct call *c)
{
	struct cdev_mmap cm;
	struct mali_bo *bo = NULL;
	size_t len;
	unsigned i;
	int r;

	if (c->usize < sizeof(cm))
		return EINVAL;
	if ((r = sys_safecopyfrom(c->endpt, c->grant, 0, (vir_bytes)&cm,
	    sizeof(cm))) != OK)
		return r;
	for (i = 1; i <= c->ctx->nbo && bo == NULL; i++)
		if ((bo = bo_get(c->ctx, i)) != NULL &&
		    bo->mmap_offset != (uint64_t)cm.cm_offset)
			bo = NULL;
	if (bo == NULL)
		return EINVAL;
	len = (cm.cm_len + MALI_PAGE - 1) & ~((size_t)MALI_PAGE - 1);
	/* The whole buffer, shared, and nothing to execute. */
	if (len != bo->size || !(cm.cm_flags & MAP_SHARED) ||
	    (cm.cm_flags & MAP_PRIVATE) || (cm.cm_prot & PROT_EXEC) ||
	    (bo->flags & PANFROST_BO_HEAP))
		return EINVAL;
	if ((r = mali_bo_map_into(bo, c->user,
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
		char raw[128];
	} arg;
	struct call c;
	size_t n;
	unsigned i, nr;
	int r;

	if (minor < 0 || minor >= MAX_CTX || ctxs[minor].state != CTX_OPEN)
		return ENXIO;

	c.ctx = &ctxs[minor];
	c.endpt = endpt;
	c.grant = grant;
	c.usize = IOCPARM_LEN(request);
	c.user = user;
	c.id = id;

	if (IOCBASECMD(request) == IOCBASECMD(MIOCMMAP))
		return do_mmap(&c);
	if (IOCGROUP(request) != DRM_IOCTL_BASE)
		return ENOTTY;

	nr = request & 0xff;
	for (i = 0; i < sizeof(calls) / sizeof(calls[0]); i++)
		if (calls[i].nr == nr)
			break;
	if (i == sizeof(calls) / sizeof(calls[0]))
		return EINVAL;

	memset(&arg, 0, sizeof(arg));
	n = c.usize < calls[i].size ? c.usize : calls[i].size;
	if ((request & IOC_IN) && n > 0 && sys_safecopyfrom(endpt, grant, 0,
	    (vir_bytes)&arg, n) != OK)
		return EFAULT;

	r = calls[i].fn(&c, &arg);

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

/* ---- open, close, cancel ---------------------------------------------- */

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
 * GPU may use them.
 */
static int
mali_close(devminor_t minor)
{
	struct ctx *c;
	struct wait *w, *next;

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

struct chardriver mali_chardriver = {
	.cdr_open	= mali_open,
	.cdr_close	= mali_close,
	.cdr_ioctl	= mali_ioctl,
	.cdr_cancel	= mali_cancel,
};

void
mali_drm_init(void)
{
	init_timer(&wait_timer);
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
	for (i = 0; i < MAX_CTX; i++)
		if (ctxs[i].state != CTX_FREE && ctxs[i].jobs == 0)
			ctx_free(&ctxs[i]);
}
