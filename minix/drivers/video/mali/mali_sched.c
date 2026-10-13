/*
 * Jobs: which runs when, and how each one ends.
 *
 * The GPU has three job slots (JS_PRESENT 7); panfrost gives fragment
 * jobs slot 0 and everything else slot 1, and so does this.  A job is
 * given by writing the slot's "next" registers - the chain's first
 * descriptor, the cores, the configuration with the job's address-space
 * window - and START; the slot's done or failed bit comes on the job line
 * when the chain is over.  One job per slot at a time.
 *
 * The order.  Jobs of one address space (one client) run one at a time
 * and in the order given; jobs of different address spaces run side by
 * side as slots and windows allow.  Panfrost orders jobs by the buffers
 * they share, and every job of a GL client shares its tiler heap with
 * the one before, so for the client this is the same order, without
 * fences inside one process; and it keeps "the job running in this
 * address space" a single job - which is what a fault has to be pinned
 * on.
 *
 * How a job ends.  Done: the chain finished.  Failed: the slot says why
 * (JS_STATUS), and if the MMU faulted in the job's window, the fault is
 * the job's too - but the two halves come on two lines, in either order,
 * and the job is over only when it has both (mali_mmu.c, and stage 11.5
 * on the board: the fault, then the switch-off of the window fails the
 * job).  Not over within a second: a hard stop, which fails it
 * (TERMINATED); not over a second after that: the GPU is reset
 * (mali_gpu_recover()), and every running job fails with it.  A client
 * that gives a chain without an end loses its job, and nobody else
 * anything.
 *
 * Caches: before a job, the buffers it uses that a processor touched are
 * cleaned; after it, cleaned and invalidated (mali_bo.c).  The GPU's own caches are
 * cleaned and invalidated at both ends of every job by the slot's
 * configuration, as panfrost does.
 */

#include <minix/drivers.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <stdint.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"

static TAILQ_HEAD(, mali_job) queue = TAILQ_HEAD_INITIALIZER(queue);
static struct mali_job *running[MALI_NR_SLOTS];
static unsigned slot_seq[MALI_NR_SLOTS];
static minix_timer_t slot_timer[MALI_NR_SLOTS];
static int halted;			/* reset or shutdown under way */

static void slot_alarm(int slot);

static const struct mali_regs *
gpu(void)
{
	return &mali.info.gpu;
}

void
mali_sched_init(void)
{
	int s;

	for (s = 0; s < MALI_NR_SLOTS; s++)
		init_timer(&slot_timer[s]);
	/* Every slot's done and failed bits, from now on. */
	mali_wr(gpu(), JOB_INT_CLEAR, JOB_INT_ALL_SLOTS);
	mali_wr(gpu(), JOB_INT_MASK, JOB_INT_ALL_SLOTS);
}

void
mali_job_submit(struct mali_job *j)
{
	unsigned i;

	j->state = MALI_JOB_QUEUED;
	j->as = -1;
	j->result = OK;
	j->js_status = 0;
	j->fault = 0;
	j->await_mmu = 0;
	j->stopping = 0;
	for (i = 0; i < j->nbos; i++)
		j->bos[i]->mem->busy++;
	TAILQ_INSERT_TAIL(&queue, j, link);
	mali_sched_kick();
}

/* The fault in the job's window is the job's: take it from its vm. */
static void
take_fault(struct mali_job *j)
{
	if (j->fault || !j->vm->faulted)
		return;
	j->fault = 1;
	j->fault_status = j->vm->fault_status;
	j->fault_addr = j->vm->fault_addr;
}

/*
 * The job is over: out of its slot, its window and the queue; the
 * processor's caches of its buffers made right; its owner told.
 */
static void
complete(struct mali_job *j, int result)
{
	unsigned i;
	int ran = j->state == MALI_JOB_RUNNING;

	if (ran) {
		cancel_timer(&slot_timer[j->slot]);
		running[j->slot] = NULL;
		take_fault(j);
		mali_as_put(j->as);
	}
	j->result = result;
	j->state = MALI_JOB_DONE;
	TAILQ_REMOVE(&queue, j, link);
	for (i = 0; i < j->nbos; i++) {
		if (ran)
			mali_bo_for_cpu(j->bos[i]);
		j->bos[i]->mem->busy--;
	}
	j->done(j);
	mali_sched_kick();
}

