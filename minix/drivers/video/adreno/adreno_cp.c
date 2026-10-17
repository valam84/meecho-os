/*
 * The command processor (port/clover/hw-gpu.md, §6, §7; stage cp): the
 * GPU's first own work.  Everything in Linux's order (a5xx_gpu.c:
 * a5xx_pm_resume(), a5xx_hw_init(), a5xx_me_init(), a5xx_submit(),
 * a5xx_pm_suspend() - GPL; offsets, values and order, not the code):
 *
 *   1. the microcode (a530_pm4.fw, a530_pfp.fw from fwdir=, adreno_fw.c),
 *      the ring (32 KiB) and a page for what the GPU writes back (its read
 *      pointer, the fence) in memory of this service, mapped for the GPU
 *      in bank 0's tables at 32 MiB and up, and the GPU's stream routed to
 *      the bank (adreno_smmu.c) - the SMMU stages have just shown that
 *      both work, and without them the GPU reaches no memory at all (§13);
 *   2. the hardware clock gating and every register a5xx_hw_init() writes
 *      for an A512 (hw_init() below; the table is §6's);
 *   3. the microcode's and the ring's addresses, the interrupt mask, and
 *      CP_PFP_ME_CNTL 0: the processor starts;
 *   4. CP_ME_INIT, WHERE_AM_I, the write pointer - and the wait until the
 *      processor has read all of it and the GPU is idle (Q20);
 *   5. the zap shader (adreno_zap.c: loaded the first time, resumed after)
 *      and CP_SET_SECURE_MODE 0 from the ring: out of secure mode (Q21);
 *   6. the first command: CP_SCRATCH_REG(2) = 1 and CACHE_FLUSH_TS with an
 *      interrupt - the fence written to memory and the GPU's line (332)
 *      raised (Q22);
 *   7. the interrupts masked, the VBIF's ports halted (a5xx_pm_suspend()),
 *      so that nothing is in flight when the stream is taken back and the
 *      domains collapse (Q23).
 *
 * The GPU reads and writes memory past the processor's caches (bank 0's
 * walk and its memory type 0 are non-cacheable), so the ring and the
 * microcode are cleaned to memory before the GPU is told about them, and
 * the written-back page is invalidated before it is read.
 *
 * One ring, no preemption: Linux too runs one ring when it has a single
 * one (a5xx_preempt_hw_init(), a5xx_preempt_start()).
 */

#include "adreno.h"
#include "adreno_regs.h"

#include <minix/cachectl.h>

#include <stdlib.h>
#include <string.h>

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))
#define G(r)		ad_rd(&ad.gpu, A5XX(r))
#define W(r, v)		ad_wr(&ad.gpu, A5XX(r), (v))

/* Where the GPU sees its buffers: above the SMMU test's 16 MiB page. */
#define VA_PM4		0x02000000ull
#define VA_PFP		0x02010000ull
#define VA_RING		0x02020000ull
#define VA_MEM		0x02030000ull
/* In the written-back page: Linux's struct msm_rbmemptrs, and the read
 * pointer's shadow (WHERE_AM_I) after it. */
#define MEM_RPTR	0x00
#define MEM_FENCE	0x04
#define MEM_SHADOW	0x40

#define FW_MAX		(64 * 1024)
#define GPU_HOOK	1		/* the SMMU's line has hook 0 */

struct gbuf {
	void *cpu;
	phys_bytes pa;
	size_t size;
	uint64_t va;
};

static struct {
	struct gbuf pm4, pfp, ring, mem;
	size_t pm4_len, pfp_len;	/* the microcode's bytes */
	uint32_t wptr;			/* in dwords */
	int hooked, hook;
	int whereami;
} cp;

/*===========================================================================*
 *    memory the GPU sees                                                    *
 *===========================================================================*/
