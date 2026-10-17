/*
 * The GPU kept up for the render node (port/clover/hw-gpu.md, step 7): what
 * the stages bring up and put back once (adreno_run.c) brought up for good,
 * and the ring fed with the clients' jobs.
 *
 * A job goes into the ring in the form of Linux's a5xx_submit() for a
 * single ring, without preemption (a5xx_gpu.c, GPL - the order and the
 * values, not the code; hw-gpu.md, §7):
 *
 *   CP_SET_PROTECTED_MODE 0; CP_CONTEXT_SWITCH_SAVE_ADDR 0 (no preemption
 *   record: one ring); CP_SET_PROTECTED_MODE 1;
 *   CP_INDIRECT_BUFFER_PFE per command buffer (an IB target is reached
 *   from another and not put here; a context-restore buffer only when the
 *   ring's last job was another client's);
 *   CP_SCRATCH_REG(2) = the job's fence;
 *   CP_EVENT_WRITE CACHE_FLUSH_TS with an interrupt: the caches written
 *   back, the fence to memory, the GPU's line.
 *
 * WHERE_AM_I goes after each job (cp_kick()), not every 32 buffers as
 * Linux's: that one is for Linux's ring space, read from the shadow; here
 * the space is known from the fences - a job whose fence is in memory has
 * been read past.
 *
 * Each client has an address space of its own (adreno_drm.c), as on the
 * CB2's Mali, where Linux on this GPU has one for everybody: TTBR1 is
 * forbidden on this SoC, and the ring cannot switch tables itself without
 * one (hw-gpu.md, §4).  The service switches them instead, between jobs:
 * a job of another client waits until the ring is empty and the GPU
 * idle, bank 0 is given the client's table (smmu_load(): TTBR0, TLB
 * invalidated), and the job begins by invalidating the GPU's UCHE - what
 * Mesa's own fd5_cache_flush() writes - so that nothing the last client
 * read is found at the same address by the next one.  The service's
 * buffers are in every table at the same address.
 *
 * The GPU's line (332) brings the fences: every job up to the one whose
 * fence memory holds is over.  Bank 0's line (361) brings a fault in a
 * client's space: the access was terminated (the bank does not stall),
 * the GPU goes on, and the job that ran - the first in the ring - fails
 * when it ends.  A job that makes no progress for hang= ms (2000; the GPU
 * runs at 19.2 MHz yet) is a hang, as is the GPU's own hang detection:
 * every job in the ring fails, the GPU is powered off and brought up again
 * as at the start (the zap shader resumed, not loaded), and the jobs that
 * waited go on.
 */

#include "adreno_drm.h"
#include "adreno_regs.h"

#include <minix/sysutil.h>
#include <minix/timers.h>

#include <stdlib.h>
#include <string.h>

/* Registers and packets beside those of the stages (a5xx.xml, pm4.xml). */
#define A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR	0x083e	/* and _HI 0x083f */
#define A5XX_UCHE_CACHE_INVALIDATE_MIN		0x0e91	/* MIN, MAX, then */
#define UCHE_INVALIDATE_ALL			0x12	/* 0x0e95: fd5's */
#define CP_INDIRECT_BUFFER_PFE			0x3f
#define CP_WAIT_FOR_IDLE			0x26

/* What the GPU's line reports and the service does not wait out: the
 * GPU's own hang detection - the rest is logged (a5xx_irq()). */
#define INT_ERRORS	(0x7eu | A5XX_INT_CP_HW_ERROR | (1u << 24))

unsigned gpu_global_faults;
long ad_hang_ms = 2000;

static TAILQ_HEAD(, ad_job) pending = TAILQ_HEAD_INITIALIZER(pending);
static TAILQ_HEAD(, ad_job) flight = TAILQ_HEAD_INITIALIZER(flight);
static struct ad_vm *on_ring;		/* whose table bank 0 walks; NULL: ours */
static struct ad_vm *last_vm;		/* the ring's last job's client */
static int inval_due;			/* the next job begins with UCHE's */
static uint32_t tail;			/* the CP is past this, in dwords */
static int up;				/* taking jobs into the ring */
static int dead;			/* did not come back from a hang */
static minix_timer_t hang_timer, retry_timer;
static int hang_armed, retry_armed;
static uint32_t hang_fence;		/* the fence at the last look */
static unsigned hang_looks;
static unsigned quiet_looks;		/* a switch of tables waiting: looks */

static void kick(void);

/*===========================================================================*
 *    the ring                                                               *
 *===========================================================================*/
static uint32_t
ring_free(void)
{
	uint32_t n = cp_ring_dwords();

	return (tail + n - cp_wptr() - 1) % n;
}