static void
start(struct mali_job *j, int as)
{
	unsigned i;
	int s = j->slot;

	for (i = 0; i < j->nbos; i++)
		mali_bo_for_gpu(j->bos[i]);

	if (mali_rd(gpu(), JS_COMMAND_NEXT(s)) != 0)
		log_warn(&mali_log, "slot %d has a job queued already\n", s);

	j->vm->faulted = 0;
	j->as = as;

	/*
	 * The slot's configuration, panfrost's: the window, medium thread
	 * priority, the GPU's caches cleaned and invalidated before and
	 * after, flush reduction (with the flush id it needs), and the
	 * chain flag, which alternates so that the slot can tell the end of
	 * one chain from the next.  The board's first job ran with 0x7b00.
	 */
	j->config = (uint32_t)as | JS_CONFIG_THREAD_PRI(8) |
	    JS_CONFIG_START_FLUSH_CLEAN_INV | JS_CONFIG_END_FLUSH_CLEAN_INV |
	    JS_CONFIG_ENABLE_FLUSH_REDUCTION |
	    ((++slot_seq[s] & 1) ? JS_CONFIG_JOB_CHAIN_FLAG : 0);

	mali_wr(gpu(), JOB_INT_CLEAR, JOB_INT_DONE(s) | JOB_INT_FAILED(s));
	mali_wr(gpu(), JOB_INT_MASK, JOB_INT_ALL_SLOTS);
	mali_wr(gpu(), JS_HEAD_NEXT_LO(s), (uint32_t)j->jc);
	mali_wr(gpu(), JS_HEAD_NEXT_HI(s), (uint32_t)(j->jc >> 32));
	mali_wr(gpu(), JS_AFFINITY_NEXT_LO(s), mali.shader_present);
	mali_wr(gpu(), JS_AFFINITY_NEXT_HI(s), 0);
	mali_wr(gpu(), JS_CONFIG_NEXT(s), j->config);
	mali_wr(gpu(), JS_FLUSH_ID_NEXT(s),
	    mali_rd(gpu(), GPU_LATEST_FLUSH_ID));

	j->state = MALI_JOB_RUNNING;
	running[s] = j;
	read_frclock_64(&j->started);
	mali_wr(gpu(), JS_COMMAND_NEXT(s), JS_COMMAND_START);

	set_timer(&slot_timer[s], sys_hz(), slot_alarm, s);
}

/*
 * Start whatever can start: in the order given, the first waiting job of
 * each address space whose earlier jobs are all over, if its slot is free
 * and a window can be had.
 */
void
mali_sched_kick(void)
{
	struct mali_vm *seen[64];
	struct mali_job *j, *next;
	unsigned nseen = 0, k;
	int as;

	if (halted)
		return;
	for (j = TAILQ_FIRST(&queue); j != NULL; j = next) {
		next = TAILQ_NEXT(j, link);
		for (k = 0; k < nseen && seen[k] != j->vm; k++)
			;
		if (k < nseen)
			continue;	/* an earlier job of its own is not over */
		if (nseen == sizeof(seen) / sizeof(seen[0]))
			break;
		seen[nseen++] = j->vm;
		if (j->state != MALI_JOB_QUEUED || running[j->slot] != NULL)
			continue;
		if ((as = mali_as_get(j->vm)) == -EBUSY)
			continue;	/* every window is busy: it waits */
		if (as < 0) {
			complete(j, EIO);
			return;		/* complete() kicked again */
		}
		start(j, as);
	}
}