static int
gbuf_new(struct gbuf *b, size_t size, uint64_t va, int prot)
{
	int r;

	b->size = (size + 4095) & ~(size_t)4095;
	if ((b->cpu = alloc_contig(b->size, AC_ALIGN4K, &b->pa)) == NULL) {
		b->size = 0;
		return ENOMEM;
	}
	memset(b->cpu, 0, b->size);
	b->va = va;
	if ((r = sys_cachectl(CACHE_CLEAN, b->cpu, b->size)) != OK)
		return r;
	return smmu_gpu_map(va, b->pa, b->size, prot);
}

static void
gbuf_free(struct gbuf *b)
{
	if (b->cpu != NULL)
		free_contig(b->cpu, b->size);
	memset(b, 0, sizeof(*b));
}

/* What the GPU wrote, as memory holds it. */
static uint32_t
mem_word(uint32_t off)
{
	(void)sys_cachectl(CACHE_INVALIDATE, cp.mem.cpu, 4096);
	return *(volatile uint32_t *)(void *)((uint8_t *)cp.mem.cpu + off);
}

/* The microcode: the file without its first word (adreno_fw_create_bo()). */
static int
ucode(const char *name, struct gbuf *b, uint64_t va, uint32_t w[4],
	size_t *bytes)
{
	void *f;
	size_t len;
	int r;

	if ((r = fw_read(name, &f, &len, FW_MAX)) != OK) {
		log_warn(&ad_log, "Q19 cp: %s/%s: %d; refusing\n", ad_fwdir,
		    name, r);
		return r;
	}
	if (len < 20 || (len & 3) != 0) {
		log_warn(&ad_log, "Q19 cp: %s: %zu bytes is no microcode; "
		    "refusing\n", name, len);
		free(f);
		return EINVAL;
	}
	if ((r = gbuf_new(b, len - 4, va, AD_PT_READ)) != OK) {
		free(f);
		return r;
	}
	memcpy(b->cpu, (uint8_t *)f + 4, len - 4);
	memcpy(w, f, 16);
	*bytes = len - 4;
	free(f);
	return sys_cachectl(CACHE_CLEAN, b->cpu, b->size);
}

/*===========================================================================*
 *    the ring                                                               *
 *===========================================================================*/
/* PM4 headers: each field's parity (adreno_pm4.xml; Linux's PM4_PARITY). */
static uint32_t
parity(uint32_t v)
{
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	return (0x9669u >> (v & 0xf)) & 1;
}

uint32_t
pm4_pkt7(uint32_t op, uint32_t cnt)
{
	return PM4_TYPE7 | (cnt & 0x7fff) | parity(cnt) << 15 |
	    (op & 0x7f) << 16 | parity(op) << 23;
}

uint32_t
pm4_pkt4(uint32_t reg, uint32_t cnt)
{
	return PM4_TYPE4 | (cnt & 0x7f) | parity(cnt) << 7 |
	    (reg & 0x3ffff) << 8 | parity(reg) << 27;
}

static void
out(uint32_t v)
{
	((volatile uint32_t *)cp.ring.cpu)[cp.wptr] = v;
	cp.wptr = (cp.wptr + 1) % (A5XX_RING_SIZE / 4);
}

/*
 * a5xx_flush(): WHERE_AM_I first (the processor writes where it is to the
 * shadow), the ring cleaned to memory, then the write pointer.
 */
static void
flush(void)
{
	if (cp.whereami) {
		out(pm4_pkt7(CP_WHERE_AM_I, 2));
		out((uint32_t)(cp.mem.va + MEM_SHADOW));
		out((uint32_t)((cp.mem.va + MEM_SHADOW) >> 32));
	}
	(void)sys_cachectl(CACHE_CLEAN, cp.ring.cpu, cp.ring.size);
	W(A5XX_CP_RB_WPTR, cp.wptr);
}

