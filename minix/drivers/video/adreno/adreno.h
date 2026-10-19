/*
 * The Adreno GPU service (port/clover/hw-gpu.md).  So far the first step of
 * its plan: what the SoC has around the GPU, read in an order that never
 * touches a block before its clock and power domain say it may be touched
 * - on this SoC a read of an unclocked block can stall the bus - and, by
 * stages, the domains brought up at the crystal's rate and the GPU and its
 * SMMU read.  Every answer is a line "Qn ...", the questions of hw-gpu.md
 * §12; nothing is left switched on that was not on before.
 *
 *   adreno.c       the service's life: arguments, SEF, the stage it runs
 *   adreno_find.c  the GPU, its SMMU, clock controller and zap shader in
 *                  the device tree
 *   adreno_run.c   the stages, in order, each one gated on the last
 *   adreno_cc.c    GCC and GPUCC: looks, clocks, power domains, undo
 *   adreno_hw.c    the GPU's and the SMMU's own registers, read
 *   adreno_smmu.c  the SMMU written: a stream-matching entry, context bank
 *                  0 with tables the SMMU itself translates through, the
 *                  bank's fault line - and all of it put back
 *   adreno_pt.c    the GPU's page tables (LPAE stage 1, 4 KiB pages)
 *   adreno_tz.c    TrustZone: is the way to the zap shader there
 *   adreno_fw.c    firmware files, read from the root (fwdir=)
 *   adreno_mdt.c   a Qualcomm MDT image understood (plain C, the stand's too)
 *   adreno_zap.c   the zap shader loaded by TrustZone, or resumed
 *   adreno_cp.c    the command processor: hw_init, the ring, its first
 *                  command
 */
#ifndef _ADRENO_H
#define _ADRENO_H

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/fdt.h>

#include <stdint.h>

extern struct log ad_log;

/* A block of registers, mapped once. */
struct ad_region {
	volatile uint8_t *base;
	size_t size;
	u64_t pa;
	const char *name;
};

/*
 * Every register access is one plain LDR or STR: a base register, no
 * offset, no writeback, the value in a general register - never the zero
 * register.  On this SoC a hypervisor traps some of these registers (the
 * SMMU's stream-matching table) and emulates the access from the
 * syndrome the trap leaves (ESR_EL2: ISV, SRT); that much any emulator
 * decodes.  What this one does not: a store from register 31.  The C
 * accessors let GCC write a constant 0 as "str wzr" (and with register
 * offsets), and on the Mi Pad 4 the S2CR write "translate, bank 0" -
 * a zero - so made stopped the whole machine twice, while non-zero
 * values written from ordinary registers were taken (hw-gpu.md, §16).
 * Linux's accessors are this same shape (asm "str %w0, [%1]"); "r" and
 * not "rZ", so that zero too comes from a real register.
 */
static inline uint32_t
ad_rd(const struct ad_region *r, uint32_t off)
{
	uint32_t v;

	__asm__ volatile("ldr %w0, [%1]" : "=r" (v) :
	    "r" (r->base + off) : "memory");
	return v;
}

static inline void
ad_wr(const struct ad_region *r, uint32_t off, uint32_t v)
{
	__asm__ volatile("str %w0, [%1]" : : "r" (v),
	    "r" (r->base + off) : "memory");
}

/*
 * The SMMU's 64-bit registers, one access each: a translation operation
 * (ATS1*) starts on its write, and Linux writes those whole.
 */
static inline uint64_t
ad_rd64(const struct ad_region *r, uint32_t off)
{
	uint64_t v;

	__asm__ volatile("ldr %x0, [%1]" : "=r" (v) :
	    "r" (r->base + off) : "memory");
	return v;
}

static inline void
ad_wr64(const struct ad_region *r, uint32_t off, uint64_t v)
{
	__asm__ volatile("str %x0, [%1]" : : "r" (v),
	    "r" (r->base + off) : "memory");
}

#define AD_MAXCLK	8
#define AD_MAXIRQ	16
#define AD_MAXOPP	16

