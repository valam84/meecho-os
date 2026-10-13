/*
 * The GPU's own MMU and its first jobs: the second half of stage 11.5.
 *
 * A Mali reaches memory only through its MMU, so before the GPU can do
 * anything at all it needs an address space: page tables in memory, and
 * the address of their root in one of its eight address-space windows.
 * Then a job is a descriptor in that address space and the address of the
 * descriptor in a job slot.  What runs here is the smallest job there is -
 * WRITE_VALUE, which stores a number at an address - sixty-four of them in
 * one chain, each filling its own word of a buffer, checked word by word
 * by the processor afterwards.  It needs no shader and no tiler, and it
 * proves everything a shader job will stand on: the page-table format, the
 * address space, the descriptor layout, the slot, the job interrupt line,
 * and the cache hand-over between a non-coherent GPU and the processor.
 *
 * "faulttest=1" adds the one thing a good chain cannot prove, the MMU's
 * own interrupt line: after the chain, one job that writes where nothing
 * is mapped.  The fault is taken the way panfrost takes one it cannot
 * resolve - the address space switched off, which fails the job stalled
 * on it - and then the address space comes back and the chain runs again,
 * so that the GPU is shown whole after its fault, not only reported.
 *
 * Every number here was seen working on this very part first: the same
 * chain through the vendor system's panfrost, from Python
 * (port/cb2-gpu/jobprobe.py and ref-jobs.txt), together with what panfrost
 * puts into the address space; the fault too (ref-fault.txt).  The
 * page-table format is Linux's io-pgtable "ARM_MALI_LPAE" (GPL; the bits,
 * not the code), the descriptor Mesa's genxml (MIT).
 *
 * The cache, because the GPU is not coherent (COHERENCY_FEATURES 0): the
 * kernel maps all RAM cacheable, so what the processor writes - tables,
 * descriptors - is cleaned to memory before the GPU is told about it, and
 * what the GPU writes is invalidated in the processor's cache before it is
 * read.  The GPU's own L2 is cleaned and invalidated at the start and the
 * end of every job by the slot's configuration, as panfrost does.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"

#define AS_JOBS		0		/* the address space the jobs run in */
#define JS_JOBS		1		/* the slot panfrost gives non-fragment jobs */
#define JOB_VA		0x02000000u	/* where panfrost puts its first buffer */
#define NJOBS		64
#define PAGE		4096
#define NPT		4		/* one table per level, L0..L3 */
#define NBUF		2		/* descriptors, and the buffer they fill */
#define CANARY		0xdeadbeefu	/* the word after the last one written */
#define VALUE(i)	(0x5a000000u | (uint32_t)(i))

/*
 * The fault test's write: entry 16 of the last-level table, whose entries
 * 0 and 1 are the two pages above, so the walk gets to the last level and
 * finds nothing there.  Not at the start of a page, to see whether the
 * whole address is reported.  jobprobe.py writes to the same place.
 */
#define FAULT_VA	(JOB_VA + 0x10040u)
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
	void *mem;			/* NPT + NBUF pages, contiguous */
	phys_bytes phys;
	int as_on;			/* the address space is programmed */
	int busy;			/* a job is on the slot */
	int run;			/* RUN_*: which one */
	int faulttest;			/* the fault test was asked for */
	u64_t started;
	unsigned seq;			/* chains started, for the chain flag */

	/* The fault, in two halves that may come in either order. */
	int faulted;			/* the MMU line reported it */
	int failed;			/* the job line reported the job failed */
	uint32_t fault_irq, fault_status, fault_js;
	uint64_t fault_addr;
} mj;

#define MEM_SIZE	((NPT + NBUF) * PAGE)
#define PT(l)		((uint64_t *)((char *)mj.mem + (l) * PAGE))
#define PT_PHYS(l)	(mj.phys + (l) * PAGE)
#define DESC		((char *)mj.mem + NPT * PAGE)
#define TARGET		((uint32_t *)((char *)mj.mem + (NPT + 1) * PAGE))
#define BUF_PHYS	(mj.phys + NPT * PAGE)

static const struct mali_regs *
gpu(void)
{
	return &mali.info.gpu;
}