/*
 * a5xx_idle(), as Linux waits: first the processor has read up to the
 * write pointer (adreno_idle(), a second), then the GPU is idle and saw no
 * hang (_a5xx_check_idle(), another second).  The read pointer is taken
 * from the register; the shadow WHERE_AM_I writes is reported beside it.
 * idle_rptr_us and idle_busy (the RBBM_STATUS bits last seen busy, but for
 * HI_BUSY) say where the time went.
 */
static unsigned idle_rptr_us;
static uint32_t idle_busy;

static int
idle(unsigned *us)
{
	unsigned t, t0;
	uint32_t st;

	idle_busy = 0;
	for (t = 0; G(A5XX_CP_RB_RPTR) != cp.wptr; t += 10) {
		if (t >= 1000000) {
			idle_rptr_us = t;
			*us = t;
			return ETIMEDOUT;
		}
		micro_delay(10);
	}
	idle_rptr_us = t;
	for (t0 = t; ; t += 10) {
		st = G(A5XX_RBBM_STATUS) & ~A5XX_RBBM_STATUS_HI_BUSY;
		if (st == 0 && !(G(A5XX_RBBM_INT_0_STATUS) &
		    A5XX_INT_MISC_HANG_DETECT)) {
			*us = t;
			return OK;
		}
		if (st != 0)
			idle_busy = st;
		if (t - t0 >= 1000000) {
			*us = t;
			return ETIMEDOUT;
		}
		micro_delay(10);
	}
}

static void
say_stuck(const char *what)
{
	log_warn(&ad_log, "Q20 cp: %s: NOT idle: rptr %s at %u us, busy %08x; "
	    "now rbbm %08x int %08x rptr %x wptr %x shadow %x cp_int %08x "
	    "hw_fault %08x protect %08x; smmu fsr %x\n", what,
	    G(A5XX_CP_RB_RPTR) == cp.wptr ? "reached" : "NOT reached",
	    idle_rptr_us, idle_busy, G(A5XX_RBBM_STATUS),
	    G(A5XX_RBBM_INT_0_STATUS), G(A5XX_CP_RB_RPTR), cp.wptr,
	    mem_word(MEM_SHADOW), G(A5XX_CP_INTERRUPT_STATUS),
	    G(A5XX_CP_HW_FAULT), G(A5XX_CP_PROTECT_STATUS), smmu_fsr());
}

/*===========================================================================*
 *    hw_init                                                                *
 *===========================================================================*/
