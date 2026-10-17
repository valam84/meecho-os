/*
 * The render node: /dev/dri/renderD128 as Mesa's freedreno finds an msm
 * kernel (port/clover/hw-gpu.md, §8 and step 7).  The node itself -
 * contexts as cloned minors, VERSION, GET_CAP, GEM_CLOSE, PRIME, the
 * SYNCOBJ_* family, dma-bufs and sync files as descriptors, waits,
 * mmap(2) - is libdrmdriver's, as the CB2's mali has it; what is msm's is
 * here, the calls by the numbers of msm_drm.h (MIT, Mesa's copy):
 *
 *   GET_PARAM, SET_PARAM      the GPU as Linux's adreno_get_param() tells
 *                             it, from this machine's tree and registers
 *   GEM_NEW, GEM_INFO         a buffer: memory of libdrmdriver's, placed at
 *                             an address of the client's own (GET_IOVA);
 *                             its mmap(2) token, name, flags, metadata
 *   GEM_CPU_PREP, _CPU_FINI   a processor's access: until the GPU is done
 *   GEM_MADVISE               nothing is ever purged: "retained"
 *   GEM_SUBMIT                command buffers by buffer and offset, the
 *                             buffers they use; fences in and out by sync
 *                             file and sync object; a fence number of its
 *                             queue back
 *   WAIT_FENCE                until a queue's fence number is over
 *   SUBMITQUEUE_NEW, _CLOSE, _QUERY
 *                             queues of priority (one ring: in order all
 *                             the same), and their faults
 *
 * The version is 1.6 - what of Linux's list is here: softpin (1.4, no
 * relocations: Mesa writes the addresses itself), submit queues and their
 * FAULTS query (1.3, 1.5), sync objects in a submission (1.6).  Not the
 * suspend count (1.7), CACHED_COHERENT (1.8, an a6xx thing) or anything
 * after.  freedreno takes 1.x and picks its path by the minor
 * (freedreno_drmif.h).
 *
 * Each client has a GPU address space of its own - its own table in bank
 * 0, switched by the service between clients' jobs (adreno_gpu.c) - where
 * Linux on an A5xx has one for all.  A buffer given to another client
 * (PRIME) is placed in that client's space at an address of its own, as
 * on the Mali.  The buffers are the service's pages, cacheable, cleaned
 * around each job a processor's mapping could have touched (libdrmdriver);
 * MSM_BO_WC, which Mesa asks for, is taken as that.
 *
 * Errors are Linux's for the same request.  A job that faulted in its
 * space or hung ends with its fence failed - a sync file shows EIO, a
 * sync object's wait fails EIO (libdrmdriver's one addition) - and is
 * counted in MSM_PARAM_FAULTS and its queue's FAULTS; WAIT_FENCE stays
 * Linux's, 0 once the job is over, failed or not.
 */

#include "adreno_drm.h"
#include "adreno_regs.h"

#include <minix/sysutil.h>
#include <minix/syslib.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drm.h"
#include "msm_drm.h"

#define MSM_MINOR	6
#define NR_PRIORITIES	3		/* one ring, three levels (Linux) */
#define MAX_JOB_BOS	4096		/* buffers one submission may name */
#define MAX_QUEUED	256		/* jobs a client may have waiting */
#define MAX_QUEUES	64		/* queues a client may have open */
#define MAX_META	1024		/* MSM_INFO_SET_METADATA */
#define PAGE		4096u

/* The tokens GET_OFFSET gives: above anything a GPU address can be. */
#define MMAP_OFFSET_BASE	(1ull << 40)

static size_t limit_vm = 512u << 20;

/* A submit queue: its fence numbers, the jobs of it not over, its faults. */
struct queue {
	TAILQ_ENTRY(queue) link;	/* the context's */
	uint32_t id, prio;
	uint32_t seq;			/* the last fence number given */
	unsigned faults;
	unsigned refs;			/* the context's list, each job */
	struct qfence {
		uint32_t seq;
		struct drmdrv_fence *f;
	} *fences;			/* those given and not over */
	unsigned nfences, maxfences;
};

/* The context's half of the service's: its space, its queues. */
struct actx {
	struct ad_vm vm;
	TAILQ_HEAD(, queue) queues;
	uint32_t next_queue;
	char comm[32];			/* MSM_PARAM_COMM, for the log */
};

/* A job of a client. */
struct ajob {
	struct ad_job job;		/* first: done() gets this */
	struct queue *q;
};

static struct actx *
actx_of(struct drmdrv_ctx *c)
{
	return c->priv;
}

/* ---- queues ------------------------------------------------------------ */

static void
queue_unref(struct queue *q)
{
	unsigned i;

	if (--q->refs > 0)
		return;
	for (i = 0; i < q->nfences; i++)
		drmdrv_fence_unref(q->fences[i].f);
	free(q->fences);
	free(q);
}

