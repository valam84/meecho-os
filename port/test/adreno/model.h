/*
 * Модель SoC вокруг Adreno 512 для хостового стенда (model.c): GCC, GPUCC,
 * окно GPU и SMMU — с правилом этой SoC, что обращение к блоку без такта
 * или питания вешает шину.  Такое обращение уходит longjmp() в
 * model_hang: сценарий провален, как провалился бы прогон на планшете.
 */
#ifndef _MODEL_H
#define _MODEL_H

#include <setjmp.h>
#include <stddef.h>

struct scenario {
	const char *name;
	int iface_on;		/* GCC_GPU_CFG_AHB_CLK at the start */
	int bimc_on;		/* GCC_BIMC_GFX_CLK, GCC_GPU_BIMC_GFX_CLK */
	int cx_on, gx_on;	/* the GPUCC's domains at the start */
	int clocks_on;		/* its branches on, their roots at the crystal */
	int gx_dead;		/* GX never powers (no rail) */
	int root_stuck;		/* gfx3d's root never finishes an update */
	int tz;			/* 0: no qcom,scm (ENODEV); 1: it answers yes */
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

#endif