static void
cache(int op, void *addr, size_t len, const char *what)
{
	int r;

	if ((r = sys_cachectl(op, addr, len)) != OK)
		log_warn(&mali_log, "cache maintenance of %s failed: %d\n",
		    what, r);
}

/* Wait until the address space has no command running, up to 100 ms. */
static int
as_wait(void)
{
	unsigned spent;

	for (spent = 0; spent <= 100000; spent += 10) {
		if (!(mali_rd(gpu(), AS_STATUS(AS_JOBS)) & AS_STATUS_ACTIVE))
			return OK;
		micro_delay(10);
	}
	log_warn(&mali_log, "address space %d stays busy (status 0x%x)\n",
	    AS_JOBS, mali_rd(gpu(), AS_STATUS(AS_JOBS)));
	return ETIMEDOUT;
}

static int
as_command(uint32_t cmd)
{
	int r;

	if ((r = as_wait()) != OK)
		return r;
	mali_wr(gpu(), AS_COMMAND(AS_JOBS), cmd);
	return as_wait();
}

/*
 * Lock the whole address space and let every memory access in flight
 * finish, the MMU's caches flushed - what panfrost does before it changes
 * an address space, with the same "all of it" region (63: 2^64 bytes).
 */
static int
as_flush_all(void)
{
	int r;

	mali_wr(gpu(), AS_LOCKADDR_LO(AS_JOBS), AS_LOCKADDR_ALL);
	mali_wr(gpu(), AS_LOCKADDR_HI(AS_JOBS), 0);
	if ((r = as_command(AS_COMMAND_LOCK)) != OK)
		return r;
	return as_command(AS_COMMAND_FLUSH_MEM);
}

/*
 * The address space on, with this service's tables - at the start, and
 * again after a fault.  Panfrost's order: the address space's interrupts
 * cleared and unmasked first, then everything in flight flushed, the
 * tables, and UPDATE.  The clear is not a formality: on the board the
 * switch-off after a fault raises the fault bit again (ref-fault.txt has
 * it up under the mask), and unmasking it as it is would report the old
 * fault as a new one.  Here the MMU line's own clear already took it; this
 * one keeps that from depending on who cleared when.
 */
static int
as_enable(void)
{
	uint64_t transtab;
	int r;

	transtab = (PT_PHYS(0) & MALI_PTE_ADDR) | AS_TRANSTAB_ADRMODE_TABLE |
	    AS_TRANSTAB_READ_INNER;

	mali_wr(gpu(), MMU_INT_CLEAR, MMU_INT_AS(AS_JOBS));
	mali_wr(gpu(), MMU_INT_MASK, mali_rd(gpu(), MMU_INT_MASK) |
	    MMU_INT_AS(AS_JOBS));
	if ((r = as_flush_all()) != OK)
		return r;
	mali_wr(gpu(), AS_TRANSTAB_LO(AS_JOBS), (uint32_t)transtab);
	mali_wr(gpu(), AS_TRANSTAB_HI(AS_JOBS), (uint32_t)(transtab >> 32));
	mali_wr(gpu(), AS_MEMATTR_LO(AS_JOBS), MALI_MEMATTR);
	mali_wr(gpu(), AS_MEMATTR_HI(AS_JOBS), 0);
	return as_command(AS_COMMAND_UPDATE);
}

/*
 * The address space off: no tables, so nothing in it translates any more.
 * What panfrost does going down and on a fault it cannot resolve - a job
 * stalled on the fault fails with it.  Its interrupts stay masked until
 * as_enable(), since an address space without tables can only fault.  A
 * flush that does not finish is not a reason to leave the tables in.
 */
static void
as_disable(void)
{
	mali_wr(gpu(), MMU_INT_MASK, mali_rd(gpu(), MMU_INT_MASK) &
	    ~MMU_INT_AS(AS_JOBS));
	(void)as_flush_all();
	mali_wr(gpu(), AS_TRANSTAB_LO(AS_JOBS), 0);
	mali_wr(gpu(), AS_TRANSTAB_HI(AS_JOBS), 0);
	mali_wr(gpu(), AS_MEMATTR_LO(AS_JOBS), 0);
	mali_wr(gpu(), AS_MEMATTR_HI(AS_JOBS), 0);
	(void)as_command(AS_COMMAND_UPDATE);
}

