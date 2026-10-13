/*
 * The GPU's own MMU: page tables, and the eight windows they are loaded
 * into.
 *
 * A Mali reaches memory only through its MMU.  An address space is a tree
 * of page tables in memory; the GPU has eight address-space windows
 * (AS_PRESENT 0xff), each holding the root of one tree while jobs run in
 * it, and a job names its window in the slot's configuration.  Every
 * client of this service has an address space of its own, and so has the
 * self-test, so that one client's job cannot even name another's memory:
 * the isolation is the GPU's MMU's, the way the processor's MMU keeps
 * processes apart.  Eight windows for more than eight address spaces is
 * what panfrost does too: a window goes to the address space whose job is
 * next, the least recently used idle one is taken back if none is free.
 *
 * The table format is Linux's io-pgtable "ARM_MALI_LPAE" (GPL; the bits,
 * not the code), proved on this very part in stage 11.5: four levels of
 * 512 entries over 4 KiB pages whatever the address width, a table entry
 * of type 3 and a leaf of type 1 at every level, stage-2-like permissions,
 * "outer" shareability, no access flag; XN on a buffer that is not code.
 * Tables are written by the processor and read by a GPU that does not see
 * its caches (COHERENCY_FEATURES 0), so every entry written is cleaned to
 * memory, and the MMU's own caches of an address space in a window are
 * flushed after it changes (a lock of the region and FLUSH_PT, as
 * panfrost does).
 *
 * A fault - a translation the tables do not have, a permission they do
 * not give, a bus error on the walk - is taken the way panfrost takes one
 * it cannot resolve, and the way stage 11.5 measured on the board: the
 * fault cleared, the window switched off (which fails the job stalled on
 * it, JOB_BUS_FAULT), and kept off until the job's failure has come in
 * too - the two halves may come in either order.  Then the window is
 * free again; the faulting address space gets one back, cleared and
 * unmasked first, when its next job starts.  Nothing of the fault reaches
 * any other address space.
 *
 * One fault is resolved instead: a translation fault inside a heap, which
 * is how a heap grows (mali_bo_grow()).  The access that faulted waits in
 * the MMU; the fault is cleared first, then the piece is mapped, and the
 * flush of the MMU's caches that the mapping ends with is what lets it go
 * on - kbase's order, and kbase says why: the flush restarts every stalled
 * access, one may fault at once, and a bit still up from the old fault
 * would hide the new one.  The same reason keeps the line's clear in here
 * rather than after the handler (mali.c): a clear after a flush could take
 * the next fault of the same window with the old one.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"
#include "panfrost_drm.h"

#define NENT		512

struct mali_table {
	uint64_t *e;			/* the page, as this service sees it */
	phys_bytes phys;
	struct mali_table **child;	/* the next level's; NULL at level 3 */
};

/* The windows. */
static struct {
	struct mali_vm *vm;		/* loaded, or NULL */
	unsigned users;			/* jobs on the GPU in it */
	int dead;			/* off after a fault, its job not over */
	unsigned long lru;
} win[MALI_NR_AS];
static unsigned long lru_clock;

static const struct mali_regs *
gpu(void)
{
	return &mali.info.gpu;
}

static void
cache(int op, void *addr, size_t len)
{
	int r;

	if ((r = sys_cachectl(op, addr, len)) != OK)
		log_warn(&mali_log, "cache maintenance of a page table "
		    "failed: %d\n", r);
}

static unsigned
idx(uint64_t va, int level)
{
	return (unsigned)(va >> (39 - 9 * level)) & (NENT - 1);
}

/* ---- tables ----------------------------------------------------------- */

static struct mali_table *
table_new(int level)
{
	struct mali_table *t;

	if ((t = calloc(1, sizeof(*t))) == NULL)
		return NULL;
	if (level < 3 && (t->child = calloc(NENT, sizeof(*t->child))) ==
	    NULL) {
		free(t);
		return NULL;
	}
	if ((t->e = alloc_contig(MALI_PAGE, AC_ALIGN4K, &t->phys)) == NULL) {
		free(t->child);
		free(t);
		return NULL;
	}
	/*
	 * Zeros to memory before the GPU walks the table, and out of the
	 * processor's cache, so that no dirty line of it lands later on top
	 * of an entry written and cleaned since.
	 */
	memset(t->e, 0, MALI_PAGE);
	cache(CACHE_CLEAN_INVALIDATE, t->e, MALI_PAGE);
	return t;
}

