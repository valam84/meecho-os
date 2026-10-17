#ifndef _MALI_H
#define _MALI_H

#include <minix/drivers.h>
#include <minix/drmdriver.h>
#include <minix/log.h>
#include <minix/timers.h>

#include <sys/queue.h>

/*
 * The GPU of the RK3566/RK3568, and so of the BIGTREETECH CB2: an ARM
 * Mali-G52 r1p0 (Bifrost; one shader core of two execution engines, one
 * tiler, one L2 - GPU_ID 0x74021000 on the board), node gpu@fde60000 in
 * the device tree.  Stage 11.5 brought the part up, gave it page tables
 * and ran jobs in them; stage 11.6 gives it clients: the interface Mesa's
 * Panfrost driver talks to (panfrost_drm.h), on /dev/dri/renderD128.
 *
 * Why a service of its own rather than a part of fb.  The two have
 * nothing in common but the screen at the end: the display controller
 * reads a frame by physical address, the GPU writes one through its own
 * MMU, and either can die without the other.  A desktop that falls back
 * to drawing with the processor when the GPU service goes away is the
 * point of stage 11.7 and possible only because this is a process.
 *
 * The parts, split by what each one talks to:
 *
 *   mali_find.c   the device tree: the node, its three lines, its clock,
 *                 its power domain and its supply
 *   mali_rk.c     the SoC around the GPU: power domain, clock gates and
 *                 resets of the RK3568 (the only file that knows the SoC)
 *   mali_mmu.c    the GPU's MMU: its eight address-space windows, page
 *                 tables, and what a fault does
 *   mali_bo.c     buffers: a place in a GPU address space for memory of
 *                 libdrmdriver's (pages, caches, a client's mapping)
 *   mali_sched.c  jobs: the slots, which job runs when, how it ends
 *   mali_drm.c    the clients: Panfrost's calls on libdrmdriver's render
 *                 node, which has the contexts, sync objects and descriptors
 *   mali_job.c    the self-test at start (a chain of WRITE_VALUE jobs,
 *                 checked by the processor; "faulttest=1" adds a fault)
 *   mali.c        the GPU itself and the service's own life
 *
 * Linux's drivers for this part (kbase and panfrost) are GPL, and only
 * register numbers and the order of steps are taken from them; every
 * value written is checked against the working vendor system on the board
 * (port/cb2-gpu/).
 */

/* The three interrupt lines, by the tree's names for them. */
#define MALI_IRQ_GPU		0
#define MALI_IRQ_JOB		1
#define MALI_IRQ_MMU		2
#define MALI_NR_IRQS		3

#define MALI_PAGE		4096u
#define MALI_NR_AS		8	/* address-space windows (AS_PRESENT) */
#define MALI_NR_SLOTS		3	/* job slots (JS_PRESENT) */
#define MALI_MAX_OPP		16	/* operating points kept from the tree */

struct mali_regs {
	phys_bytes base;
	size_t size;
	vir_bytes v;			/* mapped, or 0 */
};

/* What the device tree says about this machine's GPU. */
struct mali_info {
	struct mali_regs gpu;
	int irq[MALI_NR_IRQS];		/* GIC line, -1 if not named */

	/*
	 * The clock the tree calls "bus", as <provider, id>: the gate in
	 * front of the core.  The one it calls "gpu" belongs to the secure
	 * firmware (an SCMI clock) and is only reported.
	 */
	uint32_t bus_clk_phandle;
	uint32_t bus_clk_id;
	uint32_t gpu_clk_phandle;
	uint32_t gpu_clk_id;
	phys_bytes bus_clk_provider;	/* reg of the "bus" provider, or 0 */

	int power_domain;		/* in the PMU's numbering, or -1 */

	/* The supply, followed by its phandle to the regulator node. */
	uint32_t supply_phandle;
	char supply_name[32];
	int supply_always_on;
	int supply_boot_on;
	unsigned supply_init_uv;

	/*
	 * The operating points ("operating-points-v2"), in the tree's order:
	 * a rate and the voltage it needs - the first, target, cell of
	 * opp-microvolt.  The per-bin variants (opp-microvolt-L0..L3) are
	 * lower or equal and are not read: without knowing the bin, the
	 * target is the one that holds for every chip.
	 */
	uint32_t opp_phandle;
	unsigned nr_opp;
	struct mali_opp {
		uint64_t hz;
		unsigned uv;
	} opp[MALI_MAX_OPP];

