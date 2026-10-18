/*
 * qcom_cpufreq: the Mi Pad 4's CPU clusters (Qualcomm SDM660) set to a
 * fixed frequency that the rails, as the boot loader left them, already
 * hold - path A of port/clover/hw-cpu.md, section 9.  Frequency only:
 * nothing is ever written to the PMIC, the SAWs, the APM or MEM-ACC; the
 * only registers written are each cluster's mux (an RCG in its APCS GLB
 * block) and its PLL.
 *
 *   cpufreq.c   the service's life: arguments, SEF, the stage
 *   cf_find.c   the blocks in the device tree, mapped
 *   cf_volt.c   the fuses -> each corner's open-loop voltage (the
 *               arithmetic of port/clover/cpu-volt.py, which is its
 *               reference)
 *   cf_rail.c   the rails' set points, read through the SPMI arbiter's
 *               observer channels (qcom_pmic's spmi.c)
 *   cf_clk.c    one cluster: its state, the checks, the mux and the PLL
 *   cf_run.c    the stages: probe (reads only) and set
 *
 * Everything but cpufreq.c also builds on a host: the stand
 * (port/test/qcom_cpufreq) runs these files against a model of the PLLs,
 * the muxes, the rails and the fuses.
 */
#ifndef _QCOM_CPUFREQ_H
#define _QCOM_CPUFREQ_H

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>

#include "spmi.h"

extern struct log cf_log;

/* A block of registers, mapped once. */
struct cf_region {
	volatile uint8_t *base;
	size_t size;
	u64_t pa;
	const char *name;
};

/*
 * Every register access is one plain LDR or STR from a general register,
 * as adreno.h's accessors and for its reason: on this SoC a hypervisor
 * emulates some registers from the syndrome a trap leaves, and does not
 * decode a store from the zero register (hw-gpu.md, section 16).  Whether
 * it traps these blocks is not known; the accessor costs nothing either way.
 */
static inline uint32_t
cf_rd(const struct cf_region *r, uint32_t off)
{
	uint32_t v;

	__asm__ volatile("ldr %w0, [%1]" : "=r" (v) :
	    "r" (r->base + off) : "memory");
	return v;
}

static inline void
cf_wr(const struct cf_region *r, uint32_t off, uint32_t v)
{
	__asm__ volatile("str %w0, [%1]" : : "r" (v),
	    "r" (r->base + off) : "memory");
}

/* The clusters (hw-cpu.md, section 2). */
#define CF_SILVER	0	/* cores 0-3, power cluster, VDD_APC0 = PM660 S1 */
#define CF_GOLD		1	/* cores 4-7, perf cluster, VDD_APC1 = S2+S3 */
#define CF_NCLUSTER	2

/*
 * The voltage corners of one cluster for this chip (cf_volt.c), from its
 * fuses and the vendor tree's tables: frequency in kHz, open-loop voltage
 * (which the vendor tree also makes the ceiling) and floor in uV.
 */
#define CF_MAX_CORNERS	8
#define CF_FUSE_CORNERS	5
#define CF_FUSE_ROWS	72	/* rows 0..71 of the CPR fuse block; 38, 65-71 read */

struct cf_corners {
	unsigned speed_bin, cpr_rev;
	unsigned n;				/* corners */
	uint32_t khz[CF_MAX_CORNERS];
	int32_t ol_uv[CF_MAX_CORNERS];
	int32_t floor_uv[CF_MAX_CORNERS];
	unsigned fuse_corner[CF_MAX_CORNERS];	/* of each corner */
	unsigned raw[CF_FUSE_CORNERS];		/* the fuse fields */
	int32_t fused_uv[CF_FUSE_CORNERS];	/* before the tree's adjustment */
	const char *fc_name[CF_FUSE_CORNERS];
};

int cf_volt(int cluster, const uint64_t rows[CF_FUSE_ROWS],
	struct cf_corners *c);
int cf_corner_of(const struct cf_corners *c, uint32_t khz);

/* A rail: an FTS426 buck of PM660 (hw-cpu.md, section 6). */
struct cf_rail {
	const char *name;
	unsigned sid, base;
};

#define CF_MAX_RAILS	2

/* What a cluster's mux and PLL say (cf_clk.c). */
struct cf_state {
	uint32_t cmd, cfg;			/* the RCG */
	uint32_t mode, l, user, config, status;	/* the PLL */
	int on_pll;				/* the cores run from the PLL */
	uint32_t khz;				/* and at this rate; 0: unknown */
	const char *why;			/* why unknown */
};

struct cf_cluster {
	int id;
	const char *name;
	struct cf_region glb, pll;	/* RCG at +0x50/+0x54; the PLL */
	struct cf_rail rail[CF_MAX_RAILS];
	unsigned nrail;
	struct cf_corners c;
};

/* Everything the service found and mapped (cf_find.c). */
struct cf {
	struct cf_cluster cl[CF_NCLUSTER];
	struct cf_region fuse;		/* the qfprom page of the CPR rows */
	struct cf_region gcc;		/* the GCC page of HMSS_GPLL0_CLK_SRC */
	struct spmi spmi;
	uint32_t aux_khz;		/* the clusters' "aux" source; 0: off */
	unsigned steps;			/* climb through the table's points */
	unsigned hold;			/* seconds at each point (cf_pause()) */
};

#define CF_XO_KHZ	19200		/* the PLLs' reference: L counts it */
#define CF_APM_UV	872000		/* qcom,apm-threshold-voltage */

/* cf_find.c */
int cf_find(const void *dtb, struct cf *cf);
int cf_fuse_rows(struct cf *cf, uint64_t rows[CF_FUSE_ROWS]);
int cf_aux(struct cf *cf);

/* cf_rail.c */
int cf_rail_mv(struct spmi *s, const struct cf_rail *r, unsigned *mv);
int cf_rails_mv(struct cf *cf, const struct cf_cluster *cl, unsigned *mv);

/* cf_clk.c */
int cf_state(const struct cf *cf, const struct cf_cluster *cl,
	struct cf_state *st);
int cf_held(struct cf *cf, const struct cf_cluster *cl, uint32_t khz,
	int quiet);
int cf_go(struct cf *cf, struct cf_cluster *cl, uint32_t khz);
int cf_highest(struct cf *cf, const struct cf_cluster *cl, uint32_t *khz);
uint32_t cf_point_l(uint32_t khz);

/* cf_run.c */
#define CF_PROBE	0	/* read everything, say what set would do */
#define CF_SET		1	/* set the clusters named */

#define CF_TARGET_AUX	1	/* "aux": the cluster on GPLL0 aux */
#define CF_TARGET_MAX	2	/* "max": the highest point the rail holds */
int cf_parse_target(const struct cf_corners *c, const char *s,
	uint32_t *khz);
int cf_run(struct cf *cf, int stage, const char *const target[CF_NCLUSTER]);

/* cpufreq.c on the machine, the stand's own on a host. */
void cf_delay_us(unsigned us);
void cf_pause(unsigned seconds);

#endif /* _QCOM_CPUFREQ_H */