/*
 * One 4 KiB page of GPU address space.  One table per level is enough as
 * long as every page mapped shares the top three indices, which the two
 * pages here do (L0 0, L1 0, L2 16); a second table at some level would be
 * needed for anything wider, and mali_job_setup() checks.
 */
static void
map_page(uint64_t va, phys_bytes pa)
{
	unsigned i0 = (va >> 39) & 0x1ff, i1 = (va >> 30) & 0x1ff;
	unsigned i2 = (va >> 21) & 0x1ff, i3 = (va >> 12) & 0x1ff;

	PT(0)[i0] = (PT_PHYS(1) & MALI_PTE_ADDR) | MALI_PTE_TABLE;
	PT(1)[i1] = (PT_PHYS(2) & MALI_PTE_ADDR) | MALI_PTE_TABLE;
	PT(2)[i2] = (PT_PHYS(3) & MALI_PTE_ADDR) | MALI_PTE_TABLE;
	PT(3)[i3] = (pa & MALI_PTE_ADDR) | MALI_PTE_READ | MALI_PTE_WRITE |
	    MALI_PTE_SH_OUTER | MALI_PTE_LEAF;
}

int
mali_job_setup(void)
{
	int r;

	if (((JOB_VA ^ (JOB_VA + (NBUF - 1) * PAGE)) >> 21) != 0) {
		log_warn(&mali_log, "the job buffer crosses a 2 MiB boundary; "
		    "one table per level does not cover it\n");
		return EINVAL;
	}

	if ((mj.mem = alloc_contig(MEM_SIZE, AC_ALIGN4K, &mj.phys)) == NULL) {
		log_warn(&mali_log, "out of contiguous memory for the GPU's "
		    "page tables\n");
		return ENOMEM;
	}
	memset(mj.mem, 0, MEM_SIZE);

	map_page(JOB_VA, BUF_PHYS);
	map_page(JOB_VA + PAGE, BUF_PHYS + PAGE);

	/*
	 * To memory before the GPU walks them, and out of the processor's
	 * cache: a dirty zero evicted later would land on top of what the
	 * GPU wrote into the buffer.
	 */
	cache(CACHE_CLEAN_INVALIDATE, mj.mem, MEM_SIZE, "the page tables");

	if ((r = as_enable()) != OK)
		return r;
	mj.as_on = 1;

	log_info(&mali_log, "address space %d: tables at 0x%lx, %d pages at "
	    "GPU address 0x%x; TRANSTAB 0x%08x%08x (reference ...007) MEMATTR "
	    "0x%08x (reference 0x%08x) TRANSCFG 0x%08x (reference 0)\n",
	    AS_JOBS, (unsigned long)PT_PHYS(0), NBUF, JOB_VA,
	    mali_rd(gpu(), AS_TRANSTAB_HI(AS_JOBS)),
	    mali_rd(gpu(), AS_TRANSTAB_LO(AS_JOBS)),
	    mali_rd(gpu(), AS_MEMATTR_LO(AS_JOBS)), MALI_MEMATTR,
	    mali_rd(gpu(), AS_TRANSCFG_LO(AS_JOBS)));
	return OK;
}

/* One WRITE_VALUE descriptor: store value at addr, then go to next. */
static void
write_value_job(unsigned i, uint32_t next, uint32_t addr, uint32_t value)
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

/*
 * Give the slot the job chain at JOB_VA, with a second for its interrupt
 * (mali_job_alarm() if it does not come).  Returns the configuration.
 */