static struct queue *
queue_get(struct actx *a, uint32_t id)
{
	struct queue *q;

	TAILQ_FOREACH(q, &a->queues, link)
		if (q->id == id)
			return q;
	return NULL;
}

static int
queue_new(struct actx *a, uint32_t prio, uint32_t *id)
{
	struct queue *q;
	unsigned n = 0;

	TAILQ_FOREACH(q, &a->queues, link)
		n++;
	if (n >= MAX_QUEUES)
		return ENOSPC;
	if ((q = calloc(1, sizeof(*q))) == NULL)
		return ENOMEM;
	q->id = a->next_queue++;
	q->prio = prio;
	q->refs = 1;
	TAILQ_INSERT_TAIL(&a->queues, q, link);
	*id = q->id;
	return OK;
}

/* Fence number seq of q is f: kept until it is over (WAIT_FENCE). */
static int
queue_fence(struct queue *q, uint32_t seq, struct drmdrv_fence *f)
{
	struct qfence *t;
	unsigned i, k;

	for (i = k = 0; i < q->nfences; i++) {
		if (drmdrv_fence_done(q->fences[i].f))
			drmdrv_fence_unref(q->fences[i].f);
		else
			q->fences[k++] = q->fences[i];
	}
	q->nfences = k;
	if (q->nfences == q->maxfences) {
		k = q->maxfences ? q->maxfences * 2 : 16;
		if ((t = realloc(q->fences, k * sizeof(*t))) == NULL)
			return ENOMEM;
		q->fences = t;
		q->maxfences = k;
	}
	q->fences[q->nfences].seq = seq;
	q->fences[q->nfences++].f = drmdrv_fence_ref(f);
	return OK;
}

/* ---- buffers ----------------------------------------------------------- */

/* The first hole of size bytes; the buffer it is before in *before. */
static int
va_alloc(struct ad_vm *vm, size_t size, uint64_t *va, struct ad_bo **before)
{
	struct ad_bo *bo;
	uint64_t prev = AD_VA_START;

	TAILQ_FOREACH(bo, &vm->bos, link) {
		if (bo->iova - prev >= size) {
			*va = prev;
			*before = bo;
			return OK;
		}
		prev = bo->iova + bo->size;
	}
	if (AD_VA_END - prev < size)
		return ENOSPC;
	*va = prev;
	*before = NULL;
	return OK;
}

/*
 * mem - a reference given - placed in vm: an address, the pages in the
 * table, read-only for the GPU if the buffer says so.
 */
