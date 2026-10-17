/*
 * libdrmdriver: what every DRM render node of MEECHO has whatever GPU is
 * behind it (port/clover/hw-gpu.md, §9).  A GPU service - mali on the CB2,
 * adreno on the Mi Pad 4 - is a character driver whose node,
 * /dev/dri/renderD128, speaks DRM to Mesa; most of what that takes does
 * not depend on the GPU, and lives here:
 *
 *   contexts      a client opens the node and gets a context of its own, a
 *                 cloned minor: its handles of buffers and sync objects,
 *                 its jobs, and the driver's half (an address space)
 *   calls         the ioctl machinery - the size rule of DRM, arguments by
 *                 grant, arrays from the caller's own memory, answers that
 *                 come later (EDONTREPLY) - and the calls every render node
 *                 answers alike: VERSION (the driver's strings), GET_CAP,
 *                 GEM_CLOSE, PRIME, the SYNCOBJ_* family
 *   fences        a job's end and merges of such; sync objects; waits with
 *                 deadlines; select(2)
 *   descriptors   a buffer as a dma-buf, a fence as a sync file, a sync
 *                 object - each a minor of the device from 256 on, made a
 *                 descriptor of the client by VFS (cdevfd(2))
 *   jobs          a client's job waits here for the fences it names and for
 *                 the last job of another context on each buffer it uses,
 *                 then goes to the driver (run), which says when it is over
 *   memory        a buffer's pages - the service's own, each page's
 *                 physical address known - a client's mapping of them
 *                 (vm_remap, mmap(2) through MIOCMMAP), the processor's
 *                 caches around a job
 *
 * What is the driver's: its own calls (DRM_COMMAND_BASE + n), a buffer
 * placed in a GPU address space (its bo, which the library knows only as
 * a pointer and asks about through struct drmdrv), and running a job.
 * Linux keeps the same line between drm_*.c and a driver; only facts are
 * taken from there (the calls' numbers, sizes and errors), not code.
 *
 * One device per service: drmdrv_init() takes the driver's description,
 * and the chardriver entry points below serve it.
 */
#ifndef _MINIX_DRMDRIVER_H
#define _MINIX_DRMDRIVER_H

#include <minix/chardriver.h>
#include <minix/log.h>

#include <sys/queue.h>

#include <stdint.h>

#define DRMDRV_RENDER_MINOR	128	/* /dev/dri/renderD128 */
#define DRMDRV_MAX_CTX		16	/* contexts: minors 0..15 */
#define DRMDRV_MAX_SYNCS	64	/* sync objects one call may name */

/* ---- fences and sync objects ------------------------------------------ */

/*
 * Something that becomes over once, with an error or without: a job's end,
 * or a merge of such - over when all of its are.
 */
struct drmdrv_fence {
	unsigned refs;
	int signaled;
	int error;			/* 0, or why the job failed */
	void *owner;			/* the context whose job it is, or NULL */
	unsigned nchild;		/* a merge: these, none of them a merge */
	struct drmdrv_fence **child;
};

struct drmdrv_syncobj {
	unsigned refs;			/* handles, waits, a descriptor */
	struct drmdrv_fence *fence;	/* NULL: none */
	int minor;			/* as a descriptor, or -1 */
};

struct drmdrv_fence *drmdrv_fence_new(int signaled, void *owner);
struct drmdrv_fence *drmdrv_fence_ref(struct drmdrv_fence *f);
void drmdrv_fence_unref(struct drmdrv_fence *f);
int drmdrv_fence_done(struct drmdrv_fence *f);
void drmdrv_syncobj_set(struct drmdrv_syncobj *so, struct drmdrv_fence *f);

/* ---- memory ------------------------------------------------------------ */

/*
 * A buffer's memory: one, whoever has it.  The service owns its pages
 * (anonymous memory of its own, page by page, so that no large buffer
 * needs contiguous RAM), and maps them into a client that asks
 * (vm_remap).  A driver places it in GPU address spaces - one placement per
 * context that has a handle to it - and counts on it what is the memory's:
 * the jobs that use it (busy), the processor's access, its last job.
 */