static void
table_free(struct mali_table *t, int level)
{
	unsigned i;

	if (t == NULL)
		return;
	if (level < 3)
		for (i = 0; i < NENT; i++)
			table_free(t->child[i], level + 1);
	free(t->child);
	free_contig(t->e, MALI_PAGE);
	free(t);
}

/* The last-level table that holds va, made on the way if make. */
static struct mali_table *
leaf_table(struct mali_vm *vm, uint64_t va, int make)
{
	struct mali_table *t = vm->root, *c;
	unsigned i;
	int level;

	for (level = 0; level < 3; level++) {
		i = idx(va, level);
		if ((c = t->child[i]) == NULL) {
			if (!make || (c = table_new(level + 1)) == NULL)
				return NULL;
			t->child[i] = c;
			t->e[i] = (c->phys & MALI_PTE_ADDR) | MALI_PTE_TABLE;
			cache(CACHE_CLEAN_INVALIDATE, &t->e[i], sizeof(t->e[i]));
		}
		t = c;
	}
	return t;
}

/* ---- the windows ------------------------------------------------------ */

/* Wait until the window has no command running, up to 100 ms. */
static int
as_wait(int as)
{
	unsigned spent;

	for (spent = 0; spent <= 100000; spent += 10) {
		if (!(mali_rd(gpu(), AS_STATUS(as)) & AS_STATUS_ACTIVE))
			return OK;
		micro_delay(10);
	}
	log_warn(&mali_log, "address space %d stays busy (status 0x%x)\n",
	    as, mali_rd(gpu(), AS_STATUS(as)));
	return ETIMEDOUT;
}

static int
as_command(int as, uint32_t cmd)
{
	int r;

	if ((r = as_wait(as)) != OK)
		return r;
	mali_wr(gpu(), AS_COMMAND(as), cmd);
	return as_wait(as);
}

/*
 * Lock the smallest naturally aligned power-of-two region that holds
 * [start, start + size), and run op on it: accesses to it wait meanwhile.
 * The region is its base with log2(size) - 1 in the low bits, at least
 * 32 KiB; start 0 and size ~0 make 63, "all of it".
 */
static int
as_flush(int as, uint64_t start, uint64_t size, uint32_t op)
{
	uint64_t diff;
	unsigned width;
	int r;

	diff = start ^ (start + size - 1);
	width = diff == 0 ? 0 : 64 - (unsigned)__builtin_clzll(diff);
	if (width < AS_LOCK_REGION_MIN_LOG2)
		width = AS_LOCK_REGION_MIN_LOG2;
	width--;
	start &= ~((1ull << width) - 1);

	mali_wr(gpu(), AS_LOCKADDR_LO(as), (uint32_t)start | width);
	mali_wr(gpu(), AS_LOCKADDR_HI(as), (uint32_t)(start >> 32));
	if ((r = as_command(as, AS_COMMAND_LOCK)) != OK)
		return r;
	return as_command(as, op);
}

/*
 * The window on, with vm's tables.  Panfrost's order: the window's
 * interrupts cleared and unmasked first, then everything in flight
 * flushed, the tables, and UPDATE.  The clear is not a formality: on the
 * board the switch-off after a fault raises the fault bit again, and
 * unmasking it as it is would report the old fault as a new one.
 */
static int
as_enable(int as, struct mali_vm *vm)
{
	uint64_t transtab;
	int r;

	transtab = (vm->root->phys & MALI_PTE_ADDR) |
	    AS_TRANSTAB_ADRMODE_TABLE | AS_TRANSTAB_READ_INNER;

	mali_wr(gpu(), MMU_INT_CLEAR, MMU_INT_AS(as));
	mali_wr(gpu(), MMU_INT_MASK, mali_rd(gpu(), MMU_INT_MASK) |
	    MMU_INT_AS(as));
	if ((r = as_flush(as, 0, ~0ull, AS_COMMAND_FLUSH_MEM)) != OK)
		return r;
	mali_wr(gpu(), AS_TRANSTAB_LO(as), (uint32_t)transtab);
	mali_wr(gpu(), AS_TRANSTAB_HI(as), (uint32_t)(transtab >> 32));
	mali_wr(gpu(), AS_MEMATTR_LO(as), MALI_MEMATTR);
	mali_wr(gpu(), AS_MEMATTR_HI(as), 0);
	return as_command(as, AS_COMMAND_UPDATE);
}

/*
 * The window off: no tables, so nothing in it translates any more - what
 * panfrost does going down, giving a window to another address space, and
 * on a fault it cannot resolve, where the job stalled on the fault fails
 * with it.  Masked until enabled again: a window without tables can only
 * fault.  A flush that does not finish is not a reason to leave the
 * tables in.
 */