static struct ad_bo *
bo_place(struct ad_vm *vm, struct drmdrv_mem *mem, uint32_t flags, int *err)
{
	struct ad_bo *bo, *before;
	uint64_t va;
	int r;

	if (!mem->foreign && vm->mem + mem->size > limit_vm) {
		*err = ENOMEM;
		return NULL;
	}
	if ((bo = calloc(1, sizeof(*bo))) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	if ((r = va_alloc(vm, mem->size, &va, &before)) != OK ||
	    (r = smmu_pt_map(&vm->pt, va, mem->pages, mem->size / PAGE,
	    AD_PT_READ | ((flags & MSM_BO_GPU_READONLY) ? 0 :
	    AD_PT_WRITE))) != OK) {
		free(bo);
		*err = r;
		return NULL;
	}
	bo->vm = vm;
	bo->mem = mem;
	bo->iova = va;
	bo->size = mem->size;
	bo->flags = flags;
	bo->refs = 1;
	bo->mmap_offset = MMAP_OFFSET_BASE + va;
	if (before != NULL)
		TAILQ_INSERT_BEFORE(before, bo, link);
	else
		TAILQ_INSERT_TAIL(&vm->bos, bo, link);
	vm->mem += mem->foreign ? 0 : mem->size;
	return bo;
}

/*
 * The last reference: out of the table first - and out of the SMMU's TLB
 * if the bank walks it now - then the memory may go.  No job holds it:
 * a job keeps a reference until it is over.
 */
static void
bo_unref(struct ad_bo *bo)
{
	if (--bo->refs > 0)
		return;
	(void)smmu_pt_unmap(&bo->vm->pt, bo->iova, bo->size / PAGE);
	TAILQ_REMOVE(&bo->vm->bos, bo, link);
	bo->vm->mem -= bo->mem->foreign ? 0 : bo->size;
	drmdrv_mem_unref(bo->mem);
	free(bo->meta);
	free(bo);
}

/* ---- libdrmdriver's questions ----------------------------------------- */

static int
ctx_open(struct drmdrv_ctx *c)
{
	struct actx *a;
	uint32_t id;
	int r;

	if ((a = calloc(1, sizeof(*a))) == NULL)
		return ENOMEM;
	TAILQ_INIT(&a->vm.bos);
	TAILQ_INIT(&a->queues);
	a->vm.name = c->name;
	if ((r = smmu_pt_new(&a->vm.pt)) != OK) {
		log_warn(&ad_log, "%s: no page table: %d\n", c->name, r);
		free(a);
		return ENOMEM;
	}
	/* Queue 0, the default one, at the middle priority: Linux's
	 * DIV_ROUND_UP(max_priority, 2), max_priority NR_PRIORITIES - 1. */
	if ((r = queue_new(a, NR_PRIORITIES / 2, &id)) != OK) {
		(void)smmu_pt_free(&a->vm.pt);
		free(a);
		return r;
	}
	c->priv = a;
	return OK;
}

/* Closed: its jobs not yet in the ring never get there. */
static void
ctx_cancel(struct drmdrv_ctx *c)
{
	gpu_cancel(&actx_of(c)->vm);
}

/* Gone, its last job over and its buffers out: its queues and table. */
static void
ctx_close(struct drmdrv_ctx *c)
{
	struct actx *a = actx_of(c);
	struct queue *q;

	while ((q = TAILQ_FIRST(&a->queues)) != NULL) {
		TAILQ_REMOVE(&a->queues, q, link);
		queue_unref(q);
	}
	gpu_vm_gone(&a->vm);
	(void)smmu_pt_free(&a->vm.pt);
	free(a);
	c->priv = NULL;
}

static struct drmdrv_mem *
lib_bo_mem(void *bo)
{
	return ((struct ad_bo *)bo)->mem;
}

static void
lib_bo_unref(void *bo)
{
	bo_unref(bo);
}

static uint64_t
lib_bo_mmap_offset(void *bo)
{
	return ((struct ad_bo *)bo)->mmap_offset;
}

/* Another client's memory in this one's space (PRIME). */
static void *
lib_bo_import(struct drmdrv_ctx *c, struct drmdrv_mem *mem, int *err)
{
	struct ad_bo *bo;

	drmdrv_mem_ref(mem);
	if ((bo = bo_place(&actx_of(c)->vm, mem, mem->flags, err)) == NULL)
		drmdrv_mem_unref(mem);
	return bo;
}

/* ---- parameters ------------------------------------------------------- */

/* GPU_ID from the chip id: 0x05010200 is 512 (adreno_gpu.h's ADRENO_REV). */
static uint32_t
gpu_id(void)
{
	uint32_t c = ad.dt.chip_id;

	return ((c >> 24) & 0xff) * 100 + ((c >> 16) & 0xff) * 10 +
	    ((c >> 8) & 0xff);
}

static int
do_get_param(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_param *p = arg;
	struct actx *a = actx_of(c->ctx);

	if (p->pipe != MSM_PIPE_3D0 || p->pad != 0 || p->len != 0)
		return EINVAL;
	switch (p->param) {
	case MSM_PARAM_GPU_ID:		p->value = gpu_id(); break;
	case MSM_PARAM_GMEM_SIZE:	p->value = A512_GMEM_SIZE; break;
	case MSM_PARAM_CHIP_ID:		p->value = ad.dt.chip_id; break;
	/* The core's clock as it runs: the crystal's, until the clocks are
	 * raised (hw-gpu.md, step 10). */
	case MSM_PARAM_MAX_FREQ:	p->value = 19200000; break;
	case MSM_PARAM_TIMESTAMP:
		if (gpu_dead())
			return EIO;	/* its registers are not to be read */
		p->value = cp_timestamp();
		break;
	case MSM_PARAM_GMEM_BASE:	p->value = A5XX_GMEM_BASE; break;
	case MSM_PARAM_PRIORITIES:	p->value = NR_PRIORITIES; break;
	case MSM_PARAM_PP_PGTABLE:	p->value = 0; break;	/* deprecated */
	case MSM_PARAM_FAULTS:
		p->value = gpu_global_faults + a->vm.faults;
		break;
	case MSM_PARAM_SUSPENDS:	p->value = 0; break;	/* never */
	case MSM_PARAM_VA_START:	p->value = AD_VA_START; break;
	case MSM_PARAM_VA_SIZE:
		p->value = AD_VA_END - AD_VA_START;
		break;
	case MSM_PARAM_HIGHEST_BANK_BIT:
		p->value = A512_HIGHEST_BANK_BIT;
		break;
	case MSM_PARAM_RAYTRACING:	p->value = 0; break;
	case MSM_PARAM_UCHE_TRAP_BASE:	p->value = A5XX_UCHE_TRAP; break;
	default:
		return EINVAL;
	}
	return OK;
}

/* What a client may say about itself: its name (for the log), whether it
 * profiles; no VM_BIND here. */
static int
do_set_param(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_param *p = arg;
	struct actx *a = actx_of(c->ctx);
	size_t n;

	if (p->pipe != MSM_PIPE_3D0 || p->pad != 0)
		return EINVAL;
	switch (p->param) {
	case MSM_PARAM_COMM:
	case MSM_PARAM_CMDLINE:
		if (p->len > 4096)
			return EINVAL;
		if (p->param == MSM_PARAM_CMDLINE)
			return OK;
		n = p->len < sizeof(a->comm) - 1 ? p->len : sizeof(a->comm) - 1;
		memset(a->comm, 0, sizeof(a->comm));
		return drmdrv_from_user(c, p->value, a->comm, n);
	case MSM_PARAM_SYSPROF:
		return p->len == 0 && p->value <= 2 ? OK : EINVAL;
	default:
		return EINVAL;
	}
}

/* ---- buffers ----------------------------------------------------------- */

static int
do_gem_new(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_new *g = arg;
	struct drmdrv_mem *mem;
	struct ad_bo *bo;
	size_t size;
	int r;

	if (g->flags & ~MSM_BO_FLAGS)
		return EINVAL;
	switch (g->flags & MSM_BO_CACHE_MASK) {
	case MSM_BO_CACHED:
	case MSM_BO_WC:
	case MSM_BO_UNCACHED:
		break;
	default:			/* CACHED_COHERENT: an a6xx thing */
		return EINVAL;
	}
	size = (size_t)((g->size + PAGE - 1) & ~(uint64_t)(PAGE - 1));
	if (size == 0 || g->size > AD_VA_END - AD_VA_START)
		return EINVAL;
	if ((mem = drmdrv_mem_new(size, g->flags, &r)) == NULL)
		return r;
	if ((bo = bo_place(&actx_of(c->ctx)->vm, mem, g->flags, &r)) == NULL) {
		drmdrv_mem_unref(mem);
		return r;
	}
	if ((r = drmdrv_bo_handle(c->ctx, bo, &g->handle)) != OK) {
		bo_unref(bo);
		return r;
	}
	return OK;
}

static int
do_gem_info(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_info *g = arg;
	struct ad_bo *bo;
	size_t n;
	void *m;
	int r;

	if (g->pad != 0)
		return EINVAL;
	switch (g->info) {
	case MSM_INFO_GET_OFFSET:
	case MSM_INFO_GET_IOVA:
	case MSM_INFO_SET_IOVA:
	case MSM_INFO_GET_FLAGS:
		/* A value, not a pointer: no length. */
		if (g->len != 0)
			return EINVAL;
		break;
	case MSM_INFO_SET_NAME:
	case MSM_INFO_GET_NAME:
	case MSM_INFO_SET_METADATA:
	case MSM_INFO_GET_METADATA:
		break;
	default:
		return EINVAL;
	}
	if ((bo = drmdrv_bo_get(c->ctx, g->handle)) == NULL)
		return ENOENT;
	switch (g->info) {
	case MSM_INFO_GET_OFFSET:
		g->value = bo->mmap_offset;
		return OK;
	case MSM_INFO_GET_IOVA:
		g->value = bo->iova;
		return OK;
	case MSM_INFO_SET_IOVA:
		return EINVAL;		/* no VM_BIND: the service places */
	case MSM_INFO_GET_FLAGS:
		g->value = bo->flags;
		return OK;
	case MSM_INFO_SET_NAME:
		/* Room for the terminating zero (Linux's check). */
		if (g->len >= sizeof(bo->name))
			return EINVAL;
		memset(bo->name, 0, sizeof(bo->name));
		return drmdrv_from_user(c, g->value, bo->name, g->len);
	case MSM_INFO_GET_NAME:
		n = strlen(bo->name);
		if (g->value != 0 && g->len < n)
			return EINVAL;
		g->len = (uint32_t)n;
		return g->value != 0 ? drmdrv_to_user(c, bo->name, g->value,
		    n) : OK;
	case MSM_INFO_SET_METADATA:
		if (g->len > MAX_META)
			return EINVAL;
		if ((m = malloc(g->len > 0 ? g->len : 1)) == NULL)
			return ENOMEM;
		if ((r = drmdrv_from_user(c, g->value, m, g->len)) != OK) {
			free(m);
			return r;
		}
		free(bo->meta);
		bo->meta = m;
		bo->metalen = g->len;
		return OK;
	case MSM_INFO_GET_METADATA:
		if (g->value == 0) {
			g->len = (uint32_t)bo->metalen;
			return OK;
		}
		if (g->len < bo->metalen)
			return EINVAL;
		g->len = (uint32_t)bo->metalen;
		return drmdrv_to_user(c, bo->meta, g->value, bo->metalen);
	}
	return EINVAL;
}

/* An absolute CLOCK_MONOTONIC time in ns; "forever" stays forever. */
static int64_t
deadline_of(const struct drm_msm_timespec *t)
{
	if (t->tv_sec < 0)
		return 0;
	if (t->tv_sec >= INT64_MAX / 1000000000LL - 1)
		return INT64_MAX;
	return t->tv_sec * 1000000000LL + t->tv_nsec;
}

/*
 * A processor's access: until the GPU is done with the buffer - every job
 * of it, read or write - or EBUSY at once with NOSYNC, ETIMEDOUT past the
 * deadline (msm_gem_cpu_prep()).  The caches need nothing more: a buffer a
 * processor maps is cleaned around every job (libdrmdriver).
 */
static int
do_cpu_prep(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_cpu_prep *p = arg;
	struct ad_bo *bo;

	if (p->op & ~MSM_PREP_FLAGS)
		return EINVAL;
	if ((bo = drmdrv_bo_get(c->ctx, p->handle)) == NULL)
		return ENOENT;
	return drmdrv_wait_fence(c, bo->mem->fence, (p->op & MSM_PREP_NOSYNC) ?
	    0 : deadline_of(&p->timeout), EBUSY);
}

static int
do_cpu_fini(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_cpu_fini *f = arg;

	return drmdrv_bo_get(c->ctx, f->handle) != NULL ? OK : ENOENT;
}

static int
do_madvise(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_madvise *m = arg;

	if (m->madv != MSM_MADV_WILLNEED && m->madv != MSM_MADV_DONTNEED)
		return EINVAL;
	if (drmdrv_bo_get(c->ctx, m->handle) == NULL)
		return ENOENT;
	m->retained = 1;	/* nothing is purged here */
	return OK;
}

/* ---- queues ------------------------------------------------------------ */

static int
do_queue_new(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_submitqueue *s = arg;

	if (s->flags & ~MSM_SUBMITQUEUE_FLAGS)
		return EINVAL;
	/* No preemption (one ring) and no VM_BIND. */
	if (s->flags != 0 || s->prio >= NR_PRIORITIES)
		return EINVAL;
	return queue_new(actx_of(c->ctx), s->prio, &s->id);
}

/* Its jobs not over keep it until they are. */
static int
do_queue_close(struct drmdrv_call *c, void *arg)
{
	uint32_t id = *(uint32_t *)arg;
	struct actx *a = actx_of(c->ctx);
	struct queue *q;

	if (id == 0 || (q = queue_get(a, id)) == NULL)
		return ENOENT;		/* the default one is not the user's */
	TAILQ_REMOVE(&a->queues, q, link);
	queue_unref(q);
	return OK;
}

/* FAULTS: the queue's, a 32-bit count, as much of it as the caller has
 * room for (msm_submitqueue_query_faults()). */
static int
do_queue_query(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_submitqueue_query *s = arg;
	struct queue *q;
	uint32_t v;

	if (s->pad != 0)
		return EINVAL;
	if ((q = queue_get(actx_of(c->ctx), s->id)) == NULL)
		return ENOENT;
	if (s->param != MSM_SUBMITQUEUE_PARAM_FAULTS)
		return EINVAL;
	v = q->faults;
	if (s->len == 0) {
		s->len = sizeof(v);
		return OK;
	}
	if (s->len > sizeof(v))
		s->len = sizeof(v);
	return drmdrv_to_user(c, &v, s->data, s->len);
}

/* ---- jobs -------------------------------------------------------------- */

#define AJOB(dj)	((struct ajob *)((char *)(dj) - \
	offsetof(struct ajob, job) - offsetof(struct ad_job, dj)))

