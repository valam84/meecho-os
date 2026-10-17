/*
 * libdrmdriver's own: what its files share and a driver never sees.
 */
#ifndef _DRM_PRIV_H
#define _DRM_PRIV_H

#include <minix/drmdriver.h>
#include <minix/timers.h>

#include "drm.h"

#define MAX_HANDLES	65536		/* buffers, and sync objects, each */

/*
 * Descriptors' minors.  libchardriver serves a minor only after it has
 * seen it opened, and remembers every one it has seen, 256 at most
 * (MAX_NR_OPEN_DEVICES): 16 contexts and the node take 17.  A minor is
 * reused lowest first, so the minors ever seen are as many as the most
 * descriptors' objects there were at once.
 */
#define OBJ_MINOR	256
#define MAX_OBJ		224

#define OBJ_FREE	0
#define OBJ_DMABUF	1		/* a buffer's memory */
#define OBJ_SYNCFILE	2		/* a fence */
#define OBJ_SYNCOBJ	3		/* a sync object */

struct drmdrv_obj {
	int kind;
	unsigned opens;			/* descriptors VFS made, not closed */
	union {
		struct drmdrv_mem *mem;
		struct drmdrv_fence *fence;
		struct drmdrv_syncobj *so;
	} u;
	endpoint_t sel_endpt;		/* a select that waits, or NONE */
	unsigned sel_ops;
};

extern struct drmdrv_obj drm_objs[MAX_OBJ];
extern struct drmdrv_ctx drm_ctxs[DRMDRV_MAX_CTX];
extern const struct drmdrv *drm_drv;

/*
 * The driver's log (drmdrv_init()).  Memory may be asked for before the
 * device is set up - a driver's self-test - so drm_mem.c says nothing
 * until there is a log to say it in.
 */
extern struct log *drm_log;
#define drm_warn(...)	do { if (drm_log != NULL)			\
	log_warn(drm_log, __VA_ARGS__); } while (0)

struct drmdrv_obj *drm_obj_get(devminor_t minor, int kind);
devminor_t drm_obj_minor(const struct drmdrv_obj *o);
void drm_obj_free(struct drmdrv_obj *o);
int drm_obj_export(const struct drmdrv_call *c, int kind, void *p, int flags,
	uint64_t size, int32_t *fd);
int drm_obj_import(const struct drmdrv_call *c, int fd, int kind,
	struct drmdrv_obj **o);

int drm_handle_new(void ***tab, unsigned *n, void *p, uint32_t *handle);
void drm_ctx_free(struct drmdrv_ctx *c);
void drm_syncobj_unref(struct drmdrv_syncobj *so);
struct drmdrv_fence *drm_fence_merge(struct drmdrv_fence *a,
	struct drmdrv_fence *b);

/* A call that waits: for a fence (WAIT_BO, WAIT_FENCE, a dma-buf's SYNC), or
 * for sync objects (SYNCOBJ_WAIT). */
struct drm_wait {
	TAILQ_ENTRY(drm_wait) link;
	struct drmdrv_ctx *ctx;		/* NULL: a dma-buf's */
	endpoint_t endpt;		/* VFS, to reply to */
	cdev_id_t id;
	cp_grant_id_t grant;		/* for SYNCOBJ_WAIT's first_signaled */
	size_t copyout;
	int64_t deadline;		/* ns of CLOCK_MONOTONIC; INT64_MAX */
	struct drmdrv_fence *fence;	/* a fence's */
	struct drmdrv_syncobj *so[DRMDRV_MAX_SYNCS];	/* SYNCOBJ_WAIT */
	unsigned count;
	uint32_t flags;
	struct drm_syncobj_wait args;
};

void drm_waits_cancel_ctx(struct drmdrv_ctx *c);
int drm_waits_cancel(endpoint_t endpt, cdev_id_t id);
void drm_waits_shutdown(void);
void drm_waits_init(void);
unsigned drm_obj_ready(struct drmdrv_obj *o, unsigned ops);
void drm_cancel_held(struct drmdrv_ctx *c);
void drm_mem_unhook(struct drmdrv_mem *mem);

/* The calls of drm_sync.c, for the tables of drm_dev.c. */
int drm_syncobj_create(struct drmdrv_call *c, void *arg);
int drm_syncobj_destroy(struct drmdrv_call *c, void *arg);
int drm_syncobj_handle_to_fd(struct drmdrv_call *c, void *arg);
int drm_syncobj_fd_to_handle(struct drmdrv_call *c, void *arg);
int drm_syncobj_wait(struct drmdrv_call *c, void *arg);
int drm_syncobj_reset(struct drmdrv_call *c, void *arg);
int drm_syncobj_signal(struct drmdrv_call *c, void *arg);
int drm_sync_merge(struct drmdrv_call *c, void *arg);
int drm_sync_file_info(struct drmdrv_call *c, void *arg);
int drm_dmabuf_sync(struct drmdrv_call *c, void *arg);
int drm_dmabuf_export(struct drmdrv_call *c, void *arg);
int drm_dmabuf_import(struct drmdrv_call *c, void *arg);

#endif /* _DRM_PRIV_H */