/* The A512's clock gating (a512_hwcg[], a5xx_gpu.c), register and value. */
static const struct {
	uint16_t reg;
	uint32_t val;
} a512_hwcg[] = {
	{ 0x0068, 0x02222222 }, { 0x0069, 0x02222222 },	/* CNTL_SP0/1 */
	{ 0x006c, 0x02222220 }, { 0x006d, 0x02222220 },	/* CNTL2_SP0/1 */
	{ 0x0070, 0x0000f3cf }, { 0x0071, 0x0000f3cf },	/* HYST_SP0/1 */
	{ 0x0074, 0x00000080 }, { 0x0075, 0x00000080 },	/* DELAY_SP0/1 */
	{ 0x004a, 0x22222222 }, { 0x004b, 0x22222222 },	/* CNTL_TP0/1 */
	{ 0x004e, 0x22222222 }, { 0x004f, 0x22222222 },	/* CNTL2_TP0/1 */
	{ 0x0052, 0x00002222 }, { 0x0053, 0x00002222 },	/* CNTL3_TP0/1 */
	{ 0x00b0, 0x77777777 }, { 0x00b1, 0x77777777 },	/* HYST_TP0/1 */
	{ 0x00b4, 0x77777777 }, { 0x00b5, 0x77777777 },	/* HYST2_TP0/1 */
	{ 0x00b8, 0x00007777 }, { 0x00b9, 0x00007777 },	/* HYST3_TP0/1 */
	{ 0x00a4, 0x11111111 }, { 0x00a5, 0x11111111 },	/* DELAY_TP0/1 */
	{ 0x00a8, 0x11111111 }, { 0x00a9, 0x11111111 },	/* DELAY2_TP0/1 */
	{ 0x00ac, 0x00001111 }, { 0x00ad, 0x00001111 },	/* DELAY3_TP0/1 */
	{ 0x005a, 0x22222222 }, { 0x005b, 0x22222222 },	/* CNTL(2)_UCHE */
	{ 0x005c, 0x22222222 }, { 0x005d, 0x00222222 },	/* CNTL3/4_UCHE */
	{ 0x005e, 0x00444444 }, { 0x005f, 0x00000002 },	/* HYST/DELAY_UCHE */
	{ 0x0078, 0x22222222 }, { 0x0079, 0x22222222 },	/* CNTL_RB0/1 */
	{ 0x007c, 0x00222222 }, { 0x007d, 0x00222222 },	/* CNTL2_RB0/1 */
	{ 0x0082, 0x00022220 }, { 0x0083, 0x00022220 },	/* CNTL_CCU0/1 */
	{ 0x008a, 0x05522222 }, { 0x008b, 0x00505555 },	/* CNTL(2)_RAC */
	{ 0x0086, 0x04040404 }, { 0x0087, 0x04040404 },	/* HYST_RB_CCU0/1 */
	{ 0x0080, 0x07444044 },				/* HYST_RAC */
	{ 0x008c, 0x00000002 }, { 0x008d, 0x00000002 },	/* DELAY_RB_CCU_L1 */
	{ 0x0081, 0x00010011 },				/* DELAY_RAC */
	{ 0x0063, 0x04222222 },				/* CNTL_TSE_RAS_RBBM */
	{ 0x0060, 0x02222222 }, { 0x0091, 0x00002222 },	/* MODE_GPC/VFD */
	{ 0x0064, 0x00000000 },				/* HYST_TSE_RAS_RBBM */
	{ 0x0062, 0x04104004 }, { 0x0090, 0x00000000 },	/* HYST_GPC/VFD */
	{ 0x0066, 0x00000000 },				/* DELAY_HLSQ */
	{ 0x0065, 0x00004000 },				/* DELAY_TSE_RAS_RBBM */
	{ 0x0061, 0x00000200 }, { 0x0092, 0x00002222 },	/* DELAY_GPC/VFD */
};

/* CP_PROTECT: 1 << 30 | 1 << 29 | log2(len) << 24 | reg << 2 - the base a
 * multiple of the length, which the hardware masks without a word. */
#define PROTECT(reg, len)	((1u << 30) | (1u << 29) | \
	((uint32_t)__builtin_ctz(len) << 24) | (((reg) << 2) & 0xfffff))

static const uint32_t a5xx_protect[] = {
	PROTECT(0x04, 4), PROTECT(0x08, 8), PROTECT(0x10, 16),
	PROTECT(0x20, 32), PROTECT(0x40, 64), PROTECT(0x80, 64),	/* RBBM */
	PROTECT(A5XX_RBBM_SECVID_TSB_TRUSTED_BASE, 16),	/* content */
	PROTECT(A5XX_RBBM_SECVID_TRUST_CNTL, 2),
	PROTECT(0x800, 64), PROTECT(0x840, 8), PROTECT(0x880, 32),
	PROTECT(0xaa0, 1),					/* CP */
	PROTECT(0xcc0, 1), PROTECT(0xcf0, 2),			/* RB */
	PROTECT(0xe68, 8), PROTECT(0xe70, 16),			/* VPC */
	PROTECT(0xe80, 16),					/* UCHE */
	PROTECT(0x10000, 0x8000),				/* SMMU */
};

static unsigned hw_writes;

static void
w(uint32_t reg, uint32_t v)
{
	W(reg, v);
	hw_writes++;
}