static void
ajob_free(struct ajob *a)
{
	unsigned i;

	for (i = 0; i < a->job.nbos; i++)
		bo_unref(a->job.bos[i]);
	free(a->job.bos);
	free(a->job.cmds);
	if (a->q != NULL)
		queue_unref(a->q);
	free(a);
}

/* Over: the buffers go, then the fence, then perhaps the context. */
static void
job_done(struct ad_job *j)
{
	struct ajob *a = (struct ajob *)j;
	struct drmdrv_ctx *c = j->dj.ctx;
	int result = j->result;
	unsigned i;

	if (result != OK && result != ECANCELED) {
		if (c->failed < 4 || c->failed % 100 == 0)
			log_warn(&ad_log, "%s%s%s: job %u failed: %d%s\n",
			    c->name, actx_of(c)->comm[0] ? " " : "",
			    actx_of(c)->comm, j->seqno, result,
			    j->fault_fsr ? " (a fault in its space)" : "");
		c->failed++;
		a->q->faults++;
	}
	/* The buffers and the queue before the fence: the context may go
	 * with this job, and its space with it. */
	for (i = 0; i < j->nbos; i++)
		bo_unref(j->bos[i]);
	j->nbos = 0;
	queue_unref(a->q);
	a->q = NULL;
	drmdrv_job_done(&j->dj, result == OK ? 0 : result);
	ajob_free(a);
}

