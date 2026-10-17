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

static inline uint32_t
ad_rd(const struct ad_region *r, uint32_t off)
{
	return *(volatile uint32_t *)(r->base + off);
}

static inline void
ad_wr(const struct ad_region *r, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(r->base + off) = v;
}

/*
 * The SMMU's 64-bit registers, one access each: a translation operation
 * (ATS1*) starts on its write, and Linux writes those whole.
 */
static inline uint64_t
ad_rd64(const struct ad_region *r, uint32_t off)
{
	return *(volatile uint64_t *)(r->base + off);
}

static inline void
ad_wr64(const struct ad_region *r, uint32_t off, uint64_t v)
{
	*(volatile uint64_t *)(r->base + off) = v;
}

#define AD_MAXCLK	8
#define AD_MAXIRQ	16

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

	unsigned nopp, opp_max_mhz;
};

struct adreno {
	struct adreno_dt dt;
	struct ad_region gcc, gpucc, gpu, smmu;
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

/* adreno_find.c */
int adreno_find(const void *dtb, struct adreno_dt *dt);
void adreno_find_report(const struct adreno_dt *dt);

/* adreno_run.c */
int adreno_run(const void *dtb, int stage, int tz);
int adreno_map(const void *dtb);

/* adreno_cc.c */
int cc_gcc_on(unsigned id, uint32_t *reg);
int cc_report(void);			/* Q1..Q3; OK if GPUCC was readable */
int cc_power_up(int stage);		/* Q4 */
void cc_power_down(void);		/* Q11 */
int cc_gpu_clocked(const char **why);
int cc_smmu_clocked(const char **why);

/* adreno_hw.c */
void hw_gpu_report(void);		/* Q5..Q8 */
void hw_smmu_report(void);		/* Q9, Q10 */

/* adreno_tz.c */
void tz_report(void);			/* Q12 */

/* adreno_smmu.c */
int smmu_smr_test(void);		/* Q13 */
int smmu_cb0(int fault);		/* Q14..Q16 */
extern int ad_route;			/* route=0: no stream routed (adreno.c) */
void smmu_undo(void);			/* Q17 */

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

#endif /* _ADRENO_H */