struct drmdrv_mem {
	uint32_t flags;			/* the driver's, as it was made */
	size_t size;			/* a whole number of pages */
	void *cpu;			/* this service's mapping; NULL: none */
	phys_bytes *pages;		/* each page's physical address */
	unsigned refs;			/* placements, descriptors */
	unsigned busy;			/* jobs queued or running with it */
	int cpu_access;			/* written or read by a processor */
	int foreign;			/* a device's memory, not this service's:
					   never freed or counted here */
	endpoint_t src_ep;		/* foreign: whose region it is, and */
	vir_bytes src_vir;		/* where - what a client is mapped from */
	size_t counted;			/* bytes of it in the total */
	struct drmdrv_fence *fence;	/* its last job, or NULL */
	int minor;			/* as a dma-buf descriptor, or -1 */
};

/* Memory given out in all may not pass this (drmdrv_mem_room()). */
void drmdrv_mem_limit(size_t total);
size_t drmdrv_mem_total(void);
int drmdrv_mem_room(size_t bytes);
void drmdrv_mem_charge(size_t bytes);
void drmdrv_mem_discharge(size_t bytes);

struct drmdrv_mem *drmdrv_mem_new(size_t size, uint32_t flags, int *err);
struct drmdrv_mem *drmdrv_mem_new_unbacked(size_t size, uint32_t flags,
	int *err);
struct drmdrv_mem *drmdrv_mem_new_phys(phys_bytes phys, endpoint_t src_ep,
	vir_bytes src_vir, size_t size, uint32_t flags, int *err);
void drmdrv_mem_ref(struct drmdrv_mem *mem);
void drmdrv_mem_unref(struct drmdrv_mem *mem);
int drmdrv_mem_map_into(struct drmdrv_mem *mem, endpoint_t who, void *addr,
	int writable, void **where);
void drmdrv_mem_for_gpu(struct drmdrv_mem *mem);
void drmdrv_mem_for_cpu(struct drmdrv_mem *mem);

/* Pages of the service's own, cleaned to RAM, each one's physical address. */
int drmdrv_pages_new(size_t size, void **cpu, phys_bytes *pages);
void drmdrv_pages_free(void *cpu, size_t size);
void drmdrv_cache(int op, void *addr, size_t len);

/* The physical address of bytes another process has mapped (drm_umap.c). */
int drmdrv_umap_remote(endpoint_t ep, vir_bytes vir, vir_bytes bytes,
	phys_bytes *phys);

/* ---- contexts, calls, jobs --------------------------------------------- */

struct drmdrv_job;
TAILQ_HEAD(drmdrv_jobq, drmdrv_job);

#define DRMDRV_CTX_FREE		0
#define DRMDRV_CTX_OPEN		1
#define DRMDRV_CTX_CLOSING	2	/* closed, its last job not over */

struct drmdrv_ctx {
	int state;
	int minor;
	char name[16];			/* "client 3", for the log */
	void **bo;			/* the driver's buffer with handle h: bo[h - 1] */
	unsigned nbo;
	void **so;			/* sync object with handle h: so[h - 1] */
	unsigned nso;
	unsigned jobs;			/* given and not over */
	unsigned failed;		/* jobs that failed, for the log */
	struct drmdrv_jobq held;	/* waiting for others' fences */
	void *priv;			/* the driver's: its address space... */
};

struct drmdrv_obj;

/* A request, as the driver's call sees it. */
struct drmdrv_call {
	struct drmdrv_ctx *ctx;		/* NULL: a descriptor's object */
	struct drmdrv_obj *obj;
	endpoint_t endpt;		/* VFS */
	cp_grant_id_t grant;
	size_t usize;			/* the argument's size, caller's */
	endpoint_t user;		/* the process that asked */
	cdev_id_t id;
};

/* A call of the driver's: DRM_COMMAND_BASE + nr, its structure's size. */
struct drmdrv_calldef {
	unsigned nr;
	size_t size;
	int (*fn)(struct drmdrv_call *c, void *arg);
};

