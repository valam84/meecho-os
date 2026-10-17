/*
 * The render node of the adreno service (port/clover/hw-gpu.md, step 7):
 * the interface Mesa's freedreno talks to, msm DRM 1.x, on libdrmdriver.
 *
 *   adreno_drm.c   the calls: buffers, queues, submissions, waits - and
 *                  each client's GPU address space (struct ad_vm)
 *   adreno_gpu.c   the GPU kept up: jobs into the ring in order, a table
 *                  switch between clients' jobs, fences by the GPU's line,
 *                  faults by the SMMU's, a hang's recovery
 *
 * A header of its own, not adreno.h: the stages' host stand builds the
 * rest of the service without libdrmdriver and libchardriver.
 */
#ifndef _ADRENO_DRM_H
#define _ADRENO_DRM_H

#include "adreno.h"

#include <minix/chardriver.h>
#include <minix/drmdriver.h>

#include <sys/queue.h>

/* Where a client's buffers go: below them the service's own (adreno_cp.c,
 * from 32 MiB), above them nothing of a 32-bit address. */
#define AD_VA_START	0x04000000ull
#define AD_VA_END	0x100000000ull

struct ad_bo;
TAILQ_HEAD(ad_bo_list, ad_bo);

/* A client's GPU address space: its table, its buffers in it. */
struct ad_vm {
	struct ad_pt pt;
	struct ad_bo_list bos;		/* by address */
	size_t mem;			/* bytes of buffers placed */
	unsigned faults;		/* SMMU faults of its jobs */
	const char *name;		/* the context's, for the log */
};

/* A buffer of a client: memory (libdrmdriver's) placed in its space. */
struct ad_bo {
	TAILQ_ENTRY(ad_bo) link;	/* in its space, by address */
	struct ad_vm *vm;
	struct drmdrv_mem *mem;
	uint64_t iova;
	size_t size;			/* a whole number of pages */
	uint32_t flags;			/* MSM_BO_* */
	unsigned refs;			/* a handle, a job each */
	uint64_t mmap_offset;		/* the token GET_OFFSET gives */
	char name[32];			/* MSM_INFO_SET_NAME */
	void *meta;			/* MSM_INFO_SET_METADATA */
	size_t metalen;
};

/* A command buffer of a job, by the submission's type for it. */
#define AD_CMD_BUF	1		/* run from the ring */
#define AD_CMD_TARGET	2		/* run from another buffer only */
#define AD_CMD_RESTORE	3		/* run if the ring's last job was
					   another client's */

/*
 * A job: a submission's command buffers, run from the ring in its client's
 * address space, in the order given.  done() is told when it is over -
 * result OK, EIO (a fault in its space, a hang), or ECANCELED.
 */
struct ad_job {
	struct drmdrv_job dj;		/* libdrmdriver's: fences, order */
	TAILQ_ENTRY(ad_job) link;	/* waiting for the ring, or in it */
	struct ad_vm *vm;
	unsigned ncmds;
	struct ad_cmd {
		uint64_t iova;
		uint32_t dwords;
		int type;		/* AD_CMD_* */
	} *cmds;
	unsigned nbos;
	struct ad_bo **bos;		/* a reference each */
	uint32_t seqno;			/* the ring's fence */
	uint32_t end;			/* where it ends in the ring */
	int result;
	uint32_t fault_fsr;
	uint64_t fault_far;
	void (*done)(struct ad_job *j);
};

/* adreno_gpu.c */
int gpu_serve(void);			/* up: power, SMMU, CP */
void gpu_stop(void);			/* nothing runs any more */
void gpu_down(void);			/* everything down, back as found */
void gpu_submit(struct ad_job *j);
void gpu_cancel(struct ad_vm *vm);
void gpu_vm_gone(struct ad_vm *vm);
void gpu_intr(uint64_t mask);
uint32_t gpu_max_cmds(void);
int gpu_dead(void);			/* did not come back from a hang */
extern unsigned gpu_global_faults;
extern long ad_hang_ms;			/* hang= (adreno.c) */
extern unsigned ad_freq_mhz;		/* freq= (adreno.c) */
#define AD_GPU_HOOK	1		/* the GPU's line (cp_first()) */
#define AD_SMMU_HOOK	0		/* bank 0's (smmu_serve()) */

/* adreno_drm.c */
extern struct chardriver adreno_chardriver;
void adreno_drm_limits(size_t total, size_t per_client);
void adreno_drm_init(void);
void adreno_drm_shutdown(void);

#endif /* _ADRENO_DRM_H */
