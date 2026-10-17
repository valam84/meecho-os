/*
 * Fences, sync objects, descriptors, waits and jobs (minix/drmdriver.h).
 *
 * Descriptors.  A dma-buf, a sync file and a sync object given away are
 * minors of the device too, from OBJ_MINOR on: VFS makes the process a
 * descriptor of the minor (cdevfd(2)), and names the minor of a descriptor
 * the process gives back.  So they are ordinary files - fork, dup, exec,
 * SCM_RIGHTS over a socket - and what one may do with them is what Linux
 * lets one do: mmap(2) of a dma-buf and lseek(2) to its end (its size,
 * which is how Mesa learns it), DMA_BUF_IOCTL_SYNC and _EXPORT/
 * _IMPORT_SYNC_FILE on it; poll(2) of a sync file, SYNC_IOC_MERGE and
 * _FILE_INFO.  Holding the descriptor is the right: a context imports what
 * a descriptor of the process that asks names, never a number from the
 * request.
 *
 * Fences and order.  Jobs of one context run in the order given.  Jobs of
 * different contexts that share a buffer run in the order given too, as
 * implicit fences order them under Linux: every job leaves its fence on
 * each buffer it names, and the next job of another context to name the
 * buffer waits for it; so does a wait for the buffer, and a processor that
 * begins access with DMA_BUF_IOCTL_SYNC.  A job that waits for a fence not
 * yet over - another context's, by a sync file or a shared sync object -
 * stays here, with every later job of its context behind it, until the
 * fence is over; then it goes to the driver.
 *
 * Errors are Linux's for the same request, with one addition: a job that
 * failed makes the wait on its sync object fail with EIO, where Linux
 * returns 0 and leaves the error in the fence for a sync file to show
 * (here it shows there too).  A wait for a fence alone (WAIT_BO, msm's
 * WAIT_FENCE) stays as Linux has it, 0 once the fence is over: Mesa
 * asserts that WAIT_BO only ever fails with ETIMEDOUT or EBUSY.
 *
 * Moved here from the mali service (mali_drm.c, stage 11.6).
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

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

/* ---- fences and sync objects ------------------------------------------ */

struct drmdrv_fence *
drmdrv_fence_new(int signaled, void *owner)
{
	struct drmdrv_fence *f;

	if ((f = calloc(1, sizeof(*f))) == NULL)
		return NULL;
	f->refs = 1;
	f->signaled = signaled;
	f->owner = owner;
	return f;
}

struct drmdrv_fence *
drmdrv_fence_ref(struct drmdrv_fence *f)
{
	if (f != NULL)
		f->refs++;
	return f;
}

void
drmdrv_fence_unref(struct drmdrv_fence *f)
{
	unsigned i;

	if (f != NULL && --f->refs == 0) {
		for (i = 0; i < f->nchild; i++)
			drmdrv_fence_unref(f->child[i]);
		free(f->child);
		free(f);
	}
}

/* Over?  A merge is over when all its fences are, failed if any failed. */
int
drmdrv_fence_done(struct drmdrv_fence *f)
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
fence_count(struct drmdrv_fence *f)
{
	return f->nchild > 0 ? f->nchild : 1;
}

/*
 * The fences of a and b, those not yet over, as one: none - a fence that
 * is over; one - that one; more - a merge.  Linux's dma_fence_unwrap_merge()
 * does the same, and a sync file merged from one that is over and one that
 * is not is that one.  Either may be NULL.
 */
struct drmdrv_fence *
drm_fence_merge(struct drmdrv_fence *a, struct drmdrv_fence *b)
{
	struct drmdrv_fence *in[2] = { a, b }, *f, **c, *leaf;
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
		f = n == 1 ? drmdrv_fence_ref(c[0]) : drmdrv_fence_new(1, NULL);
		free(c);
		return f;
	}
	if ((f = drmdrv_fence_new(0, NULL)) == NULL) {
		free(c);
		return NULL;
	}
	for (i = 0; i < n; i++)
		drmdrv_fence_ref(c[i]);
	f->child = c;
	f->nchild = n;
	return f;
}

void
drmdrv_syncobj_set(struct drmdrv_syncobj *so, struct drmdrv_fence *f)
{
	drmdrv_fence_unref(so->fence);
	so->fence = drmdrv_fence_ref(f);
}