static void
ajob_run(struct drmdrv_job *dj)
{
	gpu_submit(&AJOB(dj)->job);
}

/* Its context went before its fences were over: it never runs. */
static void
ajob_cancel(struct drmdrv_job *dj)
{
	struct ajob *a = AJOB(dj);

	a->job.result = ECANCELED;
	job_done(&a->job);
}

/* The sync objects a submission names, each read and checked. */
static int
syncobjs_read(struct drmdrv_call *c, uint64_t ptr, uint32_t n,
	uint32_t stride, struct drm_msm_syncobj *out)
{
	unsigned i;
	int r;

	if (n == 0)
		return OK;
	if (n > DRMDRV_MAX_SYNCS || stride < 2 * sizeof(uint32_t) ||
	    stride > 4096)
		return EINVAL;
	for (i = 0; i < n; i++) {
		memset(&out[i], 0, sizeof(out[i]));
		if ((r = drmdrv_from_user(c, ptr + (uint64_t)i * stride,
		    &out[i], stride < sizeof(out[i]) ? stride :
		    sizeof(out[i]))) != OK)
			return r;
		if (out[i].point != 0)
			return EOPNOTSUPP;	/* no timeline sync objects */
		if (out[i].flags & ~MSM_SYNCOBJ_FLAGS)
			return EINVAL;
		if (drmdrv_syncobj_get(c->ctx, out[i].handle) == NULL)
			return EINVAL;
	}
	return OK;
}

