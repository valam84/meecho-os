/*
 * Registers of the Adreno 5xx, its ARM SMMU and the Qualcomm clock blocks
 * around it, as port/clover/hw-gpu.md lists them.  Offsets of the GPU are
 * the dword numbers of Mesa's and Linux's register database
 * (drivers/gpu/drm/msm/registers/adreno/a5xx.xml, MIT); A5XX() makes the
 * byte offset of the window.
 */
#ifndef _ADRENO_REGS_H
#define _ADRENO_REGS_H

#define A5XX(dw)			((uint32_t)(dw) * 4)

/* GPU (dword numbers). 0x0000..0x0002 have no name for a5xx; a3xx and a4xx
 * keep RBBM_HW_VERSION at 0x0000, and Linux's a5xx register dump reads all
 * three (a5xx_gpu.c), so reading them is safe. */
#define A5XX_RBBM_HW0			0x0000
#define A5XX_RBBM_STATUS		0x04f5
#define A5XX_RBBM_INT_0_STATUS		0x04e1
#define A5XX_RBBM_ALWAYSON_COUNTER_LO	0x04d2
#define A5XX_RBBM_ALWAYSON_COUNTER_HI	0x04d3
#define A5XX_RBBM_SECVID_TRUST_CNTL	0xf400
#define A5XX_CP_PFP_ME_CNTL		0x0832
#define A5XX_CP_SCRATCH_REG(n)		(0x0b78 + (n))
#define A5XX_VBIF_VERSION		0x3000

/* ARM SMMU v2, global space 0 (bytes). */
#define SMMU_SCR0			0x000
#define SMMU_ID0			0x020
#define SMMU_ID1			0x024
#define SMMU_ID2			0x028
#define SMMU_SGFSR			0x048
#define SMMU_SMR(n)			(0x800 + 4 * (n))
#define SMMU_S2CR(n)			(0xc00 + 4 * (n))
/* global space 1, one page above */
#define SMMU_CBAR(n)			(4 * (n))
#define SMMU_CBA2R(n)			(0x800 + 4 * (n))
/* a context bank (arm-smmu.h; FSRRESTORE is the SMMUv2 architecture's,
 * and Qualcomm's own interrupt self-test raises a bank's line with it) */
#define SMMU_CB_SCTLR			0x000
#define SMMU_CB_TCR2			0x010
#define SMMU_CB_TTBR0			0x020	/* 64 bits, ASID in 63:48 */
#define SMMU_CB_TTBR0_LO		0x020
#define SMMU_CB_TTBR0_HI		0x024
#define SMMU_CB_TTBR1			0x028
#define SMMU_CB_TCR			0x030
#define SMMU_CB_MAIR0			0x038
#define SMMU_CB_MAIR1			0x03c
#define SMMU_CB_PAR			0x050	/* 64 bits */
#define SMMU_CB_FSR			0x058
#define SMMU_CB_FSRRESTORE		0x05c
#define SMMU_CB_FAR			0x060	/* 64 bits */
#define SMMU_CB_FSYNR0			0x068
#define SMMU_CB_TLBIASID		0x610
#define SMMU_CB_TLBSYNC			0x7f0
#define SMMU_CB_TLBSTATUS		0x7f4
#define SMMU_CB_ATS1PR			0x800	/* 64 bits, written whole */
#define SMMU_CB_ATS1PW			0x808
#define SMMU_CB_ATS1UR			0x810
#define SMMU_CB_ATS1UW			0x818
#define SMMU_CB_ATSR			0x8f0

#define ID0_SMS				(1u << 27)
#define ID0_ATOSNS			(1u << 26)
#define ID0_NUMSIDB(v)			(((v) >> 9) & 0xf)
#define ID0_NUMSMRG(v)			((v) & 0xff)
#define ID1_PAGESIZE			(1u << 31)
#define ID1_NUMPAGENDXB(v)		(((v) >> 28) & 7)
#define ID1_NUMCB(v)			((v) & 0xff)
#define ID2_IAS(v)			((v) & 0xf)
#define ID2_OAS(v)			(((v) >> 4) & 0xf)
#define ID2_UBS(v)			(((v) >> 8) & 0xf)
#define ID2_PTFS_4K			(1u << 12)
#define SMR_VALID			(1u << 31)
#define SMR_MASK(v)			(((v) >> 16) & 0x7fff)
#define SMR_ID(v)			((v) & 0x7fff)
#define S2CR_TYPE(v)			(((v) >> 16) & 3)
#define S2CR_CBNDX(v)			((v) & 0xff)
#define S2CR_TYPE_TRANS			0
#define S2CR_TYPE_FAULT			2
#define S2CR_FIELDS			0x000300ffu	/* TYPE, CBNDX */

