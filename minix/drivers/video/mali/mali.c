/*
 * The Mali GPU itself, and the service's own life.
 *
 * What a start does, in order, and what each step proves:
 *
 *   1. the node, its lines, its clock and its domain, out of the tree
 *      (mali_find.c) - on QEMU there is none, and the service says so
 *      and does not start; the system boots on;
 *   2. the SoC around the part (mali_rk.c): clocks, power domain, resets,
 *      each read first and reported;
 *   3. GPU_ID - the first number, the way 0x4f51e91b was for the network.
 *      It is read only once the domain and the gates are known to be on,
 *      because a read of a block that is not powered or clocked hangs the
 *      bus on this SoC.  A plausible value here means power, clock, reset,
 *      the bus to the block and the mapping are all right at once; 0 or
 *      all ones means none of the harder steps would have had a chance;
 *   4. a soft reset, finished when the part says RESET_COMPLETED;
 *   5. the cores powered, L2 first: the ready bitmaps equal to the present
 *      ones say the GPU's own power controller works, which no amount of
 *      reading identity registers can;
 *   6. one interrupt on purpose - a cache clean, whose completion the GPU
 *      raises on its "gpu" line - so that the line number the tree gave is
 *      proved end to end, before anything waits on it.
 *
 * The job and MMU lines are taken too and report whatever arrives on them;
 * they are proved by the first job (the second half of 11.5).
 */

#include <minix/drivers.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"

struct mali mali;