static uint32_t
slot_start(int run)
{
	uint32_t cfg;

	mali_wr(gpu(), JOB_INT_CLEAR, JOB_INT_ALL_SLOTS);
	mali_wr(gpu(), JOB_INT_MASK, JOB_INT_ALL_SLOTS);

	/*
	 * The slot's configuration, panfrost's: the address space, medium
	 * thread priority, the GPU's caches cleaned and invalidated before
	 * and after, flush reduction (with the flush id it needs), and the
	 * chain flag, which alternates so that the slot can tell the end of
	 * one chain from the next.  The board's first job ran with 0x7b00.
	 */
	cfg = AS_JOBS | JS_CONFIG_THREAD_PRI(8) |
	    JS_CONFIG_START_FLUSH_CLEAN_INV | JS_CONFIG_END_FLUSH_CLEAN_INV |
	    JS_CONFIG_ENABLE_FLUSH_REDUCTION |
	    ((++mj.seq & 1) ? JS_CONFIG_JOB_CHAIN_FLAG : 0);

	mali_wr(gpu(), JS_HEAD_NEXT_LO(JS_JOBS), JOB_VA);
	mali_wr(gpu(), JS_HEAD_NEXT_HI(JS_JOBS), 0);
	mali_wr(gpu(), JS_AFFINITY_NEXT_LO(JS_JOBS), mali.shader_present);
	mali_wr(gpu(), JS_AFFINITY_NEXT_HI(JS_JOBS), 0);
	mali_wr(gpu(), JS_CONFIG_NEXT(JS_JOBS), cfg);
	mali_wr(gpu(), JS_FLUSH_ID_NEXT(JS_JOBS),
	    mali_rd(gpu(), GPU_LATEST_FLUSH_ID));

	mj.run = run;
	read_frclock_64(&mj.started);
	mj.busy = 1;
	mali_wr(gpu(), JS_COMMAND_NEXT(JS_JOBS), JS_COMMAND_START);

	(void)sys_setalarm(sys_hz(), 0);
	return cfg;
}

static void
chain_start(int run)
{
	uint32_t cfg;
	unsigned i;

	for (i = 0; i < NJOBS; i++)
		write_value_job(i, i + 1 < NJOBS ?
		    JOB_VA + (i + 1) * JOB_DESC_SIZE : 0,
		    JOB_VA + PAGE + 4 * i, VALUE(i));
	memset(TARGET, 0, PAGE);
	TARGET[NJOBS] = CANARY;
	cache(CACHE_CLEAN_INVALIDATE, DESC, NBUF * PAGE, "the job buffer");

	cfg = slot_start(run);
	log_info(&mali_log, "a chain of %d WRITE_VALUE jobs started on slot "
	    "%d (config 0x%x)%s\n", NJOBS, JS_JOBS, cfg,
	    run == RUN_AFTER ? ", after the fault" : "");
}

int
mali_job_start(int faulttest)
{
	if (!mj.as_on)
		return EINVAL;
	if (mali_rd(gpu(), JS_COMMAND_NEXT(JS_JOBS)) != 0) {
		log_warn(&mali_log, "slot %d has a job queued already\n",
		    JS_JOBS);
		return EBUSY;
	}
	mj.faulttest = faulttest;
	chain_start(RUN_CHAIN);
	return OK;
}

/*
 * The fault test's job: one WRITE_VALUE to FAULT_VA.  Its descriptor is
 * mapped, so the job starts; its one write is what faults.
 */
static void
fault_start(void)
{
	uint32_t cfg;

	write_value_job(0, 0, FAULT_VA, FAULT_VALUE);
	cache(CACHE_CLEAN_INVALIDATE, DESC, JOB_DESC_SIZE, "the fault job");
	mj.faulted = mj.failed = 0;

	cfg = slot_start(RUN_FAULT);
	log_info(&mali_log, "fault test: one WRITE_VALUE job to GPU address "
	    "0x%08x, where nothing is mapped, started on slot %d (config "
	    "0x%x)\n", FAULT_VA, JS_JOBS, cfg);
}

/* What the chain left in memory, checked by the processor: 1 if right. */
static int
check(u64_t now)
{
	unsigned i, wrong = 0, first = NJOBS, notdone = 0;
	uint32_t st0, stn;
	char where[64];
	int ok;

	cache(CACHE_INVALIDATE, DESC, NBUF * PAGE, "the job buffer");

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
	    NJOBS, frclock_64_to_micros(delta_frclock_64(mj.started, now)),
	    wrong, where, TARGET[NJOBS],
	    TARGET[NJOBS] == CANARY ? "" : " OVERWRITTEN", st0, stn, notdone);

	ok = wrong == 0 && TARGET[NJOBS] == CANARY && notdone == 0;
	if (ok)
		log_info(&mali_log, "the GPU filled the buffer through its own "
		    "MMU: every word checked by the processor\n");
	else
		log_warn(&mali_log, "the chain did not do what it was told\n");
	log_info(&mali_log, "slot %d config 0x%08x (reference 0x00007b00), "
	    "status 0x%x, address space status 0x%x\n", JS_JOBS,
	    mali_rd(gpu(), JS_CONFIG(JS_JOBS)),
	    mali_rd(gpu(), JS_STATUS(JS_JOBS)),
	    mali_rd(gpu(), AS_STATUS(AS_JOBS)));
	return ok;
}