/* CBAR of a stage-1 bank, Linux's: S1 with S2 bypassed and the weakest
 * shareability and memory type (BPSHCFG 3, MEMATTR 0xf), which the tables
 * override. */
#define CBAR_S1_BANK			0x0001f300u
#define CBA2R_VA64			(1u << 0)

#define SCTLR_M				(1u << 0)
#define SCTLR_TRE			(1u << 1)
#define SCTLR_AFE			(1u << 2)
#define SCTLR_CFRE			(1u << 5)
#define SCTLR_CFIE			(1u << 6)
#define SCTLR_HUPCF			(1u << 8)
#define SCTLR_ASIDPNE			(1u << 12)

#define FSR_TF				(1u << 1)
#define FSR_PF				(1u << 3)
#define FSR_FAULT			0xc00001feu	/* every fault bit */

/* TCR and TCR2 of an AArch64 stage-1 bank (arm-smmu.h, io-pgtable-arm.c):
 * no walks through TTBR1, a 4 KiB granule (TG0 0), and a walk that does not
 * see the processor's caches - outer shareable, non-cacheable both ways. */
#define TCR_T0SZ(n)			((uint32_t)(n) & 0x3f)
#define TCR_SH0_OS			(2u << 12)
#define TCR_EPD1			(1u << 23)
#define TCR2_AS				(1u << 4)
#define TCR2_SEP_UPSTREAM		(7u << 15)

#define TLBSTATUS_SACTIVE		(1u << 0)
#define ATSR_ACTIVE			(1u << 0)
#define PAR_F				(1ull << 0)
#define PAR_PA(v)			((v) & 0x0000fffffffff000ull)
#define PAR_ATTR(v)			((unsigned)((v) >> 56))

/* Qualcomm clock blocks (bytes, within GCC or GPUCC). */
#define CBCR_ENABLE			(1u << 0)
#define CBCR_HW_CTL			(1u << 1)
#define CBCR_FORCE_MEM_CORE_ON		(1u << 14)
#define CBCR_OFF			(1u << 31)
#define CBCR_NOC(v)			(((v) >> 28) & 7)
#define CBCR_NOC_ON			2

#define RCG_CMD_UPDATE			(1u << 0)
#define RCG_CMD_ROOT_OFF		(1u << 31)
#define RCG_CFG_HID(v)			((v) & 0x1f)
#define RCG_CFG_SRC(v)			(((v) >> 8) & 7)
#define RCG_CFG_MODE(v)			(((v) >> 12) & 3)
#define RCG_CFG_FIELDS			0x0000371fu	/* 4:0, 10:8, 13:12 */

#define GDSC_PWR_ON			(1u << 31)
#define GDSC_SW_COLLAPSE		(1u << 0)
#define GDSC_HW_CONTROL			(1u << 1)
/* gdsc_init()'s settings: HW_CONTROL and SW_OVERRIDE off, waits 2/8/2. */
#define GDSC_INIT_MASK			0x00fff006u
#define GDSC_INIT_VAL			0x00282000u
#define CLAMP_IO			(1u << 0)
#define CLAMP_GMEM_RESET		(1u << 4)
#define BCR_ASSERT			(1u << 0)

/* Alpha PLL, the "default" layout (clk-alpha-pll.c). */
#define PLL_MODE			0x00
#define PLL_L				0x04
#define PLL_ALPHA			0x08
#define PLL_ALPHA_U			0x0c
#define PLL_USER_CTL			0x10
#define PLL_MODE_OUTCTRL		(1u << 0)
#define PLL_MODE_LOCK_DET		(1u << 31)
#define PLL_USER_ALPHA_EN		(1u << 24)

/* GCC's vote register for GPLL0's branches to the GPU (bits 3 and 4). */
#define GCC_GPLL0_MODE			0x00000

#endif /* _ADRENO_REGS_H */