static void
as_disable(int as)
{
	mali_wr(gpu(), MMU_INT_MASK, mali_rd(gpu(), MMU_INT_MASK) &
	    ~MMU_INT_AS(as));
	(void)as_flush(as, 0, ~0ull, AS_COMMAND_FLUSH_MEM);
	mali_wr(gpu(), AS_TRANSTAB_LO(as), 0);
	mali_wr(gpu(), AS_TRANSTAB_HI(as), 0);
	mali_wr(gpu(), AS_MEMATTR_LO(as), 0);
	mali_wr(gpu(), AS_MEMATTR_HI(as), 0);
	(void)as_command(as, AS_COMMAND_UPDATE);
}

/* ---- address spaces --------------------------------------------------- */

int
mali_vm_init(struct mali_vm *vm, const char *name)
{
	memset(vm, 0, sizeof(*vm));
	vm->as = -1;
	vm->name = name;
	TAILQ_INIT(&vm->bos);
	if ((vm->root = table_new(0)) == NULL)
		return ENOMEM;
	return OK;
}

/*
 * The address space goes: out of its window first, so that nothing on
 * the GPU points at the tables when their pages go back.  No job may be
 * running in it, and no buffer left in it.
 */
void
mali_vm_fini(struct mali_vm *vm)
{
	int as = vm->as;

	if (as >= 0) {
		as_disable(as);
		win[as].vm = NULL;
		win[as].users = 0;
		vm->as = -1;
	}
	if (!TAILQ_EMPTY(&vm->bos))
		log_warn(&mali_log, "%s goes with buffers in it\n", vm->name);
	table_free(vm->root, 0);
	vm->root = NULL;
}

phys_bytes
mali_vm_root(const struct mali_vm *vm)
{
	return vm->root->phys;
}

/*
 * Map npages pages at va.  prot is what the entries allow (MALI_PTE_READ,
 * _WRITE, _XN); the leaf type and the shareability are the format's.
 */
int
mali_vm_map(struct mali_vm *vm, uint64_t va, const phys_bytes *pages,
	size_t npages, uint64_t prot)
{
	struct mali_table *t;
	size_t i = 0, k, run;
	unsigned first;
	uint64_t a;

	while (i < npages) {
		a = va + i * MALI_PAGE;
		if ((t = leaf_table(vm, a, 1)) == NULL) {
			mali_vm_unmap(vm, va, i);
			return ENOMEM;
		}
		first = idx(a, 3);
		run = NENT - first;
		if (run > npages - i)
			run = npages - i;
		for (k = 0; k < run; k++)
			t->e[first + k] = (pages[i + k] & MALI_PTE_ADDR) |
			    prot | MALI_PTE_SH_OUTER | MALI_PTE_LEAF;
		cache(CACHE_CLEAN_INVALIDATE, &t->e[first],
		    run * sizeof(t->e[0]));
		i += run;
	}
	if (vm->as >= 0)
		(void)as_flush(vm->as, va, npages * MALI_PAGE,
		    AS_COMMAND_FLUSH_PT);
	return OK;
}

/*
 * Unmap npages pages at va.  When this returns, the GPU cannot reach the
 * pages through vm: the entries are zero in memory, and the MMU's caches
 * of them are flushed if vm is in a window (a window it is not in was
 * flushed when it lost it, and is flushed again when it gets one).
 */
void
mali_vm_unmap(struct mali_vm *vm, uint64_t va, size_t npages)
{
	struct mali_table *t;
	size_t i = 0, run;
	unsigned first;
	uint64_t a;

	while (i < npages) {
		a = va + i * MALI_PAGE;
		first = idx(a, 3);
		run = NENT - first;
		if (run > npages - i)
			run = npages - i;
		if ((t = leaf_table(vm, a, 0)) != NULL) {
			memset(&t->e[first], 0, run * sizeof(t->e[0]));
			cache(CACHE_CLEAN_INVALIDATE, &t->e[first],
			    run * sizeof(t->e[0]));
		}
		i += run;
	}
	if (vm->as >= 0 && npages > 0)
		(void)as_flush(vm->as, va, npages * MALI_PAGE,
		    AS_COMMAND_FLUSH_PT);
}

/*
 * A window for vm's next job: the one it is in, a free one, or the least
 * recently used one nobody's job is in.  Returns the window, -EBUSY if
 * every window is busy (the job waits), or -EIO if the window would not
 * take the tables.
 */
