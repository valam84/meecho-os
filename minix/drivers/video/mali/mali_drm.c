/*
 * The device: /dev/dri/renderD128, the interface Mesa's Panfrost driver
 * talks to (panfrost_drm.h).  Stage 11.6.
 *
 * The render node itself - contexts as cloned minors, the calls every DRM
 * device answers (VERSION, GET_CAP, GEM_CLOSE, PRIME, SYNCOBJ_*), sync
 * objects, dma-bufs and sync files as descriptors, waits, mmap(2) - is
 * libdrmdriver's (minix/drmdriver.h), shared with the adreno service.
 * What is Panfrost's is here:
 *
 *   PANFROST_GET_PARAM            the GPU's identity and features
 *   PANFROST_CREATE_BO            a buffer, placed in the address space
 *   PANFROST_MMAP_BO              the token mmap(2) of the node is given
 *   PANFROST_GET_BO_OFFSET        where a buffer is on the GPU
 *   PANFROST_SUBMIT               a job chain, the buffers it uses,
 *                                 sync objects to wait for and to signal
 *   PANFROST_WAIT_BO              until no job uses a buffer
 *   PANFROST_MADVISE              nothing is ever purged: "retained"
 *   MEECHO_SCANOUT_IMPORT         fb's scanout buffers (meecho_drm.h)
 *
 * and each context's half: its own GPU address space (mali_mmu.c), its
 * jobs on the slots (mali_sched.c).
 *
 * Errors are Linux's for the same request, with libdrmdriver's one
 * addition: a job that failed - its chain broke, its address space
 * faulted - makes the wait on its sync object fail with EIO.  The job's
 * slot status and fault go to the log.
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/drmdriver.h>
#include <minix/ds.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"
#include "drm.h"
#include "panfrost_drm.h"
#include "meecho_drm.h"

#define MAX_JOB_BOS	4096		/* buffers one job may name */
#define MAX_QUEUED	256		/* jobs a context may have waiting */

/* A job of a client: the GPU's job, libdrmdriver's, and what it holds. */
struct pjob {
	struct mali_job job;		/* first: done() gets this */
	struct drmdrv_job dj;
	struct mali_bo *bos[];
};

static struct mali_vm *
vm_of(struct drmdrv_ctx *c)
{
	return c->priv;
}

/* ---- the context's half -------------------------------------------- */

static int
ctx_open(struct drmdrv_ctx *c)
{
	struct mali_vm *vm;

	if ((vm = calloc(1, sizeof(*vm))) == NULL)
		return ENOMEM;
	if (mali_vm_init(vm, c->name) != OK) {
		free(vm);
		return ENOMEM;
	}
	c->priv = vm;
	return OK;
}

/* Closed: its jobs not yet on a slot never get there. */
static void
ctx_cancel(struct drmdrv_ctx *c)
{
	mali_sched_cancel(vm_of(c));
}

/* Gone, its last job over: its address space too. */
static void
ctx_close(struct drmdrv_ctx *c)
{
	mali_vm_fini(vm_of(c));
	free(c->priv);
	c->priv = NULL;
}

static struct drmdrv_mem *
bo_mem(void *bo)
{
	return ((struct mali_bo *)bo)->mem;
}

static void
bo_unref(void *bo)
{
	mali_bo_unref(bo);
}

/* A heap's pages are not for the processor (as under Linux). */
static uint64_t
bo_mmap_offset(void *p)
{
	struct mali_bo *bo = p;

	return (bo->flags & PANFROST_BO_HEAP) ? 0 : bo->mmap_offset;
}

static void *
bo_import(struct drmdrv_ctx *c, struct drmdrv_mem *mem, int *err)
{
	return mali_bo_import(vm_of(c), mem, err);
}

/* ---- the calls -------------------------------------------------------- */

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
	uint64_t l2, n;

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
		/*
		 * Counted by hand: without SIMD (services are built
		 * -mgeneral-regs-only) GCC turns __builtin_popcountll into
		 * a call to libgcc, which services do not link.
		 */
		l2 = rd64(GPU_L2_PRESENT_LO);
		for (n = 0; l2 != 0; l2 &= l2 - 1)
			n++;
		return n;
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
do_get_param(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_get_param *p = arg;
	int ok;

	if (p->pad != 0)
		return EINVAL;
	p->value = mali_param(p->param, &ok);
	return ok ? OK : EINVAL;
}