/* Words a job takes in the ring, its kick included. */
static uint32_t
job_dwords(const struct ad_job *j, int inval)
{
	return 2 + 3 + 2 + (inval ? 6 + 1 : 0) + 4 * j->ncmds + 2 + 5 +
	    cp_kick_dwords();
}

int
gpu_dead(void)
{
	return dead;
}

/* The largest job the ring takes (adreno_drm.c refuses bigger ones). */
uint32_t
gpu_max_cmds(void)
{
	return (cp_ring_dwords() - 64) / 4;
}

static void
emit(struct ad_job *j)
{
	uint64_t fence = cp_fence_va();
	int restore = last_vm != j->vm;
	unsigned i;

	cp_emit(pm4_pkt7(CP_SET_PROTECTED_MODE, 1));
	cp_emit(0);
	cp_emit(pm4_pkt4(A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR, 2));
	cp_emit(0);
	cp_emit(0);
	cp_emit(pm4_pkt7(CP_SET_PROTECTED_MODE, 1));
	cp_emit(1);
	if (inval_due) {
		cp_emit(pm4_pkt4(A5XX_UCHE_CACHE_INVALIDATE_MIN, 5));
		cp_emit(0);
		cp_emit(0);
		cp_emit(0);
		cp_emit(0);
		cp_emit(UCHE_INVALIDATE_ALL);
		cp_emit(pm4_pkt7(CP_WAIT_FOR_IDLE, 0));
		inval_due = 0;
	}
	for (i = 0; i < j->ncmds; i++) {
		if (j->cmds[i].type == AD_CMD_TARGET ||
		    (j->cmds[i].type == AD_CMD_RESTORE && !restore))
			continue;
		cp_emit(pm4_pkt7(CP_INDIRECT_BUFFER_PFE, 3));
		cp_emit((uint32_t)j->cmds[i].iova);
		cp_emit((uint32_t)(j->cmds[i].iova >> 32));
		cp_emit(j->cmds[i].dwords);
	}
	cp_emit(pm4_pkt4(A5XX_CP_SCRATCH_REG(2), 1));
	cp_emit(j->seqno);
	cp_emit(pm4_pkt7(CP_EVENT_WRITE, 4));
	cp_emit(CP_EVENT_WRITE_IRQ | CACHE_FLUSH_TS);
	cp_emit((uint32_t)fence);
	cp_emit((uint32_t)(fence >> 32));
	cp_emit(j->seqno);
	cp_kick();
	j->end = cp_wptr();
}

/* A job out of the ring: its buffers to the processor, then its owner. */
static void
finish(struct ad_job *j)
{
	unsigned i;

	for (i = 0; i < j->nbos; i++) {
		j->bos[i]->mem->busy--;
		drmdrv_mem_for_cpu(j->bos[i]->mem);
	}
	j->done(j);
}

/* Look again in a tick: the GPU was not idle yet for a switch. */
static void
retry_fired(int UNUSED(arg))
{
	retry_armed = 0;
	kick();
}

static void
hang_fired(int arg);

static void
hang_arm(void)
{
	if (hang_armed)
		return;
	hang_fence = cp_fence();
	hang_looks = 0;
	set_timer(&hang_timer, (clock_t)(ad_hang_ms * (long)sys_hz() / 1000 +
	    1), hang_fired, 0);
	hang_armed = 1;
}

/*
 * The GPU idle - read past everything, nothing busy - polled a while: the
 * table switches only under that, and a table is freed only after it.
 */
static int
quiesce(unsigned us)
{
	unsigned t;

	for (t = 0; !cp_quiet(); t += 10) {
		if (t >= us)
			return 0;
		micro_delay(10);
	}
	return 1;
}

/*
 * Jobs that may go into the ring now go: in the order given, each after
 * the last if the ring has room, a client's after another's only once the
 * GPU is idle and the client's table loaded.
 */
static void
kick(void)
{
	struct ad_job *j;
	unsigned i;
	int r;

	while (up && (j = TAILQ_FIRST(&pending)) != NULL) {
		if (j->vm != on_ring) {
			if (!TAILQ_EMPTY(&flight))
				return;		/* after the other's last job */
			if (!quiesce(100)) {
				/* What keeps it from looking idle: once. */
				if (quiet_looks++ == 0)
					cp_say("not idle for a switch of tables");
				if (!retry_armed) {
					set_timer(&retry_timer, 1, retry_fired,
					    0);
					retry_armed = 1;
				}
				return;
			}
			if (quiet_looks > 0)
				log_info(&ad_log, "gpu: idle for a switch of "
				    "tables after %u looks\n", quiet_looks);
			quiet_looks = 0;
			if ((r = smmu_load(&j->vm->pt)) != OK) {
				log_warn(&ad_log, "gpu: %s's table not loaded: "
				    "%d\n", j->vm->name, r);
				TAILQ_REMOVE(&pending, j, link);
				j->result = EIO;
				j->done(j);
				continue;
			}
			on_ring = j->vm;
			inval_due = 1;
		}
		if (job_dwords(j, inval_due) > ring_free())
			return;			/* room comes with the fences */
		TAILQ_REMOVE(&pending, j, link);
		for (i = 0; i < j->nbos; i++) {
			j->bos[i]->mem->busy++;
			drmdrv_mem_for_gpu(j->bos[i]->mem);
		}
		j->seqno = cp_seq_next();
		emit(j);
		last_vm = j->vm;
		TAILQ_INSERT_TAIL(&flight, j, link);
		hang_arm();
	}
}