/* Who provides a clock or a power domain. */
#define AD_GCC		1
#define AD_GPUCC	2
#define AD_OTHER	3

struct ad_clk {
	char name[24];		/* clock-names entry */
	uint32_t phandle;
	int prov;		/* AD_* once the providers are known */
	unsigned id;
};

/* What the device tree says (adreno_find.c). */
struct adreno_dt {
	char node[48];		/* "gpu@5000000" */
	char compat[32];	/* "qcom,adreno-512.0" */
	uint32_t chip_id;	/* 0x05010200, from compat */
	u64_t gpu_pa, gpu_sz;
	int irq;
	struct ad_clk clk[AD_MAXCLK];
	unsigned nclk;
	uint32_t pd_phandle;	/* the GPU's own domain: rpmpd (VDDMX) */
	unsigned pd_id;

	uint32_t gcc_ph;	/* the GCC: qcom_soc maps and knows it */
	u64_t gcc_pa, gcc_sz;
	uint32_t gpucc_ph;
	u64_t gpucc_pa, gpucc_sz;
	char gpucc_compat[32];
	struct ad_clk gpucc_in[AD_MAXCLK];	/* the GPUCC's own inputs */
	unsigned gpucc_nin;

	uint32_t smmu_ph;
	unsigned sid;
	char smmu_node[48];
	u64_t smmu_pa, smmu_sz;
	int smmu_irq[AD_MAXIRQ];
	unsigned smmu_nirq, smmu_nglobal;
	struct ad_clk smmu_clk[AD_MAXCLK];
	unsigned smmu_nclk;
	int smmu_pd;		/* GPUCC domain number, -1 none */

	char zap_fw[32];
	uint32_t zap_ph;
	u64_t zap_pa, zap_sz;

	/* The speed bin's fuse (nvmem-cells "speed_bin"): a cell of qfprom. */
	uint32_t bin_ph;
	int bin_found;
	u64_t bin_base;		/* the qfprom block's address */
	uint32_t bin_off, bin_len, bin_shift, bin_bits;

	unsigned nopp, opp_max_mhz;
	struct {			/* the first AD_MAXOPP, in the tree's order */
		unsigned mhz;
		unsigned level;		/* opp-level: the RPM's, for VDDMX */
		uint32_t hw;		/* opp-supported-hw (~0: none said) */
	} opp[AD_MAXOPP];
};

struct adreno {
	struct adreno_dt dt;
	struct ad_region gcc, gpucc, gpu, smmu;
	struct ad_region qfprom;	/* one page: the speed bin's fuse */
};
extern struct adreno ad;

/* Stages, each one including the ones before. */
#define AD_PROBE	0	/* only reads, only what is clocked */
#define AD_CX		1	/* + GCC's branches and the CX domain */
#define AD_GX		2	/* + the GX domain, its clocks at 19.2 MHz */
#define AD_GPU		3	/* + the GPU's registers, one scratch write */
#define AD_SMMU		4	/* + the SMMU's registers, read only */
#define AD_SMR		5	/* + a free SMR and its S2CR written, put back */
#define AD_CB0		6	/* + context bank 0 with tables, translations by
				 * the SMMU itself, SID 0 routed to it a while */
#define AD_FAULT	7	/* + a fault raised in the bank: its line */
#define AD_ZAP		8	/* + the zap shader to TrustZone (or resumed) */
#define AD_CP		9	/* + the command processor started, out of
				 * secure mode, its first command */

/* adreno_find.c */
int adreno_find(const void *dtb, struct adreno_dt *dt);
void adreno_find_report(const struct adreno_dt *dt);

/* adreno_run.c */
int adreno_run(const void *dtb, int stage, int tz);
int adreno_map(const void *dtb);
int adreno_speed_bin(unsigned *fuse);	/* OK: the fuse's value */

/* adreno_rpm.c (the host stand: model.c) */
int ad_rpm_set(uint32_t key, uint32_t value);	/* VDDMX, active set */

