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
	int bypass_free;	/* SMR 0 busy, the free SMR 1 bypasses */
	int s2cr_ro;		/* S2CR writes dropped, SMR ones kept: the tablet */
	/* TrustZone and the zap shader (tzmodel.c) */
	int zap_state;		/* QCOMSCM_PAS_* of peripheral 13 at the start */
	int zap_auth_fail;	/* the image's signature is not accepted */
	/* step 10: the speed bin's fuse, and VDDMX's votes (qcom_rpm) */
	unsigned speed_fuse;	/* the value at qfprom 0x41a2, bits 5..12 */
	int no_rpm;		/* no qcom_rpm: every vote ENODEV */
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
uint32_t model_core_khz(void);		/* gfx3d as it runs */
unsigned model_rpm_level(void);		/* VDDMX: the level voted, or 0 */
int model_rpm_on(void);			/* "swen" 1 */

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
const uint8_t *model_ram_of(const void *cpu, size_t len, uint64_t *pa);
uint8_t *model_ram_pa(uint64_t pa, size_t n);
int smmu_gpu_pa(uint64_t va, int write, uint64_t *pa);
void *model_pages_new(size_t len);
int model_pages_free(void *addr, size_t len);
int model_pages_whole(const void *p, size_t len);
unsigned model_pages_live(void);
int model_umap(const void *cpu, size_t len, uint64_t *pa);
void smmu_model_serving(int on);
uint64_t smmu_model_lines(void);
uint64_t smmu_model_lines_of(uint64_t want);
int model_gpu_powered(void);		/* model.c */
uint32_t model_gpu_reg(uint32_t dw);
void model_gpu_set(uint32_t dw, uint32_t v);

/* cpmodel.c */
void cp_model_reset(const struct scenario *s);
void cp_expect(const void *pm4, size_t pm4_len, const void *pfp,
	size_t pfp_len);
int cp_model_rd(uint32_t dw, uint32_t *v);
void cp_model_wr(uint32_t dw, uint32_t v);
void cp_model_gx_off(void);
int cp_model_busy(void);
int cp_model_started(void);
unsigned cp_model_packets(void);
int cp_model_line_up(int vec);
/* The ring as the render node feeds it (msmtest.c): run at once, or when
 * told; what ran; a hang; the GPU's own hang detection. */
void cp_model_defer(int on);
void cp_model_run(void);
int cp_model_hung(void);
unsigned cp_model_ibs(void);
unsigned cp_model_invals(void);
const char *cp_model_trail(void);
void cp_model_trail_reset(void);
void cp_model_raise(uint32_t bits);
#define CP_MODEL_HANG	0x4a414d00u	/* a CP_NOP's word: the CP stops */

/* tzmodel.c */
void tz_model_reset(const struct scenario *s);
int tz_model_same(char *why, size_t size);
void *tz_map(unsigned long pa, size_t len);
void tz_expect(const void *meta, size_t meta_len, const void *image,
	size_t image_len, uint32_t span);
unsigned tz_calls(void);
const char *tz_trail(void);
const char *tz_refusal(void);
int tz_state(void);

#endif