static void smmu_line(void);

/*
 * Every job whose fence memory holds is over; the room it took is free.  A
 * fault in bank 0 is looked for first: its line and the GPU's come
 * separately, and a job must not end well because the fence's line was
 * taken before the fault's.
 */
static void
retire(void)
{
	uint32_t fence;
	struct ad_job *j;

	smmu_line();
	fence = cp_fence();
	while ((j = TAILQ_FIRST(&flight)) != NULL &&
	    (int32_t)(fence - j->seqno) >= 0) {
		TAILQ_REMOVE(&flight, j, link);
		tail = j->end;
		finish(j);
	}
	kick();
}

/*===========================================================================*
 *    a hang                                                                 *
 *===========================================================================*/
/*
 * The GPU does not come back: every job fails, and the render node stays,
 * answering EIO, until the service is restarted.
 */
static void
give_up(void)
{
	struct ad_job *j;

	up = 0;
	dead = 1;
	while ((j = TAILQ_FIRST(&pending)) != NULL) {
		TAILQ_REMOVE(&pending, j, link);
		j->result = EIO;
		j->done(j);
	}
}

/*
 * Every job in the ring fails - the first is what hung - the GPU is
 * powered off and brought up again with the route and the tables as they
 * were, and the jobs that waited go on (Linux replays the innocent ones;
 * here they fail with the guilty, which is what a client's robustness
 * query is for).
 */
static void
recover(const char *why)
{
	struct ad_job *j;
	int r;

	cp_say(why);
	gpu_global_faults++;
	up = 0;
	if (hang_armed) {
		cancel_timer(&hang_timer);
		hang_armed = 0;
	}
	if (cp_halt() != OK)
		log_warn(&ad_log, "gpu: the VBIF's ports did not halt\n");
	while ((j = TAILQ_FIRST(&flight)) != NULL) {
		TAILQ_REMOVE(&flight, j, link);
		if (j->result == OK)
			j->result = EIO;
		finish(j);
	}
	cc_power_down();
	if ((r = cc_power_up(AD_CP)) != OK || (r = smmu_check()) != OK ||
	    (r = cp_boot()) != OK) {
		log_warn(&ad_log, "gpu: the GPU does not come back: %d; every "
		    "job fails from now on\n", r);
		give_up();
		return;
	}
	(void)cp_irq();
	if (smmu_loaded() == NULL)
		on_ring = NULL;		/* the bank was set up again */
	last_vm = NULL;
	inval_due = 1;			/* the GPU's caches started anew anyway */
	tail = cp_wptr();
	up = 1;
	log_warn(&ad_log, "gpu: up again after a hang (%u so far)\n",
	    gpu_global_faults);
	kick();
}

/*
 * The hang check (hw-gpu.md, §7: Linux's hangcheck): jobs in the ring and
 * the fence not moved since the last look - twice, so that a look just
 * after a job began is not counted.
 */
static void
hang_fired(int UNUSED(arg))
{
	uint32_t fence;

	hang_armed = 0;
	if (TAILQ_EMPTY(&flight) || !up)
		return;
	fence = cp_fence();
	if (fence != hang_fence) {
		hang_arm();
		return;
	}
	if (++hang_looks < 2) {
		set_timer(&hang_timer, (clock_t)(ad_hang_ms * (long)sys_hz() /
		    1000 + 1), hang_fired, 0);
		hang_armed = 1;
		return;
	}
	retire();
	if (!TAILQ_EMPTY(&flight))
		recover("gpu: a job made no progress");
}

/*===========================================================================*
 *    the lines                                                              *
 *===========================================================================*/
