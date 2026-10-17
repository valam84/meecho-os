/*
 * Модель SoC вокруг Adreno 512 для хостового стенда (model.c): GCC, GPUCC,
 * окно GPU и SMMU — с правилом этой SoC, что обращение к блоку без такта
 * или питания вешает шину.  Такое обращение уходит longjmp() в
 * model_hang: сценарий провален, как провалился бы прогон на планшете.
 * Что внутри SMMU и памяти, которую он обходит, — smmumodel.c.
 */
#ifndef _MODEL_H
#define _MODEL_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

struct scenario {
	const char *name;
	int iface_on;		/* GCC_GPU_CFG_AHB_CLK at the start */
	int bimc_on;		/* GCC_BIMC_GFX_CLK, GCC_GPU_BIMC_GFX_CLK */
	int cx_on, gx_on;	/* the GPUCC's domains at the start */
	int clocks_on;		/* its branches on, their roots at the crystal */
	int gx_dead;		/* GX never powers (no rail) */
	int root_stuck;		/* gfx3d's root never finishes an update */
	int tz;			/* 0: no qcom,scm (ENODEV); 1: it answers yes */
	/* the SMMU (smmumodel.c) */
	int smmu_tablet;	/* as the tablet answered Q9/Q10; else a
				 * made-up one with SID 0 routed to bank 0 */
	int smr_ro;		/* SMR/S2CR writes dropped (someone above) */
	int no_line;		/* bank 0's fault never reaches its line */
	int cb0_busy;		/* bank 0 enabled by someone at the start */
	int stale_tlb;		/* a stale TLB entry for ASID 0, 16 MiB */
};

extern jmp_buf model_hang;
extern int model_verbose;

void model_reset(const struct scenario *s);
const char *model_violation(void);
unsigned model_writes(void);
int model_same_as_start(char *why, size_t size);
void model_settle(void);
int model_saw(const char *needle);
void model_log_reset(void);
void model_fail(const char *fmt, ...) __attribute__((noreturn,
	format(printf, 1, 2)));
unsigned long long model_now_us(void);

/* smmumodel.c */
uint8_t *smmu_window(void);
unsigned smmu_writes(void);
uint32_t smmu_rd(uint32_t off);
uint64_t smmu_rd64(uint32_t off);
void smmu_wr(uint32_t off, uint32_t v);
void smmu_wr64(uint32_t off, uint64_t v);
void smmu_model_reset(const struct scenario *s);
int smmu_model_same(char *why, size_t size);
unsigned model_contig_live(void);

#endif
