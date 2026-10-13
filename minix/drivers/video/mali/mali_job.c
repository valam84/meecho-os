/*
 * The self-test at start: the GPU's first jobs, run the way a client's
 * are, checked by the processor.  Stage 11.5, on the parts of 11.6.
 *
 * What runs is the smallest job there is - WRITE_VALUE, which stores a
 * number at an address - sixty-four of them in one chain, each filling
 * its own word of a buffer, checked word by word by the processor
 * afterwards.  It needs no shader and no tiler, and it proves everything
 * a shader job will stand on: the page-table format, the address space,
 * the descriptor layout, the slot, the job interrupt line, and the cache
 * hand-over between a non-coherent GPU and the processor.  Since 11.6 it
 * proves them through the very code a client's jobs go through: an
 * address space of its own (mali_mmu.c), a buffer (mali_bo.c), the queue
 * (mali_sched.c).
 *
 * "faulttest=1" adds the one thing a good chain cannot prove, the MMU's
 * own interrupt line: after the chain, one job that writes where nothing
 * is mapped.  The fault is taken as any client's is - the window switched
 * off, which fails the job stalled on it - and then the chain runs once
 * more in the same address space, so that the GPU is shown whole after
 * its fault, not only reported.
 *
 * Every number here was seen working on this very part first: the same
 * chain through the vendor system's panfrost, from Python
 * (port/cb2-gpu/jobprobe.py and ref-jobs.txt), and the same fault
 * (ref-fault.txt).  The descriptor layout is Mesa's genxml (MIT).
 */

#include <minix/drivers.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"

#define NJOBS		64
#define PAGE		MALI_PAGE
#define NBUF		2		/* descriptors, and the buffer they fill */
#define CANARY		0xdeadbeefu	/* the word after the last one written */
#define VALUE(i)	(0x5a000000u | (uint32_t)(i))

/*
 * The fault test's write: 64 KiB past the buffer, inside the same table
 * of the last level (its entries 0 and 1 are the buffer), so the walk
 * gets to the last level and finds nothing there.  Not at the start of a
 * page, to see whether the whole address is reported.  jobprobe.py
 * writes to the same place.
 */
#define FAULT_OFF	0x10040u
#define FAULT_VALUE	0xbadc0de5u

/*
 * What the board said for the same job under panfrost (ref-fault.txt):
 * TRANSLATION_FAULT_3 on a write by unit 0x7c10, at the very address
 * written; the job failed as JOB_BUS_FAULT, its header untouched (the
 * address space was off by then).
 */
#define REF_FAULTSTATUS	0x7c1003c3u
#define REF_JS_STATUS	0x48u

/* What is on the slot. */
#define RUN_CHAIN	0		/* the chain */
#define RUN_FAULT	1		/* the job that faults */
#define RUN_AFTER	2		/* the chain again, after the fault */

static struct {
	struct mali_vm vm;
	int vm_up;
	struct mali_bo *bo;
	struct mali_bo *bos[1];
	struct mali_job job;
	int busy;			/* the job is given and not over */
	int run;			/* RUN_*: which one */
	int faulttest;
	uint64_t fault_va;
	/* the fault, as the job brought it */
	uint32_t fault_status, fault_js;
	uint64_t fault_addr;
	int fault_seen;
} st;

#define DESC		((char *)st.bo->cpu)
#define TARGET		((uint32_t *)((char *)st.bo->cpu + PAGE))
#define JOB_VA		(st.bo->va)

/* One WRITE_VALUE descriptor: store value at addr, then go to next. */
static void
write_value_job(unsigned i, uint64_t next, uint64_t addr, uint32_t value)
{
	char *d = DESC + i * JOB_DESC_SIZE;

	memset(d, 0, JOB_DESC_SIZE);
	*(uint32_t *)(d + JOB_HDR_CONTROL) = JOB_CTL_64BIT |
	    JOB_CTL_TYPE(JOB_TYPE_WRITE_VALUE) | JOB_CTL_INDEX(i + 1);
	*(uint64_t *)(d + JOB_HDR_NEXT) = next;
	*(uint64_t *)(d + JOB_WV_ADDRESS) = addr;
	*(uint32_t *)(d + JOB_WV_TYPE) = JOB_WV_IMMEDIATE32;
	*(uint64_t *)(d + JOB_WV_VALUE) = value;
}

static void job_done(struct mali_job *j);

/* The chain (or the fault job) at the buffer's start goes on slot 1. */
static void
give(int run)
{
	memset(&st.job, 0, sizeof(st.job));
	st.job.vm = &st.vm;
	st.job.jc = JOB_VA;
	st.job.slot = 1;		/* where panfrost puts non-fragment jobs */
	st.job.bos = st.bos;
	st.job.nbos = 1;
	st.job.done = job_done;
	st.run = run;
	st.busy = 1;
	st.vm.expect_fault = run == RUN_FAULT;
	mali_job_submit(&st.job);
}

