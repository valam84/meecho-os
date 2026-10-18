/*
 * The model behind the qcom_cpufreq stand (model.c): the SDM660's two CPU
 * clusters as port/clover/hw-cpu.md describes them - each one's mux (an
 * RCG with UPDATE) and PLL (locks only after MODE 2 -> 6 with L in the VCO's
 * range), the GCC's aux source, the fuses, PM660's three CPU rails behind
 * the SPMI arbiter, and the blocks nothing may write (OSM, CPRh, APM,
 * SAWs, the PMIC itself).
 *
 * The model judges as it goes.  A violation is recorded the moment the
 * service commits it and the run carries on; the stand fails a scenario
 * that has any.  The model's own idea of what a rate needs is the
 * reference table of the scenario's fuses (volt-ref.txt, made by
 * port/clover/cpu-volt.py), not the service's arithmetic.
 */
#ifndef _MODEL_H
#define _MODEL_H

#include <stddef.h>
#include <stdint.h>

#define M_SILVER	0
#define M_GOLD		1

/* One fuse set of volt-ref.txt: rows, and each cluster's corners. */
struct fuse_set {
	char name[64];
	uint64_t row[72];
	int has[2];			/* 0: no corners for this bin */
	unsigned bin[2], n[2];
	uint32_t khz[2][8];
	int32_t ol[2][8], fl[2][8];
};

/* What the tablet had, unless a scenario says otherwise (hw-cpu.md, s. 8). */
struct scenario {
	const char *name;
	const char *fuses;		/* a set of volt-ref.txt; NULL: tablet */
	uint32_t cfg[2];		/* the muxes (0: 0x501, 0x401) */
	uint32_t l[2];			/* the PLLs' L (0: 0x48, 0x20) */
	unsigned mv[3];			/* S1, S2, S3 (0: 872, 868, 868) */
	int aux_off;			/* HMSS_GPLL0's root off */
	uint32_t aux_cfg;		/* its CFG (0: 0x100, GPLL0 / 1) */
	int pll_dead[2];		/* never locks */
	uint32_t vco_max[2];		/* locks only up to this L (0: 0x80) */
	int mux_stuck[2];		/* UPDATE never clears */
	int l_ro[2];			/* writes to L dropped */
	uint8_t s1_subtype;		/* 0: 0x0a (FTS426) */
	int s3_disabled;
	int spmi_dead;			/* every read times out */
	/* The rail drops: when cluster drop_cl's PLL locks at L drop_l, or
	 * when its mux has gone to the PLL drop_after times, regulator
	 * drop_reg (0 S1, 1 S2, 2 S3) is set to drop_mv. */
	int drop_cl;
	uint32_t drop_l;
	unsigned drop_after, drop_reg, drop_mv;

	/* the run */
	int stage;			/* CF_PROBE, CF_SET */
	const char *silver, *gold;	/* targets */
	int steps;			/* -1: 0; 0: the default, 1 */

	/* what must come of it */
	int fail;			/* cf_run() must fail */
	uint32_t want_khz[2];		/* each cluster at the end; 0: as at
					 * the start */
	int no_writes;			/* not a register written */
	const char *want_l[2];		/* the Ls the PLL locked at, in order:
					 * "3a 49 5b"; "" none; NULL: no check */
	const char *want_line;		/* a log line containing this */
	const char *want_line2;
};

extern int model_verbose;

int model_load_fuses(const char *path);
const struct fuse_set *model_fuse_set(const char *name);
unsigned model_fuse_sets(void);
const struct fuse_set *model_fuse_set_at(unsigned i);

void model_reset(const struct scenario *s);
const char *model_violation(void);
void model_end_check(void);
unsigned model_writes(void);
uint32_t model_khz(int cl);		/* what the cores of a cluster run at */
const char *model_l_history(int cl);	/* "3a 49 5b" */
int model_saw(const char *needle);
void model_log_reset(void);
void model_log_dump(void);
int model_held(int cl, uint32_t khz);	/* by the reference, the set points */
unsigned model_setpoint(int cl);

#endif