/*
 * GEM_SUBMIT (msm_ioctl_gem_submit()): everything checked first - queue,
 * flags, buffers, command buffers, the fences it waits for - then nothing
 * can fail any more but the sync file out, which is made last before the
 * job is given; then libdrmdriver holds the job until what it waits for is
 * over, and adreno_gpu.c puts it into the ring.
 */
static int
do_submit(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_gem_submit *s = arg;
	struct actx *a = actx_of(c->ctx);
	struct drm_msm_syncobj sin[DRMDRV_MAX_SYNCS], sout[DRMDRV_MAX_SYNCS];
	struct drm_msm_gem_submit_bo sb;
	struct drm_msm_gem_submit_cmd sc;
	struct drmdrv_fence *fin = NULL;
	struct drmdrv_syncobj *so;
	struct queue *q;
	struct ajob *aj;
	struct ad_job *j;
	struct ad_bo *bo;
	uint8_t *noimpl = NULL;		/* MSM_SUBMIT_BO_NO_IMPLICIT, each */
	unsigned i, k;
	int r;

	if (s->pad != 0 || MSM_PIPE_ID(s->flags) != MSM_PIPE_3D0 ||
	    (MSM_PIPE_FLAGS(s->flags) & ~MSM_SUBMIT_FLAGS))
		return EINVAL;
	/* Not here: commands from the ring itself, a fence number chosen by
	 * the caller (1.9). */
	if (s->flags & (MSM_SUBMIT_SUDO | MSM_SUBMIT_FENCE_SN_IN))
		return EINVAL;
	if ((q = queue_get(a, s->queueid)) == NULL)
		return ENOENT;
	if (s->nr_bos > MAX_JOB_BOS || s->nr_cmds == 0 ||
	    s->nr_cmds > gpu_max_cmds())
		return EINVAL;
	if (c->ctx->jobs >= MAX_QUEUED)
		return EAGAIN;
	if (!(s->flags & MSM_SUBMIT_SYNCOBJ_IN))
		s->nr_in_syncobjs = 0;
	if (!(s->flags & MSM_SUBMIT_SYNCOBJ_OUT))
		s->nr_out_syncobjs = 0;
	if ((r = syncobjs_read(c, s->in_syncobjs, s->nr_in_syncobjs,
	    s->syncobj_stride, sin)) != OK ||
	    (r = syncobjs_read(c, s->out_syncobjs, s->nr_out_syncobjs,
	    s->syncobj_stride, sout)) != OK)
		return r;
	for (i = 0; i < s->nr_in_syncobjs; i++)
		if (drmdrv_syncobj_get(c->ctx, sin[i].handle)->fence == NULL)
			return EINVAL;

	if ((aj = calloc(1, sizeof(*aj))) == NULL)
		return ENOMEM;
	j = &aj->job;
	if ((s->nr_bos > 0 && ((j->bos = calloc(s->nr_bos,
	    sizeof(*j->bos))) == NULL || (noimpl = calloc(s->nr_bos,
	    sizeof(*noimpl))) == NULL)) ||
	    (j->cmds = calloc(s->nr_cmds, sizeof(*j->cmds))) == NULL) {
		free(noimpl);
		ajob_free(aj);
		return ENOMEM;
	}

	/* The buffers: each once, each this client's. */
	for (i = 0; i < s->nr_bos; i++) {
		if ((r = drmdrv_from_user(c, s->bos + (uint64_t)i * sizeof(sb),
		    &sb, sizeof(sb))) != OK)
			goto fail;
		r = EINVAL;
		if ((sb.flags & ~MSM_SUBMIT_BO_FLAGS) ||
		    (bo = drmdrv_bo_get(c->ctx, sb.handle)) == NULL)
			goto fail;
		for (k = 0; k < j->nbos; k++)
			if (j->bos[k] == bo)
				goto fail;	/* twice in one list */
		bo->refs++;
		noimpl[j->nbos] = (sb.flags & MSM_SUBMIT_BO_NO_IMPLICIT) != 0;
		j->bos[j->nbos++] = bo;
	}

	/* The command buffers: by index into that list, whole dwords,
	 * inside their buffer; no relocations (softpin, 1.4). */
	for (i = 0; i < s->nr_cmds; i++) {
		if ((r = drmdrv_from_user(c, s->cmds +
		    (uint64_t)i * sizeof(sc), &sc, sizeof(sc))) != OK)
			goto fail;
		r = EINVAL;
		if ((sc.type != MSM_SUBMIT_CMD_BUF &&
		    sc.type != MSM_SUBMIT_CMD_IB_TARGET_BUF &&
		    sc.type != MSM_SUBMIT_CMD_CTX_RESTORE_BUF) ||
		    sc.nr_relocs != 0 || sc.submit_idx >= j->nbos ||
		    sc.size == 0 || (sc.size & 3) || (sc.submit_offset & 3) ||
		    (uint64_t)sc.submit_offset + sc.size >
		    j->bos[sc.submit_idx]->size || sc.size / 4 >= (1u << 20))
			goto fail;
		j->cmds[i].iova = j->bos[sc.submit_idx]->iova + sc.submit_offset;
		j->cmds[i].dwords = sc.size / 4;
		j->cmds[i].type = sc.type == MSM_SUBMIT_CMD_BUF ? AD_CMD_BUF :
		    sc.type == MSM_SUBMIT_CMD_IB_TARGET_BUF ? AD_CMD_TARGET :
		    AD_CMD_RESTORE;
		j->ncmds++;
	}

	/* A sync file to wait for: none, or not a sync file, is EINVAL. */
	if ((s->flags & MSM_SUBMIT_FENCE_FD_IN) &&
	    drmdrv_syncfile_import(c, s->fence_fd, &fin) != OK) {
		r = EINVAL;
		goto fail;
	}
	if ((r = drmdrv_job_init(&j->dj, c->ctx, s->nr_in_syncobjs + 1 +
	    j->nbos)) != OK || (r = queue_fence(q, q->seq + 1,
	    j->dj.out)) != OK)
		goto fail;
	/* The last thing that can fail: the out-fence as a descriptor. */
	if ((s->flags & MSM_SUBMIT_FENCE_FD_OUT) &&
	    (r = drmdrv_syncfile_export(c, j->dj.out, &s->fence_fd)) != OK) {
		q->nfences--;
		drmdrv_fence_unref(q->fences[q->nfences].f);
		goto fail;
	}

	/* Committed.  What it waits for: the sync file, the sync objects
	 * (reset after, if asked), and - unless told not to - the last job
	 * of another client on each buffer; then it is the last on each. */
	s->fence = ++q->seq;
	(void)drmdrv_job_wait_for(&j->dj, fin);
	drmdrv_fence_unref(fin);
	for (i = 0; i < s->nr_in_syncobjs; i++) {
		so = drmdrv_syncobj_get(c->ctx, sin[i].handle);
		(void)drmdrv_job_wait_for(&j->dj, so->fence);
		if (sin[i].flags & MSM_SYNCOBJ_RESET)
			drmdrv_syncobj_set(so, NULL);
	}
	for (i = 0; i < j->nbos; i++)
		if ((s->flags & MSM_SUBMIT_NO_IMPLICIT) || noimpl[i])
			(void)drmdrv_job_touches(&j->dj, j->bos[i]->mem);
		else
			drmdrv_job_uses(&j->dj, j->bos[i]->mem);
	free(noimpl);
	for (i = 0; i < s->nr_out_syncobjs; i++)
		drmdrv_syncobj_set(drmdrv_syncobj_get(c->ctx, sout[i].handle),
		    j->dj.out);

	j->vm = &a->vm;
	j->done = job_done;
	aj->q = q;
	q->refs++;
	j->dj.run = ajob_run;
	j->dj.cancel = ajob_cancel;
	drmdrv_job_submit(&j->dj);
	return OK;

fail:
	if (j->dj.out != NULL)
		drmdrv_job_fini(&j->dj);
	drmdrv_fence_unref(fin);
	free(noimpl);
	ajob_free(aj);
	return r;
}