/* The job line: s is JOB_INT_STAT. */
void
mali_sched_job_irq(uint32_t s)
{
	struct mali_job *j;
	int slot;

	for (slot = 0; slot < MALI_NR_SLOTS; slot++) {
		if (!(s & (JOB_INT_DONE(slot) | JOB_INT_FAILED(slot))))
			continue;
		if ((j = running[slot]) == NULL || j->await_mmu) {
			log_info(&mali_log, "line %d (job): status 0x%08x, "
			    "nothing running on slot %d\n",
			    mali.info.irq[MALI_IRQ_JOB], s, slot);
			continue;
		}
		if (!(s & JOB_INT_FAILED(slot))) {
			complete(j, OK);
			continue;
		}
		j->js_status = mali_rd(gpu(), JS_STATUS(slot));
		/*
		 * Failed.  If its window faulted, the fault is its half too:
		 * in already, or still on the MMU line (raised and not yet
		 * taken - the lines are taken gpu, job, mmu), in which case
		 * the job waits for it.
		 */
		if (j->vm->faulted)
			complete(j, EIO);
		else if (j->as >= 0 && (mali_rd(gpu(), MMU_INT_RAWSTAT) &
		    MMU_INT_AS(j->as)))
			j->await_mmu = 1;
		else
			complete(j, j->stopping ? ETIMEDOUT : EIO);
	}
}

/*
 * The MMU switched window as off after a fault in vm (mali_mmu.c).  The
 * job running in it, if its failure is in already, is over now; if not,
 * its failure will come (the switch-off is what fails it).
 */
void
mali_sched_mmu_fault(int as, struct mali_vm *vm)
{
	struct mali_job *j;
	int s;

	for (s = 0; s < MALI_NR_SLOTS; s++) {
		j = running[s];
		if (j != NULL && j->vm == vm && j->as == as && j->await_mmu) {
			complete(j, EIO);
			return;
		}
	}
}

/* A second ran out on slot: stop the job, or reset the GPU under it. */
static void
slot_alarm(int slot)
{
	struct mali_job *j = running[slot];

	if (j == NULL)
		return;
	if (j->await_mmu) {
		log_warn(&mali_log, "%s: the job failed on slot %d (status "
		    "0x%02x) and its window faulted, but the fault never came "
		    "(mmu rawstat 0x%08x)\n", j->vm->name, slot, j->js_status,
		    mali_rd(gpu(), MMU_INT_RAWSTAT));
		complete(j, EIO);
		return;
	}
	if (!j->stopping) {
		log_warn(&mali_log, "%s: no job interrupt within a second: job "
		    "rawstat 0x%08x, slot %d status 0x%x head 0x%08x, mmu "
		    "rawstat 0x%08x, address space %d status 0x%x fault "
		    "0x%08x; stopping it\n", j->vm->name,
		    mali_rd(gpu(), JOB_INT_RAWSTAT), slot,
		    mali_rd(gpu(), JS_STATUS(slot)),
		    mali_rd(gpu(), JS_HEAD_LO(slot)),
		    mali_rd(gpu(), MMU_INT_RAWSTAT), j->as,
		    j->as >= 0 ? mali_rd(gpu(), AS_STATUS(j->as)) : 0,
		    j->as >= 0 ? mali_rd(gpu(), AS_FAULTSTATUS(j->as)) : 0);
		j->stopping = 1;
		mali_wr(gpu(), JS_COMMAND(slot), JS_COMMAND_HARD_STOP);
		set_timer(&slot_timer[slot], sys_hz(), slot_alarm, slot);
		return;
	}
	log_warn(&mali_log, "%s: slot %d does not stop (status 0x%x); "
	    "resetting the GPU\n", j->vm->name, slot,
	    mali_rd(gpu(), JS_STATUS(slot)));
	(void)mali_gpu_recover();
}

/*
 * The jobs of vm that have not started are not going to: they end now,
 * cancelled.  One that runs runs to its end.
 */
void
mali_sched_cancel(struct mali_vm *vm)
{
	struct mali_job *j, *next;

	halted++;
	for (j = TAILQ_FIRST(&queue); j != NULL; j = next) {
		next = TAILQ_NEXT(j, link);
		if (j->vm == vm && j->state == MALI_JOB_QUEUED)
			complete(j, ECANCELED);
	}
	halted--;
	mali_sched_kick();
}

/*
 * The GPU is being reset, or the service is going: every running job
 * fails with result; with ECANCELED the waiting ones go too, otherwise
 * they wait for the GPU to come back (mali_sched_kick()).
 */
void
mali_sched_fail_all(int result)
{
	struct mali_job *j, *next;

	halted++;
	for (j = TAILQ_FIRST(&queue); j != NULL; j = next) {
		next = TAILQ_NEXT(j, link);
		if (j->state == MALI_JOB_RUNNING || result == ECANCELED)
			complete(j, result);
	}
	halted--;
}