	/* The SoC blocks around the GPU, by compatible string. */
	struct mali_regs cru;
	struct mali_regs pmu;
};

struct mali {
	struct mali_info info;

	uint32_t gpu_id;
	uint32_t l2_features, core_features, tiler_features, mem_features;
	uint32_t mmu_features, as_present, js_present;
	uint32_t thread_max_threads, thread_features, coherency_features;
	uint32_t shader_present, tiler_present, l2_present, stack_present;

	int irq_hook[MALI_NR_IRQS];	/* -1 if the line is not taken */
	unsigned irq_count[MALI_NR_IRQS];
};

extern struct mali mali;
extern struct log mali_log;

static inline uint32_t
mali_rd(const struct mali_regs *r, unsigned off)
{
	return *(volatile uint32_t *)(r->v + off);
}

static inline void
mali_wr(const struct mali_regs *r, unsigned off, uint32_t val)
{
	*(volatile uint32_t *)(r->v + off) = val;
}

/*
 * One GPU address space: page tables, the buffers placed in it, and the
 * window it is loaded into while jobs run in it.  Each client has one,
 * and so has the self-test; the GPU's MMU never sees one through another.
 */
struct mali_table;
struct mali_bo;
TAILQ_HEAD(mali_bo_list, mali_bo);

struct mali_vm {
	struct mali_table *root;	/* level 0, or NULL */
	int as;				/* the window it is in, or -1 */
	struct mali_bo_list bos;	/* by GPU address */
	size_t mem;			/* bytes of buffers */
	const char *name;		/* for the log: "client 3", "self-test" */

	/* The last fault in it - for the job that was running there. */
	int faulted;
	uint32_t fault_irq;		/* MMU_INT bits: page fault, bus error */
	uint32_t fault_status;
	uint64_t fault_addr;
	int expect_fault;		/* the self-test asked for one */
	unsigned faults;		/* how many so far, for the log */
	unsigned grows;			/* faults that grew a heap */
};

/* mali_find.c */
int mali_find(struct mali_info *info);

/* mali_rk.c */
int mali_rk_map(void);
void mali_rk_report(const char *when);
int mali_rk_clocks_on(unsigned want_mhz);
int mali_rk_power_domain(void);
void mali_rk_resets(void);
unsigned mali_rk_compare(void);

/* mali_mmu.c */
int mali_vm_init(struct mali_vm *vm, const char *name);
void mali_vm_fini(struct mali_vm *vm);
int mali_vm_map(struct mali_vm *vm, uint64_t va, const phys_bytes *pages,
	size_t npages, uint64_t prot);
void mali_vm_unmap(struct mali_vm *vm, uint64_t va, size_t npages);
phys_bytes mali_vm_root(const struct mali_vm *vm);
int mali_as_get(struct mali_vm *vm);
void mali_as_put(int as);
void mali_mmu_irq(uint32_t status);
void mali_mmu_reset(void);

/*
 * A buffer: memory the GPU and the processor share, in two parts.
 *
 * The memory (struct drmdrv_mem, libdrmdriver's) is one, whoever has it:
 * the service owns it (anonymous memory of its own, page by page, so that
 * no large buffer needs contiguous RAM), and maps it into a client that
 * asks (vm_remap).  A buffer object (struct mali_bo) is that memory placed
 * in one GPU address space, at an address of its own there.  A buffer made
 * by a context has one placement; given to another context as a descriptor
 * (PRIME, stage 11.6) it gets a second one - the same pages, the
 * importer's own address - as under panfrost, where a GEM object has a
 * mapping per DRM file.  What is the memory's - its pages, the
 * processor's caches around it, the jobs that use it - is counted on the
 * memory, so a job of one context and a wait of another see the same.
 *
 * A heap (PANFROST_BO_HEAP) is the exception: only its place in the
 * address space is taken at creation, and its memory comes 2 MiB at a time
 * as the GPU faults on it (mali_bo_grow()); no processor ever maps it, and
 * it never leaves its context (panfrost will not export one either), so
 * its pieces are its placement's.
 */