/* The fault as the test asked for it: a write, translated to nothing. */
static int
fault_as_asked(void)
{
	return mj.fault_irq == MMU_INT_PAGE_FAULT(AS_JOBS) &&
	    mj.fault_addr == FAULT_VA &&
	    MALI_EXC_TRANSLATION(AS_FAULT_EXCEPTION(mj.fault_status)) &&
	    AS_FAULT_ACCESS(mj.fault_status) == AS_ACCESS_WRITE;
}

/*
 * Both halves of the fault are in - the MMU's report and the job's
 * failure, in whichever order the lines brought them - so the address
 * space can come back (as_enable(): its interrupts cleared and unmasked,
 * its tables written again), and the chain runs once more: a GPU that
 * fills the buffer right after a fault came through it whole.
 */
static void
fault_recover(void)
{
	uint32_t hdr, st;

	cache(CACHE_INVALIDATE, DESC, JOB_DESC_SIZE, "the fault job");
	hdr = *(uint32_t *)(DESC + JOB_HDR_STATUS);
	st = mali_rd(gpu(), GPU_STATUS);
	log_info(&mali_log, "fault test: %s; fault status 0x%08x (reference "
	    "0x%08x)%s, slot status 0x%02x (reference 0x%02x)%s, header status "
	    "0x%x (reference 0)%s; now GPU_STATUS 0x%x, mmu rawstat 0x%08x "
	    "mask 0x%08x\n",
	    fault_as_asked() ? "the fault asked for" : "NOT the fault asked "
	    "for", mj.fault_status, REF_FAULTSTATUS,
	    mj.fault_status != REF_FAULTSTATUS ? " DIFFERS" : "",
	    mj.fault_js, REF_JS_STATUS,
	    mj.fault_js != REF_JS_STATUS ? " DIFFERS" : "",
	    hdr, hdr != 0 ? " DIFFERS" : "",
	    st, mali_rd(gpu(), MMU_INT_RAWSTAT), mali_rd(gpu(), MMU_INT_MASK));

	if (as_enable() != OK) {
		mj.busy = 0;
		(void)sys_setalarm(0, 0);
		log_warn(&mali_log, "fault test: the address space does not "
		    "come back\n");
		return;
	}
	log_info(&mali_log, "fault test: address space %d back on\n", AS_JOBS);
	chain_start(RUN_AFTER);
}

/* The job line while the fault test's job is on the slot. */
static void
fault_job_irq(uint32_t s)
{
	if (s & JOB_INT_FAILED(JS_JOBS)) {
		mj.failed = 1;
		mj.fault_js = mali_rd(gpu(), JS_STATUS(JS_JOBS));
		log_info(&mali_log, "line %d (job): the fault job failed, "
		    "slot status 0x%02x, at job 0x%08x%08x\n",
		    mali.info.irq[MALI_IRQ_JOB], mj.fault_js,
		    mali_rd(gpu(), JS_HEAD_HI(JS_JOBS)),
		    mali_rd(gpu(), JS_HEAD_LO(JS_JOBS)));
		if (mj.faulted)
			fault_recover();
		return;
	}
	if (s & JOB_INT_DONE(JS_JOBS)) {
		mj.busy = 0;
		(void)sys_setalarm(0, 0);
		log_warn(&mali_log, "line %d (job): the fault job finished - "
		    "its write to 0x%08x, where nothing is mapped, did not "
		    "fault\n", mali.info.irq[MALI_IRQ_JOB], FAULT_VA);
		return;
	}
	log_info(&mali_log, "line %d (job): status 0x%08x\n",
	    mali.info.irq[MALI_IRQ_JOB], s);
}