/* Bank 0: a fault in the space of the job that runs. */
static void
smmu_line(void)
{
	uint32_t fsr, fsynr0;
	uint64_t far;
	struct ad_job *j;

	if (!smmu_fault(&fsr, &far, &fsynr0))
		return;
	j = TAILQ_FIRST(&flight);
	if (j == NULL) {
		log_warn(&ad_log, "gpu: a fault at %llx (fsr %08x fsynr0 %08x) "
		    "with no job in the ring\n", (unsigned long long)far, fsr,
		    fsynr0);
		return;
	}
	j->vm->faults++;
	if (j->result == OK) {
		j->result = EIO;
		j->fault_fsr = fsr;
		j->fault_far = far;
		log_warn(&ad_log, "gpu: %s: a fault at %llx (fsr %08x fsynr0 "
		    "%08x), the access terminated; job %u fails\n", j->vm->name,
		    (unsigned long long)far, fsr, fsynr0, j->seqno);
	}
}

/* The GPU's: fences, and what went wrong. */
static void
gpu_line(void)
{
	uint32_t st = cp_irq();

	if (st & INT_ERRORS)
		cp_say("gpu: an error interrupt");
	if (st & A5XX_INT_MISC_HANG_DETECT) {
		retire();
		if (!TAILQ_EMPTY(&flight))
			recover("gpu: the GPU detected a hang");
		return;
	}
	retire();
}

void
gpu_intr(uint64_t mask)
{
	if (mask & (1ull << AD_SMMU_HOOK))
		smmu_line();
	if (mask & (1ull << AD_GPU_HOOK))
		gpu_line();
}

/*===========================================================================*
 *    for adreno_drm.c                                                       *
 *===========================================================================*/
/* Into the ring, after what is there and what waits for it. */
void
gpu_submit(struct ad_job *j)
{
	if (dead) {
		j->result = EIO;
		j->done(j);
		return;
	}
	j->result = OK;
	TAILQ_INSERT_TAIL(&pending, j, link);
	kick();
}

/* A client closed: its jobs not yet in the ring never get there. */
void
gpu_cancel(struct ad_vm *vm)
{
	struct ad_job *j, *next;

	for (j = TAILQ_FIRST(&pending); j != NULL; j = next) {
		next = TAILQ_NEXT(j, link);
		if (j->vm == vm) {
			TAILQ_REMOVE(&pending, j, link);
			j->result = ECANCELED;
			j->done(j);
		}
	}
}

/*
 * A client's space goes, its last job over: if bank 0 walks its table, the
 * service's own goes there first - once the GPU is idle, so that no walk
 * is left in the table freed next.
 */
void
gpu_vm_gone(struct ad_vm *vm)
{
	if (last_vm == vm)
		last_vm = NULL;
	if (on_ring != vm)
		return;
	if (up && !quiesce(100000)) {
		log_warn(&ad_log, "gpu: not idle when %s's table goes\n",
		    vm->name);
		cp_say("not idle");
	}
	(void)smmu_load(NULL);
	on_ring = NULL;
	kick();
}

/*===========================================================================*
 *    up and down                                                            *
 *===========================================================================*/
int
gpu_serve(void)
{
	int r;

	init_timer(&hang_timer);
	init_timer(&retry_timer);
	if ((r = cc_power_up(AD_CP)) == OK && (r = smmu_serve()) == OK &&
	    (r = cp_alloc()) == OK && (r = cp_boot()) == OK)
		r = cp_first();
	if (r != OK) {
		cp_stop();
		smmu_undo();
		cc_power_down();
		cp_free();
		return r;
	}
	/* The first command's line was taken: the kernel keeps it masked
	 * until the driver says so. */
	(void)cp_irq();
	tail = cp_wptr();
	on_ring = last_vm = NULL;
	up = 1;
	dead = 0;
	return OK;
}

/*
 * The service goes: whatever waits is cancelled, whatever runs fails (the
 * GPU is stopped under it), and then - adreno_drm_shutdown() having freed
 * every client's space - everything as found.
 */
void
gpu_stop(void)
{
	struct ad_job *j;

	up = 0;
	if (hang_armed)
		cancel_timer(&hang_timer);
	if (retry_armed)
		cancel_timer(&retry_timer);
	hang_armed = retry_armed = 0;
	(void)cp_halt();
	while ((j = TAILQ_FIRST(&flight)) != NULL) {
		TAILQ_REMOVE(&flight, j, link);
		j->result = EIO;
		finish(j);
	}
	while ((j = TAILQ_FIRST(&pending)) != NULL) {
		TAILQ_REMOVE(&pending, j, link);
		j->result = ECANCELED;
		j->done(j);
	}
	(void)smmu_load(NULL);
	on_ring = NULL;
}

/* After adreno_drm_shutdown(): the rest down, back as found (Q23, Q17, Q11). */
void
gpu_down(void)
{
	cp_stop();
	smmu_undo();
	cc_power_down();
	cp_free();
}