void
drm_syncobj_unref(struct drmdrv_syncobj *so)
{
	if (--so->refs == 0) {
		drmdrv_fence_unref(so->fence);
		free(so);
	}
}

/* A buffer's memory goes: the fence it held goes with it (drm_mem.c). */
void
drm_mem_unhook(struct drmdrv_mem *mem)
{
	drmdrv_fence_unref(mem->fence);
	mem->fence = NULL;
}

/* ---- descriptors' objects ---------------------------------------------- */

struct drmdrv_obj drm_objs[MAX_OBJ];

struct drmdrv_obj *
drm_obj_get(devminor_t minor, int kind)
{
	struct drmdrv_obj *o;

	if (minor < OBJ_MINOR || minor >= OBJ_MINOR + MAX_OBJ)
		return NULL;
	o = &drm_objs[minor - OBJ_MINOR];
	return o->kind == kind || (kind == OBJ_FREE && o->kind != OBJ_FREE) ?
	    o : NULL;
}

devminor_t
drm_obj_minor(const struct drmdrv_obj *o)
{
	return OBJ_MINOR + (devminor_t)(o - drm_objs);
}

/* The last descriptor of it is closed: what it holds goes. */
void
drm_obj_free(struct drmdrv_obj *o)
{
	switch (o->kind) {
	case OBJ_DMABUF:
		o->u.mem->minor = -1;
		drmdrv_mem_unref(o->u.mem);
		break;
	case OBJ_SYNCFILE:
		drmdrv_fence_unref(o->u.fence);
		break;
	case OBJ_SYNCOBJ:
		o->u.so->minor = -1;
		drm_syncobj_unref(o->u.so);
		break;
	}
	memset(o, 0, sizeof(*o));
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
int
drm_obj_export(const struct drmdrv_call *c, int kind, void *p, int flags,
	uint64_t size, int32_t *fd)
{
	struct drmdrv_obj *o = NULL;
	int i, r;

	if (kind == OBJ_DMABUF && ((struct drmdrv_mem *)p)->minor >= 0)
		o = drm_obj_get(((struct drmdrv_mem *)p)->minor, kind);
	else if (kind == OBJ_SYNCOBJ && ((struct drmdrv_syncobj *)p)->minor >= 0)
		o = drm_obj_get(((struct drmdrv_syncobj *)p)->minor, kind);
	if (o == NULL) {
		for (i = 0; i < MAX_OBJ && drm_objs[i].kind != OBJ_FREE; i++)
			;
		if (i == MAX_OBJ) {
			log_warn(drm_log, "every descriptor's minor is "
			    "taken (%d)\n", MAX_OBJ);
			return ENFILE;
		}
		o = &drm_objs[i];
		o->kind = kind;
		o->sel_endpt = NONE;
		switch (kind) {
		case OBJ_DMABUF:
			o->u.mem = p;
			drmdrv_mem_ref(o->u.mem);
			o->u.mem->minor = drm_obj_minor(o);
			break;
		case OBJ_SYNCFILE:
			o->u.fence = drmdrv_fence_ref(p);
			break;
		case OBJ_SYNCOBJ:
			o->u.so = p;
			o->u.so->refs++;
			o->u.so->minor = drm_obj_minor(o);
			break;
		}
	}
	o->opens++;
	if ((r = chardriver_open_minor(drm_obj_minor(o))) == OK &&
	    (r = cdevfd_new(c->user, drm_obj_minor(o), flags, size)) >= 0) {
		*fd = r;
		return OK;
	}
	if (--o->opens == 0)
		drm_obj_free(o);
	return r;
}

/*
 * The object of this kind a descriptor of the process that asks is: OK,
 * or VFS's error (EBADF: no such descriptor), or EINVAL (another device's,
 * or another kind).
 */
int
drm_obj_import(const struct drmdrv_call *c, int fd, int kind,
	struct drmdrv_obj **o)
{
	int minor;

	if ((minor = cdevfd_get(c->user, fd)) < 0)
		return minor;
	if ((*o = drm_obj_get(minor, kind)) == NULL)
		return EINVAL;
	return OK;
}

/* A fence as a new sync file of the caller's: a job's out-fence (msm). */
int
drmdrv_syncfile_export(struct drmdrv_call *c, struct drmdrv_fence *f,
	int32_t *fd)
{
	return drm_obj_export(c, OBJ_SYNCFILE, f, O_RDONLY | O_CLOEXEC, 0, fd);
}

/* The fence of a sync file of the caller's, a reference: a job's in-fence. */
int
drmdrv_syncfile_import(struct drmdrv_call *c, int fd, struct drmdrv_fence **f)
{
	struct drmdrv_obj *o;
	int r;

	if ((r = drm_obj_import(c, fd, OBJ_SYNCFILE, &o)) != OK)
		return r;
	*f = drmdrv_fence_ref(o->u.fence);
	return OK;
}

/* ---- jobs ------------------------------------------------------------- */

static void
job_release_in(struct drmdrv_job *j)
{
	unsigned i;

	for (i = 0; i < j->nin; i++)
		drmdrv_fence_unref(j->in[i]);
	free(j->in);
	j->in = NULL;
	j->nin = j->maxin = 0;
}

/*
 * A new job of ctx, waiting for at most maxin fences of others: its own
 * fence made, nothing given yet.  OK or ENOMEM.
 */
int
drmdrv_job_init(struct drmdrv_job *j, struct drmdrv_ctx *ctx, unsigned maxin)
{
	memset(j, 0, sizeof(*j));
	j->ctx = ctx;
	if (maxin > 0 && (j->in = calloc(maxin, sizeof(*j->in))) == NULL)
		return ENOMEM;
	j->maxin = maxin;
	if ((j->out = drmdrv_fence_new(0, ctx)) == NULL) {
		free(j->in);
		j->in = NULL;
		return ENOMEM;
	}
	return OK;
}

/* A job that was never given: what it holds goes. */
void
drmdrv_job_fini(struct drmdrv_job *j)
{
	job_release_in(j);
	drmdrv_fence_unref(j->out);
	j->out = NULL;
}

/* Wait for f too, unless it is over or the context's own (in order). */
int
drmdrv_job_wait_for(struct drmdrv_job *j, struct drmdrv_fence *f)
{
	unsigned i;

	if (f == NULL || drmdrv_fence_done(f) || f->owner == j->ctx)
		return OK;
	for (i = 0; i < j->nin; i++)
		if (j->in[i] == f)
			return OK;
	if (j->nin == j->maxin)
		return ENOMEM;
	j->in[j->nin++] = drmdrv_fence_ref(f);
	return OK;
}

/*
 * The implicit order of shared buffers: the job waits for the last job of
 * another context on the memory, then is the last job on it.
 */
void
drmdrv_job_uses(struct drmdrv_job *j, struct drmdrv_mem *mem)
{
	(void)drmdrv_job_wait_for(j, mem->fence);
	drmdrv_fence_unref(mem->fence);
	mem->fence = drmdrv_fence_ref(j->out);
}

/*
 * The job touches the memory but waits for nobody on it (msm's
 * NO_IMPLICIT): its fence goes beside what the memory had, so that a wait
 * for the buffer still waits for both.
 */
int
drmdrv_job_touches(struct drmdrv_job *j, struct drmdrv_mem *mem)
{
	struct drmdrv_fence *f;

	if ((f = drm_fence_merge(mem->fence, j->out)) == NULL)
		return ENOMEM;
	drmdrv_fence_unref(mem->fence);
	mem->fence = f;
	return OK;
}

/* Every fence the job waits for is over: it may go to the driver. */
static int
job_ready(struct drmdrv_job *j)
{
	unsigned i;

	for (i = 0; i < j->nin; i++)
		if (!drmdrv_fence_done(j->in[i]))
			return 0;
	return 1;
}

/* Behind a held job of its own, or held itself, or to the driver. */
void
drmdrv_job_submit(struct drmdrv_job *j)
{
	struct drmdrv_ctx *c = j->ctx;

	c->jobs++;
	if (!TAILQ_EMPTY(&c->held) || !job_ready(j))
		TAILQ_INSERT_TAIL(&c->held, j, hold);
	else {
		job_release_in(j);
		j->run(j);
	}
	/* A wait for a fence to appear may be satisfied now. */
	drmdrv_changed();
}

/*
 * A job is over: its fence over, with the error the driver gives it - the
 * error a sync file shows, and a wait on a sync object that holds the fence
 * fails EIO for (wait_state()).  The driver has let go of everything of its
 * own in the job; the context goes with its last one.
 */
void
drmdrv_job_done(struct drmdrv_job *j, int error)
{
	struct drmdrv_ctx *c = j->ctx;

	j->out->signaled = 1;
	j->out->error = error;
	drmdrv_fence_unref(j->out);
	j->out = NULL;
	job_release_in(j);
	c->jobs--;
	if (c->state == DRMDRV_CTX_CLOSING && c->jobs == 0)
		drm_ctx_free(c);
	drmdrv_changed();
}

/* Held jobs whose fences are over go to the driver, in each context's order. */
static void
release_held(void)
{
	struct drmdrv_job *j;
	int i;

	for (i = 0; i < DRMDRV_MAX_CTX; i++)
		while ((j = TAILQ_FIRST(&drm_ctxs[i].held)) != NULL &&
		    job_ready(j)) {
			TAILQ_REMOVE(&drm_ctxs[i].held, j, hold);
			job_release_in(j);
			j->run(j);
		}
}

/* The context is going: its held jobs never run, and the driver ends them. */
void
drm_cancel_held(struct drmdrv_ctx *c)
{
	struct drmdrv_job *j;

	while ((j = TAILQ_FIRST(&c->held)) != NULL) {
		TAILQ_REMOVE(&c->held, j, hold);
		j->cancel(j);
	}
}

/* ---- waiting ---------------------------------------------------------- */

static TAILQ_HEAD(, drm_wait) waits = TAILQ_HEAD_INITIALIZER(waits);
static minix_timer_t wait_timer;

static void
wait_free(struct drm_wait *w)
{
	unsigned i;

	drmdrv_fence_unref(w->fence);
	for (i = 0; i < w->count; i++)
		drm_syncobj_unref(w->so[i]);
	free(w);
}

/*
 * Where a wait stands: EDONTREPLY while it waits, else its answer.  A
 * sync object is looked at afresh each time: WAIT_FOR_SUBMIT waits for
 * a fence to be put in it, and RESET or a new submission changes it.  A
 * wait for a buffer waits for the fence it had when the wait began: a job
 * after that one is after it too.
 */
static int
wait_state(struct drm_wait *w, int64_t now)
{
	struct drmdrv_fence *f;
	unsigned i, done = 0, first = UINT_MAX;
	int error = 0;

	if (w->count == 0) {
		if (drmdrv_fence_done(w->fence))
			return OK;
		return now >= w->deadline ? ETIMEDOUT : EDONTREPLY;
	}

	for (i = 0; i < w->count; i++) {
		f = w->so[i]->fence;
		if (f == NULL && !(w->flags &
		    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT))
			return EINVAL;
		if (f != NULL && drmdrv_fence_done(f)) {
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
	drmdrv_changed();
}

/* Answer every wait that has its answer; the timer for the next one. */
static void
waits_check(void)
{
	struct drm_wait *w, *next;
	int64_t now = drmdrv_now_ns(), soonest = INT64_MAX, d;
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
unsigned
drm_obj_ready(struct drmdrv_obj *o, unsigned ops)
{
	struct drmdrv_fence *f;

	switch (o->kind) {
	case OBJ_SYNCFILE:
		/* POLLIN once the fence is over, failed or not (Linux). */
		return drmdrv_fence_done(o->u.fence) ? (ops & CDEV_OP_RD) : 0;
	case OBJ_DMABUF:
		/* Read and write once no job uses the buffer (Linux). */
		f = o->u.mem->fence;
		return drmdrv_fence_done(f) ? (ops & (CDEV_OP_RD | CDEV_OP_WR)) :
		    0;
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
		if (drm_objs[i].kind == OBJ_FREE ||
		    drm_objs[i].sel_endpt == NONE)
			continue;
		if ((ready = drm_obj_ready(&drm_objs[i],
		    drm_objs[i].sel_ops)) != 0) {
			chardriver_reply_select(drm_objs[i].sel_endpt,
			    drm_obj_minor(&drm_objs[i]), ready);
			drm_objs[i].sel_endpt = NONE;
			drm_objs[i].sel_ops = 0;
		}
	}
}

/*
 * A fence may be over, or a sync object may hold another: held jobs that
 * may go now go, waits and selects that have their answer get it.  A job
 * given to the driver may be over at once (nothing to run it on), and come
 * back here; the outer call then goes round again.
 */
void
drmdrv_changed(void)
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
wait_start(struct drmdrv_call *c, struct drm_wait *w)
{
	int r;

	w->ctx = c->ctx;
	w->endpt = c->endpt;
	w->id = c->id;
	w->grant = c->grant;
	if ((r = wait_state(w, drmdrv_now_ns())) != EDONTREPLY)
		return r;		/* the caller copies the argument out */
	TAILQ_INSERT_TAIL(&waits, w, link);
	waits_check();
	return EDONTREPLY;
}

/*
 * Until the fence is over: a buffer's last job (WAIT_BO, CPU_PREP), a
 * queue's job (WAIT_FENCE).  A deadline already past is "not now" - past,
 * which is EBUSY for a buffer and ETIMEDOUT for msm's WAIT_FENCE; one
 * reached is ETIMEDOUT.
 */
int
drmdrv_wait_fence(struct drmdrv_call *c, struct drmdrv_fence *f,
	int64_t deadline, int past)
{
	struct drm_wait *w;
	int r;

	if (drmdrv_fence_done(f))
		return OK;
	if (deadline <= drmdrv_now_ns())
		return past;
	if ((w = calloc(1, sizeof(*w))) == NULL)
		return ENOMEM;
	w->fence = drmdrv_fence_ref(f);
	w->deadline = deadline;
	if ((r = wait_start(c, w)) != EDONTREPLY)
		wait_free(w);
	return r;
}

/* A context closed: its waits end, EINTR. */
void
drm_waits_cancel_ctx(struct drmdrv_ctx *c)
{
	struct drm_wait *w, *next;

	for (w = TAILQ_FIRST(&waits); w != NULL; w = next) {
		next = TAILQ_NEXT(w, link);
		if (w->ctx == c) {
			TAILQ_REMOVE(&waits, w, link);
			chardriver_reply_task(w->endpt, w->id, EINTR);
			wait_free(w);
		}
	}
}

/* A signal for a process that waits: its wait ends, EINTR. */
int
drm_waits_cancel(endpoint_t endpt, cdev_id_t id)
{
	struct drm_wait *w;

	TAILQ_FOREACH(w, &waits, link)
		if (w->endpt == endpt && w->id == id) {
			TAILQ_REMOVE(&waits, w, link);
			wait_free(w);
			return EINTR;
		}
	return EDONTREPLY;
}

void
drm_waits_init(void)
{
	init_timer(&wait_timer);
}

/* The service is going: every wait answered EIO. */
void
drm_waits_shutdown(void)
{
	struct drm_wait *w;

	while ((w = TAILQ_FIRST(&waits)) != NULL) {
		TAILQ_REMOVE(&waits, w, link);
		chardriver_reply_task(w->endpt, w->id, EIO);
		wait_free(w);
	}
}

/* ---- sync objects ------------------------------------------------------ */

int
drm_syncobj_create(struct drmdrv_call *c, void *arg)
{
	struct drm_syncobj_create *a = arg;
	struct drmdrv_syncobj *so;
	int r;

	if (a->flags & ~DRM_SYNCOBJ_CREATE_SIGNALED)
		return EINVAL;
	if ((so = calloc(1, sizeof(*so))) == NULL)
		return ENOMEM;
	so->refs = 1;
	so->minor = -1;
	if ((a->flags & DRM_SYNCOBJ_CREATE_SIGNALED) &&
	    (so->fence = drmdrv_fence_new(1, NULL)) == NULL) {
		free(so);
		return ENOMEM;
	}
	if ((r = drm_handle_new(&c->ctx->so, &c->ctx->nso, so,
	    &a->handle)) != OK) {
		drm_syncobj_unref(so);
		return r;
	}
	return OK;
}

int
drm_syncobj_destroy(struct drmdrv_call *c, void *arg)
{
	struct drm_syncobj_destroy *a = arg;
	struct drmdrv_syncobj *so;

	if (a->pad != 0)
		return EINVAL;
	if ((so = drmdrv_syncobj_get(c->ctx, a->handle)) == NULL)
		return EINVAL;
	c->ctx->so[a->handle - 1] = NULL;
	drm_syncobj_unref(so);
	return OK;
}

/* The handles an array request names, each to a sync object. */
static int
syncobj_array(struct drmdrv_call *c, uint64_t ptr, uint32_t count,
	struct drmdrv_syncobj **so)
{
	uint32_t h[DRMDRV_MAX_SYNCS];
	unsigned i;
	int r;

	if (count == 0 || count > DRMDRV_MAX_SYNCS)
		return EINVAL;
	if ((r = drmdrv_from_user(c, ptr, h, count * sizeof(h[0]))) != OK)
		return r;
	for (i = 0; i < count; i++)
		if ((so[i] = drmdrv_syncobj_get(c->ctx, h[i])) == NULL)
			return ENOENT;
	return OK;
}

int
drm_syncobj_wait(struct drmdrv_call *c, void *arg)
{
	struct drm_syncobj_wait *a = arg;
	struct drm_wait *w;
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
syncobj_reset_signal(struct drmdrv_call *c, void *arg, int signal)
{
	struct drm_syncobj_array *a = arg;
	struct drmdrv_syncobj *so[DRMDRV_MAX_SYNCS];
	struct drmdrv_fence *f = NULL;
	unsigned i;
	int r;

	if (a->pad != 0)
		return EINVAL;
	if ((r = syncobj_array(c, a->handles, a->count_handles, so)) != OK)
		return r;
	if (signal && (f = drmdrv_fence_new(1, NULL)) == NULL)
		return ENOMEM;
	for (i = 0; i < a->count_handles; i++)
		drmdrv_syncobj_set(so[i], f);
	drmdrv_fence_unref(f);
	drmdrv_changed();
	return OK;
}

int
drm_syncobj_reset(struct drmdrv_call *c, void *arg)
{
	return syncobj_reset_signal(c, arg, 0);
}

int
drm_syncobj_signal(struct drmdrv_call *c, void *arg)
{
	return syncobj_reset_signal(c, arg, 1);
}

/*
 * SYNCOBJ_HANDLE_TO_FD: with EXPORT_SYNC_FILE, the fence the sync object
 * holds as a sync file (none: EINVAL); without, the sync object itself,
 * shared by whoever imports it.  Both close-on-exec, as Linux makes them.
 */
int
drm_syncobj_handle_to_fd(struct drmdrv_call *c, void *arg)
{
	struct drm_syncobj_handle *a = arg;
	struct drmdrv_syncobj *so;

	if (a->pad != 0)
		return EINVAL;
	if (a->flags != 0 &&
	    a->flags != DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE)
		return EINVAL;
	so = drmdrv_syncobj_get(c->ctx, a->handle);
	if (a->flags & DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE) {
		if (so == NULL)
			return ENOENT;
		if (so->fence == NULL)
			return EINVAL;
		return drm_obj_export(c, OBJ_SYNCFILE, so->fence,
		    O_RDONLY | O_CLOEXEC, 0, &a->fd);
	}
	if (so == NULL)
		return EINVAL;
	return drm_obj_export(c, OBJ_SYNCOBJ, so, O_RDONLY | O_CLOEXEC, 0,
	    &a->fd);
}

/*
 * SYNCOBJ_FD_TO_HANDLE: with IMPORT_SYNC_FILE, the sync file's fence into
 * the sync object (its fence replaced); without, a handle of this
 * context to the sync object the descriptor is.
 */
int
drm_syncobj_fd_to_handle(struct drmdrv_call *c, void *arg)
{
	struct drm_syncobj_handle *a = arg;
	struct drmdrv_syncobj *so;
	struct drmdrv_obj *o;
	int r;

	if (a->pad != 0)
		return EINVAL;
	if (a->flags != 0 &&
	    a->flags != DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE)
		return EINVAL;
	if (a->flags & DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE) {
		if (drm_obj_import(c, a->fd, OBJ_SYNCFILE, &o) != OK)
			return EINVAL;	/* none, or not a sync file */
		if ((so = drmdrv_syncobj_get(c->ctx, a->handle)) == NULL)
			return ENOENT;
		drmdrv_syncobj_set(so, o->u.fence);
		drmdrv_changed();
		return OK;
	}
	if (drm_obj_import(c, a->fd, OBJ_SYNCOBJ, &o) != OK)
		return EINVAL;		/* none, or not a sync object */
	if ((r = drm_handle_new(&c->ctx->so, &c->ctx->nso, o->u.so,
	    &a->handle)) != OK)
		return r;
	o->u.so->refs++;
	return OK;
}

/* ---- sync files --------------------------------------------------------- */

/* SYNC_IOC_MERGE: a new sync file, over when both are. */
int
drm_sync_merge(struct drmdrv_call *c, void *arg)
{
	struct sync_merge_data *a = arg;
	struct drmdrv_fence *f;
	struct drmdrv_obj *o2;
	int r;

	if (a->flags != 0 || a->pad != 0)
		return EINVAL;
	if (drm_obj_import(c, a->fd2, OBJ_SYNCFILE, &o2) != OK)
		return ENOENT;		/* none, or not a sync file */
	if ((f = drm_fence_merge(c->obj->u.fence, o2->u.fence)) == NULL)
		return ENOMEM;
	r = drm_obj_export(c, OBJ_SYNCFILE, f, O_RDONLY | O_CLOEXEC, 0,
	    &a->fence);
	drmdrv_fence_unref(f);
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
int
drm_sync_file_info(struct drmdrv_call *c, void *arg)
{
	struct sync_file_info *a = arg;
	struct sync_fence_info fi;
	struct drmdrv_fence *f = c->obj->u.fence, *leaf;
	unsigned i, n = fence_count(f);
	int r;

	if (a->flags != 0 || a->pad != 0)
		return EINVAL;
	strlcpy(a->name, drm_drv->fence_driver, sizeof(a->name));
	a->status = !drmdrv_fence_done(f) ? 0 : f->error != 0 ?
	    neg(f->error) : 1;
	if (a->num_fences == 0) {
		a->num_fences = n;
		return OK;
	}
	if (a->num_fences < n)
		return EINVAL;
	for (i = 0; i < n; i++) {
		leaf = f->nchild > 0 ? f->child[i] : f;
		memset(&fi, 0, sizeof(fi));
		strlcpy(fi.obj_name, drm_drv->fence_obj, sizeof(fi.obj_name));
		strlcpy(fi.driver_name, drm_drv->fence_driver,
		    sizeof(fi.driver_name));
		fi.status = !leaf->signaled ? 0 : leaf->error != 0 ?
		    neg(leaf->error) : 1;
		if ((r = drmdrv_to_user(c, &fi, a->sync_fence_info +
		    i * sizeof(fi), sizeof(fi))) != OK)
			return r;
	}
	a->num_fences = n;
	return OK;
}

/* ---- dma-bufs ---------------------------------------------------------- */

/*
 * DMA_BUF_IOCTL_SYNC: a processor's access to the buffer.  START waits
 * for the jobs that use it, as Linux's dma_buf_begin_cpu_access() waits
 * for its fences; the caches need nothing more - every job that uses a
 * buffer a processor maps is cleaned around (drm_mem.c).
 */
int
drm_dmabuf_sync(struct drmdrv_call *c, void *arg)
{
	struct dma_buf_sync *a = arg;

	if (a->flags & ~DMA_BUF_SYNC_VALID_FLAGS_MASK)
		return EINVAL;
	if ((a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if (a->flags & DMA_BUF_SYNC_END)
		return OK;
	return drmdrv_wait_fence(c, c->obj->u.mem->fence, INT64_MAX, EBUSY);
}

/* DMA_BUF_IOCTL_EXPORT_SYNC_FILE: the jobs that use the buffer, a sync file. */
int
drm_dmabuf_export(struct drmdrv_call *c, void *arg)
{
	struct dma_buf_export_sync_file *a = arg;
	struct drmdrv_fence *f;
	int r;

	if ((a->flags & ~DMA_BUF_SYNC_RW) || (a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if ((f = drm_fence_merge(c->obj->u.mem->fence, NULL)) == NULL)
		return ENOMEM;
	r = drm_obj_export(c, OBJ_SYNCFILE, f, O_RDONLY | O_CLOEXEC, 0,
	    &a->fd);
	drmdrv_fence_unref(f);
	return r;
}

/*
 * DMA_BUF_IOCTL_IMPORT_SYNC_FILE: a fence the next jobs using the buffer
 * wait for, and so does a wait for the buffer - added to what it had.
 */
int
drm_dmabuf_import(struct drmdrv_call *c, void *arg)
{
	struct dma_buf_import_sync_file *a = arg;
	struct drmdrv_mem *mem = c->obj->u.mem;
	struct drmdrv_fence *f;
	struct drmdrv_obj *o;

	if ((a->flags & ~DMA_BUF_SYNC_RW) || (a->flags & DMA_BUF_SYNC_RW) == 0)
		return EINVAL;
	if (drm_obj_import(c, a->fd, OBJ_SYNCFILE, &o) != OK)
		return EINVAL;		/* none, or not a sync file */
	if ((f = drm_fence_merge(mem->fence, o->u.fence)) == NULL)
		return ENOMEM;
	drmdrv_fence_unref(mem->fence);
	mem->fence = f;
	return OK;
}