/* The job line: s is JOB_INT_STAT. */
void
mali_job_irq(uint32_t s)
{
	u64_t now;
	int ok;

	read_frclock_64(&now);
	if (!mj.busy) {
		log_info(&mali_log, "line %d (job): status 0x%08x, no chain "
		    "running\n", mali.info.irq[MALI_IRQ_JOB], s);
		return;
	}
	if (mj.run == RUN_FAULT) {
		fault_job_irq(s);
		return;
	}
	if (s & JOB_INT_FAILED(JS_JOBS)) {
		mj.busy = 0;
		(void)sys_setalarm(0, 0);
		log_warn(&mali_log, "line %d (job): the chain failed, slot "
		    "status 0x%x, at job 0x%08x%08x\n",
		    mali.info.irq[MALI_IRQ_JOB],
		    mali_rd(gpu(), JS_STATUS(JS_JOBS)),
		    mali_rd(gpu(), JS_HEAD_HI(JS_JOBS)),
		    mali_rd(gpu(), JS_HEAD_LO(JS_JOBS)));
		return;
	}
	if (s & JOB_INT_DONE(JS_JOBS)) {
		mj.busy = 0;
		(void)sys_setalarm(0, 0);
		log_info(&mali_log, "line %d (job): slot %d done - the line "
		    "works\n", mali.info.irq[MALI_IRQ_JOB], JS_JOBS);
		ok = check(now);
		if (mj.run == RUN_CHAIN && mj.faulttest) {
			if (ok)
				fault_start();
			else
				log_warn(&mali_log, "fault test not started: "
				    "the chain before it is wrong\n");
		} else if (mj.run == RUN_AFTER) {
			if (ok && fault_as_asked())
				log_info(&mali_log, "fault test passed: line "
				    "%d (mmu) reported the fault, the address "
				    "space came back, the chain after it is "
				    "clean\n", mali.info.irq[MALI_IRQ_MMU]);
			else
				log_warn(&mali_log, "fault test FAILED\n");
		}
		return;
	}
	log_info(&mali_log, "line %d (job): status 0x%08x\n",
	    mali.info.irq[MALI_IRQ_JOB], s);
}

static const char *
access_name(uint32_t fs)
{
	static const char *const names[] = {
		"atomic", "execute", "read", "write"
	};

	return names[AS_FAULT_ACCESS(fs)];
}

static const char *
exception_name(uint32_t fs)
{
	uint32_t e = AS_FAULT_EXCEPTION(fs);

	if (MALI_EXC_TRANSLATION(e))
		return "translation fault";
	if (MALI_EXC_PERMISSION(e))
		return "permission fault";
	if ((e & 0xf8) == 0xd0)
		return "table bus fault";
	if ((e & 0xf8) == 0xd8)
		return "access flag fault";
	if ((e & 0xf0) == 0xe0)
		return "address size or memory attribute fault";
	return "fault";
}

/*
 * The MMU line: s is MMU_INT_STAT.  A fault in this service's address
 * space is taken the way panfrost takes one it cannot resolve: the fault
 * cleared, the address space switched off, which fails whatever is
 * stalled on the fault.  The address space stays masked until it is
 * enabled again.
 */