/*
 * WAIT_FENCE (msm_wait_fence()): a number past the queue's last is EINVAL;
 * one no longer kept is over already; else until it is, ETIMEDOUT past
 * the deadline.
 */
static int
do_wait_fence(struct drmdrv_call *c, void *arg)
{
	struct drm_msm_wait_fence *w = arg;
	struct queue *q;
	unsigned i;

	if (w->flags & ~MSM_WAIT_FENCE_FLAGS)
		return EINVAL;
	if ((q = queue_get(actx_of(c->ctx), w->queueid)) == NULL)
		return ENOENT;
	if ((int32_t)(w->fence - q->seq) > 0)
		return EINVAL;
	for (i = 0; i < q->nfences; i++)
		if (q->fences[i].seq == w->fence)
			return drmdrv_wait_fence(c, q->fences[i].f,
			    deadline_of(&w->timeout), ETIMEDOUT);
	return OK;
}

/* ---- the device ------------------------------------------------------- */

static const struct drmdrv_calldef msm_calls[] = {
	{ DRM_MSM_GET_PARAM, sizeof(struct drm_msm_param), do_get_param },
	{ DRM_MSM_SET_PARAM, sizeof(struct drm_msm_param), do_set_param },
	{ DRM_MSM_GEM_NEW, sizeof(struct drm_msm_gem_new), do_gem_new },
	{ DRM_MSM_GEM_INFO, sizeof(struct drm_msm_gem_info), do_gem_info },
	{ DRM_MSM_GEM_CPU_PREP, sizeof(struct drm_msm_gem_cpu_prep),
	    do_cpu_prep },
	{ DRM_MSM_GEM_CPU_FINI, sizeof(struct drm_msm_gem_cpu_fini),
	    do_cpu_fini },
	{ DRM_MSM_GEM_SUBMIT, sizeof(struct drm_msm_gem_submit), do_submit },
	{ DRM_MSM_WAIT_FENCE, sizeof(struct drm_msm_wait_fence),
	    do_wait_fence },
	{ DRM_MSM_GEM_MADVISE, sizeof(struct drm_msm_gem_madvise),
	    do_madvise },
	{ DRM_MSM_SUBMITQUEUE_NEW, sizeof(struct drm_msm_submitqueue),
	    do_queue_new },
	{ DRM_MSM_SUBMITQUEUE_CLOSE, sizeof(uint32_t), do_queue_close },
	{ DRM_MSM_SUBMITQUEUE_QUERY, sizeof(struct drm_msm_submitqueue_query),
	    do_queue_query },
	{ 0, 0, NULL }
};