int
mali_as_get(struct mali_vm *vm)
{
	int i, pick = -1;

	if (vm->as >= 0) {
		win[vm->as].users++;
		win[vm->as].lru = ++lru_clock;
		return vm->as;
	}
	for (i = 0; i < MALI_NR_AS; i++) {
		if (!(mali.as_present & (1u << i)) || win[i].dead)
			continue;
		if (win[i].vm == NULL) {
			pick = i;
			break;
		}
		if (win[i].users == 0 && (pick < 0 || win[i].lru <
		    win[pick].lru))
			pick = i;
	}
	if (pick < 0)
		return -EBUSY;

	if (win[pick].vm != NULL) {
		as_disable(pick);
		win[pick].vm->as = -1;
		win[pick].vm = NULL;
	}
	if (as_enable(pick, vm) != OK) {
		log_warn(&mali_log, "address space %d does not take the "
		    "tables of %s\n", pick, vm->name);
		as_disable(pick);
		return -EIO;
	}
	win[pick].vm = vm;
	win[pick].users = 1;
	win[pick].lru = ++lru_clock;
	vm->as = pick;
	log_debug(&mali_log, "address space %d: %s, tables at 0x%lx\n", pick,
	    vm->name, (unsigned long)vm->root->phys);
	return pick;
}

/* A job that ran in window as is over. */
void
mali_as_put(int as)
{
	if (as < 0 || as >= MALI_NR_AS || win[as].users == 0)
		return;
	if (--win[as].users == 0 && win[as].dead) {
		win[as].dead = 0;
		win[as].vm = NULL;
	}
}

/* After a reset of the GPU every window is off, and none holds anything. */
void
mali_mmu_reset(void)
{
	int i;

	for (i = 0; i < MALI_NR_AS; i++) {
		if (win[i].vm != NULL)
			win[i].vm->as = -1;
		win[i].vm = NULL;
		win[i].users = 0;
		win[i].dead = 0;
	}
}

/* ---- faults ----------------------------------------------------------- */

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
 * A fault in window as that may be a heap's: a translation fault (of any
 * level - the piece may lack a table as well as an entry), not a bus
 * error, at an address inside a heap of the address space in the window.
 * Then the heap grows by the piece that holds the address and the access
 * that faulted goes on: OK.  Anything else is for as_fault() to take.
 */
static int
heap_fault(int as, struct mali_vm *vm, uint32_t bits, uint32_t fs,
	uint64_t addr)
{
	struct mali_bo *bo;
	uint64_t piece;
	u64_t t0, t1;
	int r;

	if (vm == NULL || bits != MMU_INT_PAGE_FAULT(as) ||
	    !MALI_EXC_TRANSLATION(AS_FAULT_EXCEPTION(fs)) ||
	    (bo = mali_bo_at(vm, addr)) == NULL ||
	    !(bo->flags & PANFROST_BO_HEAP))
		return ESRCH;

	/* Cleared before the flush that lets the access go on: see above. */
	mali_wr(gpu(), MMU_INT_CLEAR, MMU_INT_PAGE_FAULT(as));
	read_frclock_64(&t0);
	r = mali_bo_grow(bo, addr, &piece);
	read_frclock_64(&t1);
	if (r == EEXIST) {
		/*
		 * The piece is there already: a second access faulted on it
		 * before the first one's flush.  Panfrost only clears such a
		 * fault; kbase sends the flush anyway, because the access may
		 * still be waiting for one, and so does this.
		 */
		log_debug(&mali_log, "%s: heap fault at 0x%08llx, the piece at "
		    "0x%08llx is there already\n", vm->name,
		    (unsigned long long)addr, (unsigned long long)piece);
		(void)as_flush(as, piece, MALI_HEAP_PIECE, AS_COMMAND_FLUSH_PT);
	} else if (r != OK) {
		log_warn(&mali_log, "%s: the heap at 0x%08llx cannot grow at "
		    "0x%08llx (%zu KiB of %zu): %d\n", vm->name,
		    (unsigned long long)bo->va, (unsigned long long)addr,
		    bo->backed / 1024, bo->size / 1024, r);
		return r;
	} else {
		vm->grows++;
		log_debug(&mali_log, "%s: heap fault at 0x%08llx (status "
		    "0x%08x): 2 MiB at 0x%08llx mapped in %u us, %zu KiB of "
		    "%zu\n", vm->name, (unsigned long long)addr, fs,
		    (unsigned long long)piece,
		    frclock_64_to_micros(delta_frclock_64(t0, t1)),
		    bo->backed / 1024, bo->size / 1024);
	}
	/*
	 * A job of this window that failed meanwhile, for a reason of its
	 * own, waits for its MMU half (mali_sched.c); this was it.
	 */
	mali_sched_mmu_fault(as, bo->vm);
	return OK;
}