/* After a start: the window the chain was given, against panfrost's. */
static void
report_window(void)
{
	int as = st.job.as;

	if (st.job.state != MALI_JOB_RUNNING || as < 0)
		return;
	log_info(&mali_log, "address space %d: tables at 0x%lx, %d pages at "
	    "GPU address 0x%llx; TRANSTAB 0x%08x%08x (reference ...007) "
	    "MEMATTR 0x%08x (reference 0x%08x) TRANSCFG 0x%08x (reference "
	    "0)\n", as, (unsigned long)mali_vm_root(&st.vm), NBUF,
	    (unsigned long long)JOB_VA,
	    mali_rd(&mali.info.gpu, AS_TRANSTAB_HI(as)),
	    mali_rd(&mali.info.gpu, AS_TRANSTAB_LO(as)),
	    mali_rd(&mali.info.gpu, AS_MEMATTR_LO(as)), MALI_MEMATTR,
	    mali_rd(&mali.info.gpu, AS_TRANSCFG_LO(as)));
}

static void
chain_start(int run)
{
	unsigned i;

	for (i = 0; i < NJOBS; i++)
		write_value_job(i, i + 1 < NJOBS ?
		    JOB_VA + (i + 1) * JOB_DESC_SIZE : 0,
		    JOB_VA + PAGE + 4 * i, VALUE(i));
	memset(TARGET, 0, PAGE);
	TARGET[NJOBS] = CANARY;

	give(run);
	if (run == RUN_CHAIN)
		report_window();
	else if (st.job.state == MALI_JOB_RUNNING)
		log_info(&mali_log, "fault test: address space %d back on\n",
		    st.job.as);
	log_info(&mali_log, "a chain of %d WRITE_VALUE jobs started on slot "
	    "%d (config 0x%x)%s\n", NJOBS, st.job.slot, st.job.config,
	    run == RUN_AFTER ? ", after the fault" : "");
}

int
mali_job_setup(void)
{
	int r;

	if ((r = mali_vm_init(&st.vm, "the self-test")) != OK) {
		log_warn(&mali_log, "out of memory for the GPU's page "
		    "tables\n");
		return r;
	}
	st.vm_up = 1;
	/* Code, as panfrost's first buffer was: that puts it at 32 MiB. */
	if ((st.bo = mali_bo_new(&st.vm, NBUF * PAGE, 0, &r)) == NULL) {
		log_warn(&mali_log, "no buffer for the job test: %d\n", r);
		return r;
	}
	st.bos[0] = st.bo;
	/* This service writes the descriptors and reads the result. */
	st.bo->cpu_access = 1;
	st.fault_va = JOB_VA + FAULT_OFF;
	return OK;
}

int
mali_job_start(int faulttest)
{
	if (st.bo == NULL)
		return EINVAL;
	if (st.busy)
		return EBUSY;
	st.faulttest = faulttest;
	chain_start(RUN_CHAIN);
	return OK;
}

/* The fault test's job: one WRITE_VALUE where nothing is mapped. */
static void
fault_start(void)
{
	write_value_job(0, 0, st.fault_va, FAULT_VALUE);
	st.fault_seen = 0;
	give(RUN_FAULT);
	log_info(&mali_log, "fault test: one WRITE_VALUE job to GPU address "
	    "0x%08llx, where nothing is mapped, started on slot %d (config "
	    "0x%x)\n", (unsigned long long)st.fault_va, st.job.slot,
	    st.job.config);
}

/* What the chain left in memory, checked by the processor: 1 if right. */
static int
check(void)
{
	unsigned i, wrong = 0, first = NJOBS, notdone = 0;
	uint32_t st0, stn;
	u64_t now;
	char where[64];
	int ok;

	read_frclock_64(&now);
	for (i = 0; i < NJOBS; i++)
		if (TARGET[i] != VALUE(i)) {
			if (wrong++ == 0)
				first = i;
		}
	for (i = 0; i < NJOBS; i++)
		if (*(uint32_t *)(DESC + i * JOB_DESC_SIZE + JOB_HDR_STATUS) !=
		    JOB_STATUS_DONE)
			notdone++;
	st0 = *(uint32_t *)(DESC + JOB_HDR_STATUS);
	stn = *(uint32_t *)(DESC + (NJOBS - 1) * JOB_DESC_SIZE +
	    JOB_HDR_STATUS);

	where[0] = '\0';
	if (wrong)
		snprintf(where, sizeof(where), " (first at %u: 0x%08x, want "
		    "0x%08x)", first, TARGET[first], VALUE(first));
	log_info(&mali_log, "chain of %d: done in %u us; %u wrong word(s)%s, "
	    "canary 0x%08x%s, job status first 0x%x last 0x%x, %u not done\n",
	    NJOBS, frclock_64_to_micros(delta_frclock_64(st.job.started, now)),
	    wrong, where, TARGET[NJOBS],
	    TARGET[NJOBS] == CANARY ? "" : " OVERWRITTEN", st0, stn, notdone);

	ok = wrong == 0 && TARGET[NJOBS] == CANARY && notdone == 0;
	if (ok)
		log_info(&mali_log, "the GPU filled the buffer through its own "
		    "MMU: every word checked by the processor\n");
	else
		log_warn(&mali_log, "the chain did not do what it was told\n");
	log_info(&mali_log, "slot %d config 0x%08x (reference 0x00007b00), "
	    "status 0x%x, address space status 0x%x\n", st.job.slot,
	    mali_rd(&mali.info.gpu, JS_CONFIG(st.job.slot)),
	    mali_rd(&mali.info.gpu, JS_STATUS(st.job.slot)),
	    st.job.as >= 0 ? mali_rd(&mali.info.gpu, AS_STATUS(st.job.as)) :
	    0);
	return ok;
}