static int
do_create_bo(struct drmdrv_call *c, void *arg)
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
	if ((bo = mali_bo_new(vm_of(c->ctx), a->size, a->flags, &r)) == NULL)
		return r;
	if ((r = drmdrv_bo_handle(c->ctx, bo, &a->handle)) != OK) {
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
do_scanout_import(struct drmdrv_call *c, void *arg)
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
	if ((r = drmdrv_umap_remote(c->user, (vir_bytes)a->addr,
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
	if ((bo = mali_bo_new_phys(vm_of(c->ctx), phys, fb, (vir_bytes)vir,
	    (size_t)a->size, &r)) == NULL)
		return r;
	if ((r = drmdrv_bo_handle(c->ctx, bo, &a->handle)) != OK) {
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
do_mmap_bo(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_mmap_bo *a = arg;
	struct mali_bo *bo;

	if (a->flags != 0)
		return EINVAL;
	if ((bo = drmdrv_bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	/* A heap's pages are not for the processor (as under Linux). */
	if (bo->flags & PANFROST_BO_HEAP)
		return EINVAL;
	a->offset = bo->mmap_offset;
	return OK;
}

static int
do_get_bo_offset(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_get_bo_offset *a = arg;
	struct mali_bo *bo;

	if ((bo = drmdrv_bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	a->offset = bo->va;
	return OK;
}

static int
do_madvise(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_madvise *a = arg;

	if (drmdrv_bo_get(c->ctx, a->handle) == NULL)
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

#define PJOB(dj)	((struct pjob *)((char *)(dj) - offsetof(struct pjob, dj)))

/* A job of a client is over. */
static void
pjob_done(struct mali_job *j)
{
	struct pjob *p = (struct pjob *)j;
	struct drmdrv_ctx *c = p->dj.ctx;
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
	 * a sync object that holds it still fails EIO (libdrmdriver).  The
	 * buffers go first: the context may go with this job, and its
	 * address space with it.
	 */
	for (i = 0; i < j->nbos; i++)
		mali_bo_unref(p->bos[i]);
	drmdrv_job_done(&p->dj, j->result == OK ? 0 : j->result == EIO ? EINVAL :
	    j->result);
	free(p);
}

/* Every fence it waits for is over: to the slots. */
static void
pjob_run(struct drmdrv_job *dj)
{
	mali_job_submit(&PJOB(dj)->job);
}

/* Its context went first: it never runs, and fails. */
static void
pjob_cancel(struct drmdrv_job *dj)
{
	struct pjob *p = PJOB(dj);

	p->job.result = ECANCELED;
	pjob_done(&p->job);
}

static int
do_submit(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_submit *a = arg;
	uint32_t handles[DRMDRV_MAX_SYNCS], *bh = NULL;
	struct drmdrv_syncobj *out = NULL, *so;
	struct pjob *p;
	unsigned i, max;
	int r;

	if (a->jc == 0)
		return EINVAL;
	if (a->requirements & ~PANFROST_JD_REQ_FS)
		return EINVAL;
	if (a->jm_ctx_handle != 0 || a->pad != 0)
		return EINVAL;		/* no contexts of priority here */
	if (a->bo_handle_count > MAX_JOB_BOS ||
	    a->in_sync_count > DRMDRV_MAX_SYNCS)
		return EINVAL;
	if (c->ctx->jobs >= MAX_QUEUED)
		return EAGAIN;

	/* What it waits for: a sync object without a fence is an error. */
	if ((r = drmdrv_from_user(c, a->in_syncs, handles,
	    a->in_sync_count * sizeof(handles[0]))) != OK)
		return r;
	for (i = 0; i < a->in_sync_count; i++) {
		if ((so = drmdrv_syncobj_get(c->ctx, handles[i])) == NULL)
			return ENOENT;
		if (so->fence == NULL)
			return EINVAL;
	}
	if (a->out_sync != 0 &&
	    (out = drmdrv_syncobj_get(c->ctx, a->out_sync)) == NULL)
		return ENODEV;

	if ((p = calloc(1, sizeof(*p) + a->bo_handle_count *
	    sizeof(p->bos[0]))) == NULL)
		return ENOMEM;
	max = a->in_sync_count + a->bo_handle_count;
	if (drmdrv_job_init(&p->dj, c->ctx, max) != OK ||
	    (a->bo_handle_count > 0 && (bh = malloc(a->bo_handle_count *
	    sizeof(*bh))) == NULL)) {
		drmdrv_job_fini(&p->dj);
		free(p);
		return ENOMEM;
	}
	if ((r = drmdrv_from_user(c, a->bo_handles, bh, a->bo_handle_count *
	    sizeof(*bh))) != OK) {
		free(bh);
		drmdrv_job_fini(&p->dj);
		free(p);
		return r;
	}
	for (i = 0; i < a->bo_handle_count; i++) {
		if ((p->bos[i] = drmdrv_bo_get(c->ctx, bh[i])) == NULL) {
			while (i-- > 0)
				mali_bo_unref(p->bos[i]);
			free(bh);
			drmdrv_job_fini(&p->dj);
			free(p);
			return ENOENT;
		}
		mali_bo_ref(p->bos[i]);
	}
	free(bh);

	/*
	 * Others' fences it waits for: the sync objects it names, and - the
	 * implicit order of shared buffers - the last job of another context
	 * on each of its buffers.  Then it is the last job on each.
	 */
	for (i = 0; i < a->in_sync_count; i++)
		(void)drmdrv_job_wait_for(&p->dj,
		    drmdrv_syncobj_get(c->ctx, handles[i])->fence);
	for (i = 0; i < a->bo_handle_count; i++)
		drmdrv_job_uses(&p->dj, p->bos[i]->mem);
	if (out != NULL)
		drmdrv_syncobj_set(out, p->dj.out);

	p->job.vm = vm_of(c->ctx);
	p->job.jc = a->jc;
	p->job.req = a->requirements;
	p->job.slot = (a->requirements & PANFROST_JD_REQ_FS) ? 0 : 1;
	p->job.bos = p->bos;
	p->job.nbos = a->bo_handle_count;
	p->job.done = pjob_done;
	p->job.owner = c->ctx;
	p->dj.run = pjob_run;
	p->dj.cancel = pjob_cancel;

	/* Behind a held job of its own, or held itself, or to the slots. */
	drmdrv_job_submit(&p->dj);
	return OK;
}

/* Until the buffer's memory is idle: its last job over. */
static int
do_wait_bo(struct drmdrv_call *c, void *arg)
{
	struct drm_panfrost_wait_bo *a = arg;
	struct mali_bo *bo;

	if (a->pad != 0)
		return EINVAL;
	if ((bo = drmdrv_bo_get(c->ctx, a->handle)) == NULL)
		return ENOENT;
	/* A deadline already past is "not now", EBUSY; one reached is
	 * ETIMEDOUT. */
	return drmdrv_wait_fence(c, bo->mem->fence, a->timeout_ns, EBUSY);
}

/* ---- the device ------------------------------------------------------- */

static const struct drmdrv_calldef panfrost_calls[] = {
	{ DRM_PANFROST_SUBMIT,
	    sizeof(struct drm_panfrost_submit),		do_submit },
	{ DRM_PANFROST_WAIT_BO,
	    sizeof(struct drm_panfrost_wait_bo),	do_wait_bo },
	{ DRM_PANFROST_CREATE_BO,
	    sizeof(struct drm_panfrost_create_bo),	do_create_bo },
	{ DRM_PANFROST_MMAP_BO,
	    sizeof(struct drm_panfrost_mmap_bo),	do_mmap_bo },
	{ DRM_PANFROST_GET_PARAM,
	    sizeof(struct drm_panfrost_get_param),	do_get_param },
	{ DRM_PANFROST_GET_BO_OFFSET,
	    sizeof(struct drm_panfrost_get_bo_offset),	do_get_bo_offset },
	{ DRM_PANFROST_MADVISE,
	    sizeof(struct drm_panfrost_madvise),	do_madvise },
	{ DRM_MEECHO_SCANOUT_IMPORT,
	    sizeof(struct drm_meecho_scanout_import),	do_scanout_import },
	{ 0, 0, NULL }
};

/* What the board's panfrost says (port/cb2-gpu/ref-api.txt). */
static const struct drmdrv panfrost = {
	.name = "panfrost",
	.date = "20180908",
	.desc = "panfrost DRM",
	.major = 1,
	.minor = 2,
	.patchlevel = 0,
	.fence_driver = "mali",
	.fence_obj = "mali-job",
	.log = &mali_log,
	.calls = panfrost_calls,
	.ctx_open = ctx_open,
	.ctx_cancel = ctx_cancel,
	.ctx_close = ctx_close,
	.bo_mem = bo_mem,
	.bo_unref = bo_unref,
	.bo_mmap_offset = bo_mmap_offset,
	.bo_import = bo_import,
};

struct chardriver mali_chardriver = {
	.cdr_open	= drmdrv_open,
	.cdr_close	= drmdrv_close,
	.cdr_ioctl	= drmdrv_ioctl,
	.cdr_cancel	= drmdrv_cancel,
	.cdr_select	= drmdrv_select,
};

void
mali_drm_init(void)
{
	drmdrv_init(&panfrost);
}

/* The service is going: every wait answered, every context freed. */
void
mali_drm_shutdown(void)
{
	drmdrv_shutdown();
}