static void
w64(uint32_t reg, uint64_t v)
{
	w(reg, (uint32_t)v);
	w(reg + 1, (uint32_t)(v >> 32));
}

static void
rmw(uint32_t reg, uint32_t clear, uint32_t set)
{
	w(reg, (G(reg) & ~clear) | set);
}

static void
set_hwcg(void)
{
	unsigned i;

	for (i = 0; i < NELEM(a512_hwcg); i++)
		w(a512_hwcg[i].reg, a512_hwcg[i].val);
	w(A5XX_RBBM_CLOCK_CNTL, 0xaaa8aa00);
	w(A5XX_RBBM_ISDB_CNT, 0x182);
}

static void
hw_init(void)
{
	static const uint32_t addr_mode[] = { A5XX_CP_ADDR_MODE_CNTL,
	    A5XX_VSC_ADDR_MODE_CNTL, A5XX_GRAS_ADDR_MODE_CNTL,
	    A5XX_RB_ADDR_MODE_CNTL, A5XX_PC_ADDR_MODE_CNTL,
	    A5XX_HLSQ_ADDR_MODE_CNTL, A5XX_VFD_ADDR_MODE_CNTL,
	    A5XX_VPC_ADDR_MODE_CNTL, A5XX_UCHE_ADDR_MODE_CNTL,
	    A5XX_SP_ADDR_MODE_CNTL, A5XX_TPL1_ADDR_MODE_CNTL,
	    A5XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL };
	const uint64_t trap = 0x0001ffffffff0000ull;	/* UCHE_TRAP_BASE */
	const uint32_t gmem = 0x44000;			/* 256 + 16 KiB */
	const uint32_t hbb = 14 - 13;			/* highest bank bit 14 */
	unsigned i;

	hw_writes = 0;
	/* a5xx_pm_resume(), a GPU without a GPMU: the sp_input clock held
	 * while the gating is set, then let go. */
	w(A5XX_RBBM_CLOCK_CNTL, 0x00000055);
	set_hwcg();
	rmw(A5XX_RBBM_CLOCK_CNTL, 0xff, 0);

	/* a5xx_hw_init() */
	w(A5XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x00000003);
	w(A5XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000009);
	w(A5XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);
	w(A5XX_RBBM_AHB_CNTL0, 0x00000001);
	w(A5XX_RBBM_INTERFACE_HANG_INT_CNTL, (1u << 30) | 0xffff);
	w(A5XX_RBBM_PERFCTR_CNTL, 0x01);
	w(A5XX_CP_PERFCTR_CP_SEL_0, 0);		/* PERF_CP_ALWAYS_COUNT */
	w(A5XX_RBBM_PERFCTR_RBBM_SEL_0, 6);
	w(A5XX_UCHE_CACHE_WAYS, 0x02);
	w64(A5XX_UCHE_TRAP_BASE, trap);
	w64(A5XX_UCHE_WRITE_THRU_BASE, trap);
	w64(A5XX_UCHE_GMEM_RANGE_MIN, 0x00100000);
	w64(A5XX_UCHE_GMEM_RANGE_MAX, 0x00100000 + gmem - 1);
	w(A5XX_CP_MEQ_THRESHOLDS, 0x40);
	w(A5XX_CP_MERCIU_SIZE, 0x400);
	w(A5XX_CP_ROQ_THRESHOLDS_2, 0x80000060);
	w(A5XX_CP_ROQ_THRESHOLDS_1, 0x40201b16);
	w(A5XX_PC_DBG_ECO_CNTL, 0x200u << 11 | 0x200u << 22);
	rmw(A5XX_RB_DBG_ECO_CNTL, 0, 1u << 9);	/* one SP: no DP2 gating */
	w(A5XX_UCHE_MODE_CNTL, 1u << 29);
	w(A5XX_CP_CHICKEN_DBG, 0x02000000);
	w(A5XX_RBBM_AHB_CNTL1, 0xa6ffffff);
	set_hwcg();
	w(A5XX_RBBM_AHB_CNTL2, 0x0000003f);
	w(A5XX_TPL1_MODE_CNTL, hbb << 7);
	w(A5XX_RB_MODE_CNTL, hbb << 1);
	w(A5XX_UCHE_DBG_ECO_CNTL_2, hbb);
	rmw(A5XX_VPC_DBG_ECO_CNTL, 0, 1u << 10);
	w(A5XX_CP_PROTECT_CNTL, 0x00000007);
	for (i = 0; i < NELEM(a5xx_protect); i++)
		w(A5XX_CP_PROTECT(i), a5xx_protect[i]);
	w(A5XX_RBBM_SECVID_TSB_CNTL, 0);
	w64(A5XX_RBBM_SECVID_TSB_TRUSTED_BASE, 0);
	w(A5XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0);
	for (i = 0; i < NELEM(addr_mode); i++)
		w(addr_mode[i], 0x1);
	/* ADRENO_QUIRK_LMLOADKILL_DISABLE, the A512's one quirk. */
	rmw(A5XX_VPC_DBG_ECO_CNTL, 0, 1u << 23);
	rmw(A5XX_HLSQ_DBG_ECO_CNTL, 1u << 18, 0);
}