/* The fault as the test asked for it: a write, translated to nothing. */
static int
fault_as_asked(void)
{
	return st.fault_seen && st.fault_addr == st.fault_va &&
	    MALI_EXC_TRANSLATION(AS_FAULT_EXCEPTION(st.fault_status)) &&
	    AS_FAULT_ACCESS(st.fault_status) == AS_ACCESS_WRITE;
}

/*
 * The fault job is over, with both halves of its fault in - the MMU's
 * report and the job's failure, in whichever order the lines brought
 * them (mali_sched.c).  The window it ran in is free again, and the
 * chain runs once more: a GPU that fills the buffer right after a fault
 * came through it whole.
 */
static void
fault_over(struct mali_job *j)
{
	uint32_t hdr;

	if (j->fault) {
		st.fault_seen = 1;
		st.fault_status = j->fault_status;
		st.fault_addr = j->fault_addr;
	}
	st.fault_js = j->js_status;
	if (j->result == OK) {
		log_warn(&mali_log, "line %d (job): the fault job finished - "
		    "its write to 0x%08llx, where nothing is mapped, did not "
		    "fault\n", mali.info.irq[MALI_IRQ_JOB],
		    (unsigned long long)st.fault_va);
		return;
	}
	log_info(&mali_log, "line %d (job): the fault job failed, slot "
	    "status 0x%02x, at job 0x%08llx\n", mali.info.irq[MALI_IRQ_JOB],
	    st.fault_js, (unsigned long long)j->jc);

	/* As the GPU left it: the scheduler invalidated the buffer. */
	hdr = *(uint32_t *)(DESC + JOB_HDR_STATUS);
	log_info(&mali_log, "fault test: %s; fault status 0x%08x (reference "
	    "0x%08x)%s, slot status 0x%02x (reference 0x%02x)%s, header status "
	    "0x%x (reference 0)%s; now GPU_STATUS 0x%x, mmu rawstat 0x%08x "
	    "mask 0x%08x\n",
	    fault_as_asked() ? "the fault asked for" : "NOT the fault asked "
	    "for", st.fault_status, REF_FAULTSTATUS,
	    st.fault_status != REF_FAULTSTATUS ? " DIFFERS" : "",
	    st.fault_js, REF_JS_STATUS,
	    st.fault_js != REF_JS_STATUS ? " DIFFERS" : "",
	    hdr, hdr != 0 ? " DIFFERS" : "",
	    mali_rd(&mali.info.gpu, GPU_STATUS),
	    mali_rd(&mali.info.gpu, MMU_INT_RAWSTAT),
	    mali_rd(&mali.info.gpu, MMU_INT_MASK));

	chain_start(RUN_AFTER);
}

static void
job_done(struct mali_job *j)
{
	int ok;

	st.busy = 0;
	st.vm.expect_fault = 0;
	if (st.run == RUN_FAULT) {
		fault_over(j);
		return;
	}
	if (j->result != OK) {
		log_warn(&mali_log, "line %d (job): the chain failed: %d, slot "
		    "status 0x%x, at job 0x%08llx%s\n",
		    mali.info.irq[MALI_IRQ_JOB], j->result, j->js_status,
		    (unsigned long long)j->jc,
		    j->fault ? " - its address space faulted" : "");
		return;
	}
	log_info(&mali_log, "line %d (job): slot %d done - the line works\n",
	    mali.info.irq[MALI_IRQ_JOB], j->slot);
	ok = check();
	if (st.run == RUN_CHAIN && st.faulttest) {
		if (ok)
			fault_start();
		else
			log_warn(&mali_log, "fault test not started: the "
			    "chain before it is wrong\n");
	} else if (st.run == RUN_AFTER) {
		if (ok && fault_as_asked())
			log_info(&mali_log, "fault test passed: line %d (mmu) "
			    "reported the fault, the address space came back, "
			    "the chain after it is clean\n",
			    mali.info.irq[MALI_IRQ_MMU]);
		else
			log_warn(&mali_log, "fault test FAILED\n");
	}
}

/*
 * Going down: nothing may be left that points the GPU at this memory.
 * The caller has stopped every job; the buffer leaves the GPU's tables,
 * the address space its window, and only then does the memory go back.
 */
void
mali_job_teardown(void)
{
	if (st.bo != NULL) {
		mali_bo_unref(st.bo);
		st.bo = NULL;
	}
	if (st.vm_up) {
		mali_vm_fini(&st.vm);
		st.vm_up = 0;
	}
}