/*
 * A fault in window as; bits are its MMU_INT_STAT bits.  A heap's grows
 * the heap (heap_fault()).  Any other is taken the way panfrost takes one
 * it cannot resolve: cleared, the window switched off, which fails
 * whatever job is stalled on the fault.  Measured on the board
 * (PORTING-LOG, "Линия MMU"): the clear takes, and the switch-off raises
 * the bit again - the access that waited faults once more as the tables
 * go - while the job fails at once (JOB_BUS_FAULT).  A second clear takes
 * that bit; as_enable() clears again before unmasking whatever the order.
 */
static void
as_fault(int as, uint32_t bits)
{
	struct mali_vm *vm = win[as].vm;
	uint32_t fs, raw;
	uint64_t addr;
	char line[240];
	int loud;

	fs = mali_rd(gpu(), AS_FAULTSTATUS(as));
	addr = ((uint64_t)mali_rd(gpu(), AS_FAULTADDRESS_HI(as)) << 32) |
	    mali_rd(gpu(), AS_FAULTADDRESS_LO(as));

	if (heap_fault(as, vm, bits, fs, addr) == OK)
		return;

	snprintf(line, sizeof(line), "line %d (mmu): status 0x%08x; address "
	    "space %d %s 0x%08x (%s 0x%02x, %s access, source 0x%x) at "
	    "0x%08x%08x, in %s\n", mali.info.irq[MALI_IRQ_MMU], bits, as,
	    (bits & MMU_INT_BUS_ERROR(as)) ? "bus error, fault status" :
	    "fault status", fs, exception_name(fs), AS_FAULT_EXCEPTION(fs),
	    access_name(fs), AS_FAULT_SOURCE(fs), (uint32_t)(addr >> 32),
	    (uint32_t)addr, vm != NULL ? vm->name : "nobody's address space");

	/*
	 * A client's fault is its own bug, and a client can make as many as
	 * it likes: the first few are said, then one in a hundred.
	 */
	loud = vm == NULL || !vm->expect_fault;
	if (vm == NULL || vm->expect_fault || vm->faults < 4 ||
	    vm->faults % 100 == 0) {
		if (loud)
			log_warn(&mali_log, "%s", line);
		else
			log_info(&mali_log, "%s", line);
	}

	mali_wr(gpu(), MMU_INT_CLEAR, bits);
	raw = mali_rd(gpu(), MMU_INT_RAWSTAT);
	as_disable(as);
	if (vm == NULL || vm->expect_fault || vm->faults < 4)
		log_info(&mali_log, "address space %d off: mmu rawstat 0x%08x "
		    "after the clear, 0x%08x after the switch-off, mask 0x%08x; "
		    "job rawstat 0x%08x, slot status 0x%02x 0x%02x, GPU_STATUS "
		    "0x%x\n", as, raw, mali_rd(gpu(), MMU_INT_RAWSTAT),
		    mali_rd(gpu(), MMU_INT_MASK),
		    mali_rd(gpu(), JOB_INT_RAWSTAT),
		    mali_rd(gpu(), JS_STATUS(0)), mali_rd(gpu(), JS_STATUS(1)),
		    mali_rd(gpu(), GPU_STATUS));
	mali_wr(gpu(), MMU_INT_CLEAR, bits);

	if (vm == NULL) {
		log_warn(&mali_log, "address space %d faulted with nobody's "
		    "tables in it\n", as);
		win[as].users = 0;
		win[as].dead = 0;
		return;
	}

	vm->faulted = 1;
	vm->fault_irq = bits;
	vm->fault_status = fs;
	vm->fault_addr = addr;
	vm->faults++;
	vm->as = -1;
	win[as].vm = NULL;
	if (win[as].users > 0)
		win[as].dead = 1;	/* until the job that ran in it is over */

	mali_sched_mmu_fault(as, vm);
}

/*
 * The MMU line: s is MMU_INT_STAT.  Every bit of it is cleared here, each
 * at its moment (as_fault(), heap_fault()); the caller clears nothing.
 */
void
mali_mmu_irq(uint32_t s)
{
	int as;

	for (as = 0; as < MALI_NR_AS; as++)
		if (s & MMU_INT_AS(as))
			as_fault(as, s & MMU_INT_AS(as));
}