/*
 * A client's job, the part that is the library's: what it waits for, and
 * the fence that is over when it is.  The driver embeds it in its own job,
 * fills it (drmdrv_job_init(), _wait_for(), _uses()), gives it
 * (drmdrv_job_submit()) and, once the GPU is done with it, ends it
 * (drmdrv_job_done()).  run is called when every fence it waits for is
 * over, in its context's order; cancel when its context goes before that -
 * the driver ends it then too.
 */
struct drmdrv_job {
	struct drmdrv_ctx *ctx;
	struct drmdrv_fence *out;	/* over when it is */
	TAILQ_ENTRY(drmdrv_job) hold;	/* in ctx->held */
	struct drmdrv_fence **in;	/* what it waits for, still held here */
	unsigned nin, maxin;
	void (*run)(struct drmdrv_job *j);
	void (*cancel)(struct drmdrv_job *j);
};

int drmdrv_job_init(struct drmdrv_job *j, struct drmdrv_ctx *ctx,
	unsigned maxin);
void drmdrv_job_fini(struct drmdrv_job *j);
int drmdrv_job_wait_for(struct drmdrv_job *j, struct drmdrv_fence *f);
void drmdrv_job_uses(struct drmdrv_job *j, struct drmdrv_mem *mem);
int drmdrv_job_touches(struct drmdrv_job *j, struct drmdrv_mem *mem);
void drmdrv_job_submit(struct drmdrv_job *j);
void drmdrv_job_done(struct drmdrv_job *j, int error);

/* The driver: its name, its calls, and what the library asks of its buffers. */
struct drmdrv {
	const char *name, *date, *desc;		/* DRM_IOCTL_VERSION */
	int major, minor, patchlevel;
	const char *fence_driver, *fence_obj;	/* SYNC_IOC_FILE_INFO */
	struct log *log;
	const struct drmdrv_calldef *calls;	/* ends with fn NULL */

	/* A context opens (its address space), and its jobs not yet given to
	 * the GPU end at close; after its last job the context goes. */
	int (*ctx_open)(struct drmdrv_ctx *c);
	void (*ctx_cancel)(struct drmdrv_ctx *c);
	void (*ctx_close)(struct drmdrv_ctx *c);

	/* A buffer of the driver's: its memory, its last reference dropped,
	 * the token mmap(2) of the render node maps it by (0: never), and
	 * another context's memory placed in this one (PRIME). */
	struct drmdrv_mem *(*bo_mem)(void *bo);
	void (*bo_unref)(void *bo);
	uint64_t (*bo_mmap_offset)(void *bo);
	void *(*bo_import)(struct drmdrv_ctx *c, struct drmdrv_mem *mem,
		int *err);
};

void drmdrv_init(const struct drmdrv *d);
void drmdrv_shutdown(void);
void drmdrv_changed(void);
int64_t drmdrv_now_ns(void);
struct drmdrv_ctx *drmdrv_ctx(int i);

/* The entry points of the render node; the driver's chardriver uses them. */
int drmdrv_open(devminor_t minor, int access, endpoint_t user);
int drmdrv_close(devminor_t minor);
int drmdrv_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags, endpoint_t user, cdev_id_t id);
int drmdrv_cancel(devminor_t minor, endpoint_t endpt, cdev_id_t id);
int drmdrv_select(devminor_t minor, unsigned int ops, endpoint_t endpt);

/* For the driver's calls. */
int drmdrv_from_user(const struct drmdrv_call *c, uint64_t ptr, void *buf,
	size_t len);
int drmdrv_to_user(const struct drmdrv_call *c, const void *buf,
	uint64_t ptr, size_t len);
int drmdrv_bo_handle(struct drmdrv_ctx *c, void *bo, uint32_t *handle);
void *drmdrv_bo_get(struct drmdrv_ctx *c, uint32_t handle);
struct drmdrv_syncobj *drmdrv_syncobj_get(struct drmdrv_ctx *c,
	uint32_t handle);
int drmdrv_wait_fence(struct drmdrv_call *c, struct drmdrv_fence *f,
	int64_t deadline, int past);
int drmdrv_syncfile_export(struct drmdrv_call *c, struct drmdrv_fence *f,
	int32_t *fd);
int drmdrv_syncfile_import(struct drmdrv_call *c, int fd,
	struct drmdrv_fence **f);

#endif /* _MINIX_DRMDRIVER_H */