struct log mali_log = {
	.name = "mali",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

static const char *const irq_names[MALI_NR_IRQS] = { "gpu", "job", "mmu" };

/*
 * The notification bit of each line.  Not the line number: the kernel
 * reports interrupts as bits of one word and refuses hook identifiers above
 * 63, and the GPU's lines are 71, 72 and 73 (see xhci.c for the driver
 * that found that out).
 */
#define IRQ_BIT(i)	(1 + (i))

/*
 * Bifrost parts by product, for the log.  The product field carries the
 * architecture in its top bits and the model in its bottom ones, with a
 * revision in between that the model does not depend on - the same mask
 * both Linux drivers apply.  The G52 of the RK3566 is 0x7402.
 */
static const struct {
	uint32_t product;
	const char *name;
} models[] = {
	{ 0x6000, "Mali-G71" },
	{ 0x6001, "Mali-G72" },
	{ 0x7000, "Mali-G51" },
	{ 0x7001, "Mali-G76" },
	{ 0x7002, "Mali-G52" },
	{ 0x7003, "Mali-G31" },
};

static const char *
model_name(uint32_t gpu_id)
{
	uint32_t product = GPU_ID_PRODUCT(gpu_id);
	unsigned i;

	if (GPU_ID_ARCH_MAJOR(gpu_id) >= 6)
		product &= 0xf00f;
	for (i = 0; i < sizeof(models) / sizeof(models[0]); i++)
		if (models[i].product == product)
			return models[i].name;
	return "an unknown Mali";
}

/* Poll a GPU register until (value & mask) == want, up to usec. */
static int
gpu_wait(unsigned off, uint32_t mask, uint32_t want, unsigned usec)
{
	unsigned spent;

	for (spent = 0; spent <= usec; spent += 10) {
		if ((mali_rd(&mali.info.gpu, off) & mask) == want)
			return OK;
		micro_delay(10);
	}
	return ETIMEDOUT;
}

static int
gpu_soft_reset(void)
{
	const struct mali_regs *gpu = &mali.info.gpu;

	mali_wr(gpu, GPU_INT_MASK, 0);
	mali_wr(gpu, GPU_INT_CLEAR, GPU_IRQ_ALL);
	mali_wr(gpu, GPU_CMD, GPU_CMD_SOFT_RESET);

	if (gpu_wait(GPU_INT_RAWSTAT, GPU_IRQ_RESET_COMPLETED,
	    GPU_IRQ_RESET_COMPLETED, 10000) != OK) {
		log_warn(&mali_log, "the soft reset did not complete "
		    "(rawstat 0x%08x, status 0x%08x)\n",
		    mali_rd(gpu, GPU_INT_RAWSTAT), mali_rd(gpu, GPU_STATUS));
		return EIO;
	}
	mali_wr(gpu, GPU_INT_CLEAR, GPU_IRQ_ALL);
	mali_wr(gpu, JOB_INT_MASK, 0);
	mali_wr(gpu, MMU_INT_MASK, 0);
	log_info(&mali_log, "soft reset complete\n");
	return OK;
}

static void
gpu_read_features(void)
{
	const struct mali_regs *gpu = &mali.info.gpu;

	mali.l2_features = mali_rd(gpu, GPU_L2_FEATURES);
	mali.core_features = mali_rd(gpu, GPU_CORE_FEATURES);
	mali.tiler_features = mali_rd(gpu, GPU_TILER_FEATURES);
	mali.mem_features = mali_rd(gpu, GPU_MEM_FEATURES);
	mali.mmu_features = mali_rd(gpu, GPU_MMU_FEATURES);
	mali.as_present = mali_rd(gpu, GPU_AS_PRESENT);
	mali.js_present = mali_rd(gpu, GPU_JS_PRESENT);
	mali.thread_max_threads = mali_rd(gpu, GPU_THREAD_MAX_THREADS);
	mali.thread_features = mali_rd(gpu, GPU_THREAD_FEATURES);
	mali.coherency_features = mali_rd(gpu, GPU_COHERENCY_FEATURES);
	mali.shader_present = mali_rd(gpu, GPU_SHADER_PRESENT_LO);
	mali.tiler_present = mali_rd(gpu, GPU_TILER_PRESENT_LO);
	mali.l2_present = mali_rd(gpu, GPU_L2_PRESENT_LO);
	mali.stack_present = mali_rd(gpu, GPU_STACK_PRESENT_LO);

	log_info(&mali_log, "features: L2 0x%08x core 0x%08x tiler 0x%08x "
	    "mem 0x%08x mmu 0x%08x (%u-bit VA, %u-bit PA)\n",
	    mali.l2_features, mali.core_features, mali.tiler_features,
	    mali.mem_features, mali.mmu_features, mali.mmu_features & 0xff,
	    (mali.mmu_features >> 8) & 0xff);
	log_info(&mali_log, "present: shader 0x%x tiler 0x%x L2 0x%x stack "
	    "0x%x, address spaces 0x%x, job slots 0x%x, %u threads, "
	    "coherency 0x%x\n", mali.shader_present, mali.tiler_present,
	    mali.l2_present, mali.stack_present, mali.as_present,
	    mali.js_present, mali.thread_max_threads,
	    mali.coherency_features);
	log_info(&mali_log, "config: jm 0x%08x shader 0x%08x tiler 0x%08x "
	    "l2/mmu 0x%08x\n", mali_rd(gpu, GPU_JM_CONFIG),
	    mali_rd(gpu, GPU_SHADER_CONFIG), mali_rd(gpu, GPU_TILER_CONFIG),
	    mali_rd(gpu, GPU_L2_MMU_CONFIG));
}

/*
 * One kind of core: write its present bitmap to the power-on register and
 * wait until the ready bitmap says the same.
 */
static int
gpu_power_kind(const char *what, unsigned pwron, unsigned ready,
	uint32_t present)
{
	const struct mali_regs *gpu = &mali.info.gpu;

	if (present == 0) {
		log_warn(&mali_log, "no %s core is present\n", what);
		return EIO;
	}
	mali_wr(gpu, pwron, present);
	if (gpu_wait(ready, present, present, 20000) != OK) {
		log_warn(&mali_log, "the %s cores did not power up: ready "
		    "0x%x of 0x%x\n", what, mali_rd(gpu, ready), present);
		return EIO;
	}
	return OK;
}

static int
gpu_power_up(void)
{
	const struct mali_regs *gpu = &mali.info.gpu;
	int r;

	if ((r = gpu_power_kind("L2", GPU_L2_PWRON_LO, GPU_L2_READY_LO,
	    mali.l2_present)) != OK ||
	    (r = gpu_power_kind("shader", GPU_SHADER_PWRON_LO,
	    GPU_SHADER_READY_LO, mali.shader_present)) != OK ||
	    (r = gpu_power_kind("tiler", GPU_TILER_PWRON_LO,
	    GPU_TILER_READY_LO, mali.tiler_present)) != OK)
		return r;

	log_info(&mali_log, "cores ready: shader 0x%x tiler 0x%x L2 0x%x\n",
	    mali_rd(gpu, GPU_SHADER_READY_LO), mali_rd(gpu, GPU_TILER_READY_LO),
	    mali_rd(gpu, GPU_L2_READY_LO));
	return OK;
}

/* The cores off again, L2 last; the domain itself is left on. */
static void
gpu_power_down(void)
{
	const struct mali_regs *gpu = &mali.info.gpu;

	mali_wr(gpu, GPU_SHADER_PWROFF_LO, mali.shader_present);
	mali_wr(gpu, GPU_TILER_PWROFF_LO, mali.tiler_present);
	(void)gpu_wait(GPU_SHADER_READY_LO, mali.shader_present, 0, 20000);
	(void)gpu_wait(GPU_TILER_READY_LO, mali.tiler_present, 0, 20000);
	mali_wr(gpu, GPU_L2_PWROFF_LO, mali.l2_present);
	(void)gpu_wait(GPU_L2_READY_LO, mali.l2_present, 0, 20000);
}

static void
irq_setup(void)
{
	int i, r;

	for (i = 0; i < MALI_NR_IRQS; i++) {
		mali.irq_hook[i] = -1;
		if (mali.info.irq[i] < 0) {
			log_warn(&mali_log, "the tree names no \"%s\" line\n",
			    irq_names[i]);
			continue;
		}
		mali.irq_hook[i] = IRQ_BIT(i);
		if ((r = sys_irqsetpolicy(mali.info.irq[i], 0,
		    &mali.irq_hook[i])) != OK) {
			log_warn(&mali_log, "cannot take line %d (%s): %d\n",
			    mali.info.irq[i], irq_names[i], r);
			mali.irq_hook[i] = -1;
			continue;
		}
		if ((r = sys_irqenable(&mali.irq_hook[i])) != OK)
			log_warn(&mali_log, "cannot switch line %d (%s) on: "
			    "%d\n", mali.info.irq[i], irq_names[i], r);
	}
}

/*
 * Whatever arrived on one line: read the block's status, say it, clear it
 * and let the line raise again.  The masks are such that only the cache
 * clean of step 6 and faults reach here for now.
 */
static void
irq_handle(int i)
{
	static const unsigned stat[MALI_NR_IRQS] = {
		GPU_INT_STAT, JOB_INT_STAT, MMU_INT_STAT
	};
	static const unsigned clear[MALI_NR_IRQS] = {
		GPU_INT_CLEAR, JOB_INT_CLEAR, MMU_INT_CLEAR
	};
	const struct mali_regs *gpu = &mali.info.gpu;
	uint32_t s;

	s = mali_rd(gpu, stat[i]);
	mali.irq_count[i]++;

	if (i == MALI_IRQ_GPU && s == GPU_IRQ_CLEAN_CACHES &&
	    mali.irq_count[i] == 1)
		log_info(&mali_log, "line %d (gpu): cache clean completed - "
		    "the line works\n", mali.info.irq[i]);
	else if (i == MALI_IRQ_GPU && (s & (GPU_IRQ_FAULT |
	    GPU_IRQ_MULTIPLE_FAULT)))
		log_warn(&mali_log, "line %d (gpu): fault, status 0x%08x, "
		    "address 0x%08x%08x\n", mali.info.irq[i],
		    mali_rd(gpu, GPU_FAULTSTATUS),
		    mali_rd(gpu, GPU_FAULTADDRESS_HI),
		    mali_rd(gpu, GPU_FAULTADDRESS_LO));
	else
		log_info(&mali_log, "line %d (%s): status 0x%08x\n",
		    mali.info.irq[i], irq_names[i], s);

	if (s != 0)
		mali_wr(gpu, clear[i], s);
	(void)sys_irqenable(&mali.irq_hook[i]);
}

static int
mali_init(int type, sef_init_info_t *UNUSED(info))
{
	const struct mali_regs *gpu = &mali.info.gpu;
	uint32_t id;
	int r;

	if (type == SEF_INIT_RESTART)
		log_info(&mali_log, "restarted; bringing the GPU up again\n");

	if ((r = mali_find(&mali.info)) != OK)
		return r;
	if ((r = mali_rk_map()) != OK)
		return r;

	mali_rk_report("found");
	if ((r = mali_rk_clocks_on()) != OK ||
	    (r = mali_rk_power_domain()) != OK)
		return r;
	mali_rk_resets();

	id = mali.gpu_id = mali_rd(gpu, GPU_ID);
	if (id == 0 || id == 0xffffffff) {
		log_warn(&mali_log, "the GPU does not answer: GPU_ID 0x%08x; "
		    "its domain, clocks or supply are wrong\n", id);
		mali_rk_report("dead");
		return ENXIO;
	}
	log_info(&mali_log, "%s, product 0x%04x r%up%u status %u (GPU_ID "
	    "0x%08x)\n", model_name(id), GPU_ID_PRODUCT(id),
	    GPU_ID_VER_MAJOR(id), GPU_ID_VER_MINOR(id), GPU_ID_VER_STATUS(id),
	    id);

	if ((r = gpu_soft_reset()) != OK)
		return r;
	gpu_read_features();
	if ((r = gpu_power_up()) != OK)
		return r;
	mali_rk_report("up");

	/* Step 6: one interrupt on purpose, taken in the main loop. */
	irq_setup();
	if (mali.irq_hook[MALI_IRQ_GPU] >= 0) {
		mali_wr(gpu, GPU_INT_MASK, GPU_IRQ_FAULT |
		    GPU_IRQ_MULTIPLE_FAULT | GPU_IRQ_CLEAN_CACHES);
		mali_wr(gpu, GPU_CMD, GPU_CMD_CLEAN_CACHES);
	}
	return OK;
}

static void
mali_signal(int signo)
{
	int i;

	if (signo != SIGTERM)
		return;

	log_info(&mali_log, "going down; powering the cores off\n");
	for (i = 0; i < MALI_NR_IRQS; i++)
		if (mali.irq_hook[i] >= 0)
			(void)sys_irqrmpolicy(&mali.irq_hook[i]);
	mali_wr(&mali.info.gpu, GPU_INT_MASK, 0);
	gpu_power_down();
	exit(0);
}

int
main(int argc, char *argv[])
{
	message m;
	int ipc_status, r, i;
	long v;

	env_setargs(argc, argv);
	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		mali_log.log_level = (int)v;

	sef_setcb_init_fresh(mali_init);
	sef_setcb_init_restart(mali_init);
	sef_setcb_signal_handler(mali_signal);
	sef_startup();

	/*
	 * Nobody talks to this service yet: the interface Mesa will use is
	 * stage 11.6.  The loop is here so that the service stays alive with
	 * the GPU up, and so that its interrupts have somewhere to go.
	 */
	for (;;) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK)
			panic("sef_receive failed: %d", r);

		if (is_ipc_notify(ipc_status)) {
			if (_ENDPOINT_P(m.m_source) == HARDWARE) {
				for (i = 0; i < MALI_NR_IRQS; i++)
					if (mali.irq_hook[i] >= 0 &&
					    (m.m_notify.interrupts &
					    (1u << IRQ_BIT(i))))
						irq_handle(i);
			}
			continue;
		}

		log_debug(&mali_log, "message %d from %d ignored\n",
		    m.m_type, m.m_source);
	}

	return 0;
}