/*===========================================================================*
 *    the interrupt                                                          *
 *===========================================================================*/
/* As adreno_smmu.c waits for its line: 1 came, 0 not in time, -1 error. */
static int
wait_gpu_irq(clock_t ticks)
{
	message m;
	clock_t up;
	int st;

	if (sys_setalarm2(ticks, 0, NULL, &up) != OK)
		return -1;
	for (;;) {
		if (ipc_receive(ANY, &m, &st) != OK)
			return -1;
		if (!is_ipc_notify(st))
			continue;
		if (_ENDPOINT_P(m.m_source) == HARDWARE &&
		    (m.m_notify.interrupts & (1ull << GPU_HOOK))) {
			(void)sys_setalarm(0, 0);
			return 1;
		}
		if (_ENDPOINT_P(m.m_source) == CLOCK &&
		    m.m_notify.timestamp >= up + ticks)
			return 0;
	}
}

/*===========================================================================*
 *    cp_run                                                                 *
 *===========================================================================*/
int
cp_run(void)
{
	const char *why;
	uint32_t pm4w[4], pfpw[4], st, fence, scratch, secvid;
	unsigned us;
	int r, got;

	memset(&cp, 0, sizeof(cp));
	if (!cc_gpu_clocked(&why)) {
		log_warn(&ad_log, "Q19 cp: %s is off; refusing\n", why);
		return EIO;
	}
	if (!ad_route) {
		log_warn(&ad_log, "Q19 cp: route=0 - the GPU would reach no "
		    "memory; refusing\n");
		return EINVAL;
	}

	/* 1. Memory, the microcode in it, the route. */
	if ((r = ucode("a530_pm4.fw", &cp.pm4, VA_PM4, pm4w,
	    &cp.pm4_len)) != OK ||
	    (r = ucode("a530_pfp.fw", &cp.pfp, VA_PFP, pfpw,
	    &cp.pfp_len)) != OK)
		return r;
	/* Patched microcode knows WHERE_AM_I: the low nibble of word 1 is
	 * 0xa, of word 3 the patch level (a5xx_ucode_check_version()). */
	cp.whereami = (pfpw[1] & 0xf) == 0xa && (pfpw[3] & 0xf) >= 1;
	if ((r = gbuf_new(&cp.ring, A5XX_RING_SIZE, VA_RING, AD_PT_READ)) !=
	    OK || (r = gbuf_new(&cp.mem, 4096, VA_MEM,
	    AD_PT_READ | AD_PT_WRITE)) != OK) {
		log_warn(&ad_log, "Q19 cp: no memory for the ring: %d\n", r);
		return r;
	}
	log_info(&ad_log, "Q19 cp: pm4 %zu bytes at %llx (%08x %08x), pfp "
	    "%zu at %llx (%08x %08x)%s; ring %llx, memptrs %llx\n",
	    cp.pm4_len, (unsigned long long)VA_PM4, pm4w[1], pm4w[3],
	    cp.pfp_len, (unsigned long long)VA_PFP, pfpw[1], pfpw[3],
	    cp.whereami ? ", where-am-i" : "", (unsigned long long)VA_RING,
	    (unsigned long long)VA_MEM);
	if ((r = smmu_attach()) != OK)
		return r;

	/* 2. The registers, as Linux leaves them before the start. */
	hw_init();
	log_info(&ad_log, "Q19 cp: hw_init, %u writes; cp_protect_cntl %x "
	    "uche_cache_ways %x pc_dbg_eco %08x\n", hw_writes,
	    G(A5XX_CP_PROTECT_CNTL), G(A5XX_UCHE_CACHE_WAYS),
	    G(A5XX_PC_DBG_ECO_CNTL));

	/* 3. Where things are, and the start. */
	W(A5XX_CP_ME_INSTR_BASE, (uint32_t)VA_PM4);
	W(A5XX_CP_ME_INSTR_BASE + 1, (uint32_t)(VA_PM4 >> 32));
	W(A5XX_CP_PFP_INSTR_BASE, (uint32_t)VA_PFP);
	W(A5XX_CP_PFP_INSTR_BASE + 1, (uint32_t)(VA_PFP >> 32));
	W(A5XX_CP_RB_BASE, (uint32_t)VA_RING);
	W(A5XX_CP_RB_BASE + 1, (uint32_t)(VA_RING >> 32));
	W(A5XX_CP_RB_CNTL, A5XX_CP_RB_CNTL_VALUE);
	if (cp.whereami) {
		W(A5XX_CP_RB_RPTR_ADDR, (uint32_t)(VA_MEM + MEM_SHADOW));
		W(A5XX_CP_RB_RPTR_ADDR + 1,
		    (uint32_t)((VA_MEM + MEM_SHADOW) >> 32));
	}
	W(A5XX_RBBM_INT_0_MASK, A5XX_INT_MASK);
	W(A5XX_CP_PFP_ME_CNTL, 0);

	/* 4. CP_ME_INIT (a5xx_me_init(), an A512: no microcode workarounds). */
	out(pm4_pkt7(CP_ME_INIT, 8));
	out(0x0000002f);		/* the fields that follow */
	out(0x00000003);		/* several hardware contexts */
	out(0x20000000);		/* error detection */
	out(0x00000000);		/* no header dump */
	out(0x00000000);
	out(0x00000000);		/* no workarounds */
	out(0x00000000);
	out(0x00000000);
	flush();
	if (idle(&us) != OK) {
		say_stuck("me_init");
		return ETIMEDOUT;
	}
	log_info(&ad_log, "Q20 cp: me_init done, idle in %u us (read in %u, "
	    "busy %x): rptr %x wptr %x shadow %x, rbbm %08x int %08x\n", us,
	    idle_rptr_us, idle_busy, G(A5XX_CP_RB_RPTR), cp.wptr,
	    mem_word(MEM_SHADOW), G(A5XX_RBBM_STATUS),
	    G(A5XX_RBBM_INT_0_STATUS));

	/* 5. The zap shader, then out of secure mode from the ring. */
	if ((r = zap_load()) != OK)
		return r;
	out(pm4_pkt7(CP_SET_SECURE_MODE, 1));
	out(0x00000000);
	flush();
	if (idle(&us) != OK) {
		say_stuck("set_secure_mode");
		return ETIMEDOUT;
	}
	secvid = G(A5XX_RBBM_SECVID_TRUST_CNTL);
	log_info(&ad_log, "Q21 cp: set_secure_mode 0, idle in %u us; secvid "
	    "%08x, rbbm %08x int %08x\n", us, secvid, G(A5XX_RBBM_STATUS),
	    G(A5XX_RBBM_INT_0_STATUS));

	/* 6. The first command: a scratch register, a fence, the line. */
	cp.hook = GPU_HOOK;
	if ((r = sys_irqsetpolicy(ad.dt.irq, 0, &cp.hook)) != OK) {
		log_warn(&ad_log, "Q22 line %d: irqsetpolicy %d\n", ad.dt.irq,
		    r);
		return r;
	}
	cp.hooked = 1;
	(void)sys_irqenable(&cp.hook);
	W(A5XX_RBBM_INT_CLEAR_CMD, G(A5XX_RBBM_INT_0_STATUS) &
	    ~A5XX_INT_AHB_ERROR);
	out(pm4_pkt4(A5XX_CP_SCRATCH_REG(2), 1));
	out(1);
	out(pm4_pkt7(CP_EVENT_WRITE, 4));
	out(CP_EVENT_WRITE_IRQ | CACHE_FLUSH_TS);
	out((uint32_t)(VA_MEM + MEM_FENCE));
	out((uint32_t)((VA_MEM + MEM_FENCE) >> 32));
	out(1);
	flush();
	got = wait_gpu_irq((clock_t)sys_hz());
	st = G(A5XX_RBBM_INT_0_STATUS);
	W(A5XX_RBBM_INT_CLEAR_CMD, st & ~A5XX_INT_AHB_ERROR);
	fence = mem_word(MEM_FENCE);
	scratch = G(A5XX_CP_SCRATCH_REG(2));
	r = idle(&us);
	log_info(&ad_log, "Q22 line %d %s: int %08x, fence %u in memory, "
	    "scratch2 %u, %s; smmu fsr %x\n", ad.dt.irq,
	    got == 1 ? "came" : "did NOT come", st, fence, scratch,
	    r == OK ? "idle" : "NOT idle", smmu_fsr());
	if (got == 1 && (st & A5XX_INT_CP_CACHE_FLUSH_TS) && fence == 1 &&
	    scratch == 1 && r == OK)
		log_info(&ad_log, "Q22 the GPU ran its first command: the "
		    "line, the fence and the scratch register say so\n");
	else {
		log_warn(&ad_log, "Q22 the first command did NOT come back as "
		    "it should\n");
		return EIO;
	}
	return OK;
}