void
mali_mmu_irq(uint32_t s)
{
	uint32_t fs, raw;
	uint64_t addr;
	int asked;
	char line[200];

	fs = mali_rd(gpu(), AS_FAULTSTATUS(AS_JOBS));
	addr = ((uint64_t)mali_rd(gpu(), AS_FAULTADDRESS_HI(AS_JOBS)) << 32) |
	    mali_rd(gpu(), AS_FAULTADDRESS_LO(AS_JOBS));
	asked = mj.busy && mj.run == RUN_FAULT;

	snprintf(line, sizeof(line), "line %d (mmu): status 0x%08x; address "
	    "space %d %s 0x%08x (%s 0x%02x, %s access, source 0x%x) at "
	    "0x%08x%08x\n", mali.info.irq[MALI_IRQ_MMU], s, AS_JOBS,
	    (s & MMU_INT_BUS_ERROR(AS_JOBS)) ? "bus error, fault status" :
	    "fault status", fs, exception_name(fs), AS_FAULT_EXCEPTION(fs),
	    access_name(fs), AS_FAULT_SOURCE(fs), (uint32_t)(addr >> 32),
	    (uint32_t)addr);
	if (asked)
		log_info(&mali_log, "%s", line);
	else
		log_warn(&mali_log, "%s", line);

	if (!(s & MMU_INT_AS(AS_JOBS)) || !mj.as_on)
		return;

	/*
	 * Panfrost's order: the fault cleared, then the address space off.
	 * Measured on the board (PORTING-LOG, "Линия MMU"): the clear takes,
	 * and the switch-off raises the bit again - the access that waited
	 * faults once more as the tables go - while the job fails at once
	 * (JOB_BUS_FAULT).  The caller's clear on return takes that second
	 * bit; as_enable() clears again before unmasking whatever the order.
	 */
	mali_wr(gpu(), MMU_INT_CLEAR, s & MMU_INT_AS(AS_JOBS));
	raw = mali_rd(gpu(), MMU_INT_RAWSTAT);
	as_disable();
	log_info(&mali_log, "address space %d off: mmu rawstat 0x%08x after "
	    "the clear, 0x%08x after the switch-off, mask 0x%08x; job rawstat "
	    "0x%08x, slot status 0x%02x, GPU_STATUS 0x%x\n", AS_JOBS, raw,
	    mali_rd(gpu(), MMU_INT_RAWSTAT), mali_rd(gpu(), MMU_INT_MASK),
	    mali_rd(gpu(), JOB_INT_RAWSTAT), mali_rd(gpu(), JS_STATUS(JS_JOBS)),
	    mali_rd(gpu(), GPU_STATUS));
	mj.fault_irq = s & MMU_INT_AS(AS_JOBS);
	mj.fault_status = fs;
	mj.fault_addr = addr;

	if (!asked) {
		log_warn(&mali_log, "address space %d is off after a fault "
		    "nobody asked for\n", AS_JOBS);
		return;
	}
	mj.faulted = 1;
	if (mj.failed)
		fault_recover();
}

/* The second ran out: say where everything stands, and stop the slot. */
void
mali_job_alarm(void)
{
	if (!mj.busy)
		return;
	mj.busy = 0;
	if (mj.run == RUN_FAULT) {
		log_warn(&mali_log, "fault test: not over within a second - "
		    "the mmu line %s, the job line %s; mmu rawstat 0x%08x "
		    "mask 0x%08x, job rawstat 0x%08x, slot status 0x%x, "
		    "address space status 0x%x fault 0x%08x\n",
		    mj.faulted ? "came" : "did not come",
		    mj.failed ? "came" : "did not come",
		    mali_rd(gpu(), MMU_INT_RAWSTAT), mali_rd(gpu(), MMU_INT_MASK),
		    mali_rd(gpu(), JOB_INT_RAWSTAT),
		    mali_rd(gpu(), JS_STATUS(JS_JOBS)),
		    mali_rd(gpu(), AS_STATUS(AS_JOBS)),
		    mali_rd(gpu(), AS_FAULTSTATUS(AS_JOBS)));
		if (!mj.failed)
			mali_wr(gpu(), JS_COMMAND(JS_JOBS),
			    JS_COMMAND_HARD_STOP);
		return;
	}
	log_warn(&mali_log, "no job interrupt within a second: job rawstat "
	    "0x%08x, slot status 0x%x head 0x%08x, mmu rawstat 0x%08x, "
	    "address space status 0x%x fault 0x%08x\n",
	    mali_rd(gpu(), JOB_INT_RAWSTAT), mali_rd(gpu(), JS_STATUS(JS_JOBS)),
	    mali_rd(gpu(), JS_HEAD_LO(JS_JOBS)), mali_rd(gpu(), MMU_INT_RAWSTAT),
	    mali_rd(gpu(), AS_STATUS(AS_JOBS)),
	    mali_rd(gpu(), AS_FAULTSTATUS(AS_JOBS)));
	mali_wr(gpu(), JS_COMMAND(JS_JOBS), JS_COMMAND_HARD_STOP);
}

/*
 * Going down: nothing may be left that points the GPU at this process's
 * memory, so the slot is stopped and the address space emptied before the
 * memory goes back - panfrost's order for disabling an address space.
 */
void
mali_job_teardown(void)
{
	if (mj.busy) {
		mali_wr(gpu(), JS_COMMAND(JS_JOBS), JS_COMMAND_HARD_STOP);
		mj.busy = 0;
	}
	if (mj.as_on) {
		as_disable();
		mj.as_on = 0;
	}
	if (mj.mem != NULL) {
		free_contig(mj.mem, MEM_SIZE);
		mj.mem = NULL;
	}
}