static const struct drmdrv msm = {
	.name = "msm",
	.date = "20130625",
	.desc = "MSM Snapdragon DRM",
	.major = 1,
	.minor = MSM_MINOR,
	.patchlevel = 0,
	.fence_driver = "msm",
	.fence_obj = "gpu-ring-0",
	.log = &ad_log,
	.calls = msm_calls,
	.ctx_open = ctx_open,
	.ctx_cancel = ctx_cancel,
	.ctx_close = ctx_close,
	.bo_mem = lib_bo_mem,
	.bo_unref = lib_bo_unref,
	.bo_mmap_offset = lib_bo_mmap_offset,
	.bo_import = lib_bo_import,
};

struct chardriver adreno_chardriver = {
	.cdr_open	= drmdrv_open,
	.cdr_close	= drmdrv_close,
	.cdr_ioctl	= drmdrv_ioctl,
	.cdr_cancel	= drmdrv_cancel,
	.cdr_select	= drmdrv_select,
};

/* Buffers up to total bytes in all, per_client each. */
void
adreno_drm_limits(size_t total, size_t per_client)
{
	drmdrv_mem_limit(total);
	limit_vm = per_client;
}

void
adreno_drm_init(void)
{
	drmdrv_init(&msm);
	log_info(&ad_log, "/dev/dri/renderD128 is served (msm 1.%d): A%u, chip "
	    "0x%08x, gmem 0x%x at 0x%x, buffers from 0x%llx\n", MSM_MINOR,
	    gpu_id(), ad.dt.chip_id, A512_GMEM_SIZE, A5XX_GMEM_BASE,
	    (unsigned long long)AD_VA_START);
}

void
adreno_drm_shutdown(void)
{
	drmdrv_shutdown();
}