/*
 * After cp_run(), whatever it came to, while the GPU and its SMMU are
 * still powered: nothing more raised, nothing in flight on the VBIF's
 * ports (a5xx_pm_suspend()).
 */
void
cp_stop(void)
{
	unsigned t;
	uint32_t got = 0;

	if (cp.ring.cpu == NULL && !cp.hooked)
		return;
	W(A5XX_RBBM_INT_0_MASK, 0);
	if (cp.hooked) {
		(void)sys_irqrmpolicy(&cp.hook);
		cp.hooked = 0;
	}
	W(A5XX_VBIF_XIN_HALT_CTRL0, 0xf);
	for (t = 0; t < 100000; t += 10) {
		if (((got = G(A5XX_VBIF_XIN_HALT_CTRL1)) & 0xf) == 0xf)
			break;
		micro_delay(10);
	}
	W(A5XX_VBIF_XIN_HALT_CTRL0, 0);
	log_info(&ad_log, "Q23 cp: interrupts masked, vbif ports %s (%x)\n",
	    (got & 0xf) == 0xf ? "halted" : "NOT halted", got & 0xf);
}

/* After smmu_undo(): the bank no longer points at any of it. */
void
cp_free(void)
{
	gbuf_free(&cp.pm4);
	gbuf_free(&cp.pfp);
	gbuf_free(&cp.ring);
	gbuf_free(&cp.mem);
}