/* adreno_cc.c */
int cc_gcc_on(unsigned id, uint32_t *reg);
int cc_report(void);			/* Q1..Q3; OK if GPUCC was readable */
int cc_power_up(int stage);		/* Q4 */
void cc_power_down(void);		/* Q11 */
int cc_gpu_clocked(const char **why);
int cc_smmu_clocked(const char **why);
int cc_gfx3d_set(unsigned mhz);		/* Q24: the core's clock */
uint32_t cc_gfx3d_read_khz(void);	/* the same, from the registers */
unsigned cc_gfx3d_mhz(void);

/* adreno_hw.c */
void hw_gpu_report(void);		/* Q5..Q8 */
void hw_smmu_report(void);		/* Q9, Q10 */

/* adreno_tz.c */
void tz_report(void);			/* Q12 */

/* adreno_fw.c */
extern char ad_fwdir[64];		/* fwdir= (adreno.c) */
int fw_read(const char *name, void **buf, size_t *len, size_t max);
#define FW_SUM0		2166136261u
uint32_t fw_sum(const void *p, size_t n, uint32_t h);

/* adreno_zap.c */
int zap_load(void);			/* Q18 */

/* adreno_cp.c */
int cp_run(void);			/* Q19..Q22: the three below */
int cp_alloc(void);			/* Q19: microcode and buffers */
int cp_boot(void);			/* Q19..Q21: started, out of secure mode */
int cp_first(void);			/* Q22: a first command and its line */
void cp_stop(void);			/* Q23 */
int cp_halt(void);			/* the same, the line kept (recovery) */
void cp_free(void);
uint32_t pm4_pkt7(uint32_t op, uint32_t cnt);
uint32_t pm4_pkt4(uint32_t reg, uint32_t cnt);
uint32_t cp_wptr(void);
uint32_t cp_ring_dwords(void);
void cp_emit(uint32_t v);
void cp_kick(void);
uint32_t cp_kick_dwords(void);
uint32_t cp_seq_next(void);
uint64_t cp_fence_va(void);
uint32_t cp_fence(void);
int cp_quiet(void);
uint32_t cp_irq(void);
void cp_say(const char *what);
uint64_t cp_timestamp(void);

/* adreno_smmu.c */
int smmu_smr_test(void);		/* Q13 */
int smmu_cb0(int fault);		/* Q14..Q16 */
extern int ad_route;			/* route=0: no stream routed (adreno.c) */
void smmu_undo(void);			/* Q17 */
int smmu_gpu_map(uint64_t va, phys_bytes pa, size_t len, int prot);
int smmu_attach(void);			/* SID 0 to bank 0 until smmu_undo() */
uint32_t smmu_fsr(void);

/* adreno_pt.c: one address space of the GPU. */
#define AD_PT_READ	1
#define AD_PT_WRITE	2
struct ad_ptab;
struct ad_pt {
	struct ad_ptab *root;
	unsigned ias, oas;		/* input and output address bits */
	int start;			/* the level the walk starts at */
};
int pt_init(struct ad_pt *pt, unsigned ias, unsigned oas);
int pt_map(struct ad_pt *pt, uint64_t va, uint64_t pa, int prot);
int pt_unmap(struct ad_pt *pt, uint64_t va);
uint64_t pt_root(const struct ad_pt *pt);
void pt_free(struct ad_pt *pt);

/* adreno_smmu.c, serving: bank 0 kept, the clients' tables switched. */
int smmu_serve(void);
int smmu_pt_new(struct ad_pt *pt);
int smmu_pt_free(struct ad_pt *pt);
int smmu_pt_map(struct ad_pt *pt, uint64_t va, const phys_bytes *pages,
	size_t n, int prot);
int smmu_pt_unmap(struct ad_pt *pt, uint64_t va, size_t n);
int smmu_load(struct ad_pt *pt);
struct ad_pt *smmu_loaded(void);
int smmu_fault(uint32_t *fsr, uint64_t *far, uint32_t *fsynr0);
int smmu_check(void);

#endif /* _ADRENO_H */