#define MALI_HEAP_PIECE	0x200000u	/* what a fault in a heap brings in */

struct mali_bo {
	TAILQ_ENTRY(mali_bo) link;	/* in its address space, by address */
	struct mali_vm *vm;
	struct drmdrv_mem *mem;
	uint32_t flags;			/* PANFROST_BO_* */
	size_t size;			/* a whole number of pages */
	size_t backed;			/* bytes of memory it counts in vm */
	void **pieces;			/* a heap's, NULL until a fault */
	uint64_t va;			/* in the GPU address space */
	unsigned refs;			/* a handle, a job, a wait each hold one */
	uint64_t mmap_offset;		/* the token mmap(2) is given */
};

#define MALI_VA_START	0x02000000ull	/* panfrost's: 32 MiB */
#define MALI_VA_END	0x100000000ull	/* and 4 GiB */

void mali_bo_limits(size_t total, size_t per_vm);
struct mali_bo *mali_bo_new(struct mali_vm *vm, size_t size, uint32_t flags,
	int *err);
void mali_bo_ref(struct mali_bo *bo);
void mali_bo_unref(struct mali_bo *bo);
struct mali_bo *mali_bo_new_phys(struct mali_vm *vm, phys_bytes phys,
	endpoint_t src_ep, vir_bytes src_vir,
	size_t size, int *err);
struct mali_bo *mali_bo_import(struct mali_vm *vm, struct drmdrv_mem *mem,
	int *err);
void mali_bo_for_gpu(struct mali_bo *bo);
void mali_bo_for_cpu(struct mali_bo *bo);
int mali_bo_map_into(struct mali_bo *bo, endpoint_t who, void *addr,
	int writable, void **where);
struct mali_bo *mali_bo_at(struct mali_vm *vm, uint64_t va);
int mali_bo_grow(struct mali_bo *bo, uint64_t va, uint64_t *piece);
size_t mali_bo_total(void);

/*
 * A job: a chain of descriptors at a GPU address, run in one address
 * space on one slot.  Jobs of one address space run one at a time and in
 * the order given - what panfrost's implicit fences on shared buffers
 * come to for a client whose every job touches its tiler heap - and jobs
 * of different address spaces run side by side as slots and windows
 * allow.
 */
struct mali_job {
	TAILQ_ENTRY(mali_job) link;	/* the queue, in the order given */
	struct mali_vm *vm;
	uint64_t jc;			/* the first descriptor */
	uint32_t req;			/* PANFROST_JD_REQ_* */
	struct mali_bo **bos;		/* what it uses (held by the owner) */
	unsigned nbos;

	int slot, as, state;
	int result;			/* OK, EIO, ETIMEDOUT, ECANCELED */
	uint32_t js_status;		/* the slot's word on how it ended */
	int fault;			/* an MMU fault came with it */
	uint32_t fault_status;
	uint64_t fault_addr;
	int await_mmu;			/* failed; the MMU's half not yet in */
	int stopping;			/* hard stop sent at its deadline */
	u64_t started;			/* frclock */
	uint32_t config;		/* JS_CONFIG it ran with */

	/* Told when it is over, from the job or the MMU line. */
	void (*done)(struct mali_job *job);
	void *owner;
};

#define MALI_JOB_QUEUED		0
#define MALI_JOB_RUNNING	1
#define MALI_JOB_DONE		2

/* mali_sched.c */
void mali_sched_init(void);
void mali_job_submit(struct mali_job *job);
void mali_sched_job_irq(uint32_t status);
void mali_sched_mmu_fault(int as, struct mali_vm *vm);
void mali_sched_cancel(struct mali_vm *vm);
void mali_sched_fail_all(int result);
void mali_sched_kick(void);

/* mali_job.c: the self-test */
int mali_job_setup(void);
int mali_job_start(int faulttest);
void mali_job_teardown(void);

/* mali_drm.c: the device */
struct chardriver;
extern struct chardriver mali_chardriver;
void mali_drm_init(void);
void mali_drm_shutdown(void);

/* mali.c */
int mali_gpu_recover(void);

#endif /* _MALI_H */
