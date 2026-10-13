#ifndef _MALI_H
#define _MALI_H

#include <minix/drivers.h>
#include <minix/log.h>

/*
 * The GPU of the RK3566/RK3568, and so of the BIGTREETECH CB2: an ARM
 * Mali-G52 r1p0 (Bifrost; one shader core of two execution engines, one
 * tiler, one L2 - GPU_ID 0x74021000 on the board), node gpu@fde60000 in
 * the device tree.  Stage 11.5 of the plan, first half: power, clocks, reset, the
 * identity register and the cores powered up.  The interface Mesa's
 * Panfrost driver talks to (panfrost_drm.h) comes with 11.6; until then
 * this service brings the part up, says what it found and keeps it so.
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
 *   mali_find.c  the device tree: the node, its three lines, its clock,
 *                its power domain and its supply
 *   mali_rk.c    the SoC around the GPU: power domain, clock gates and
 *                resets of the RK3568 (the only file that knows the SoC)
 *   mali_job.c   the GPU's own MMU and its first jobs (a WRITE_VALUE
 *                chain that fills a buffer, checked by the processor)
 *   mali.c       the GPU itself and the service's own life
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

/* mali_find.c */
int mali_find(struct mali_info *info);

/* mali_rk.c */
int mali_rk_map(void);
void mali_rk_report(const char *when);
int mali_rk_clocks_on(void);
int mali_rk_power_domain(void);
void mali_rk_resets(void);
unsigned mali_rk_compare(void);

/* mali_job.c */
int mali_job_setup(void);
int mali_job_start(void);
void mali_job_irq(uint32_t status);
void mali_mmu_irq(uint32_t status);
void mali_job_alarm(void);
void mali_job_teardown(void);

#endif /* _MALI_H */
