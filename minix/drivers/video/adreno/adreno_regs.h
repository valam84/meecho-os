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

/* What a5xx_hw_init() writes, and the command processor's own (stage cp,
 * adreno_cp.c).  The 64-bit ones are a LO/HI pair, LO first. */
#define A5XX_RBBM_INTERFACE_HANG_INT_CNTL 0x002f
#define A5XX_RBBM_INT_CLEAR_CMD		0x0037
#define A5XX_RBBM_INT_0_MASK		0x0038
#define A5XX_RBBM_SW_RESET_CMD		0x0043
#define A5XX_RBBM_CLOCK_CNTL		0x0067
#define A5XX_RBBM_AHB_CNTL0		0x0093
#define A5XX_RBBM_AHB_CNTL1		0x0094
#define A5XX_RBBM_AHB_CNTL2		0x0095
#define A5XX_RBBM_AHB_CMD		0x0096
#define A5XX_RBBM_PERFCTR_CNTL		0x0464
#define A5XX_RBBM_PERFCTR_RBBM_SEL_0	0x046b
#define A5XX_RBBM_PERFCTR_GPU_BUSY_MASKED 0x046f
#define A5XX_RBBM_AHB_ERROR_STATUS	0x04f4
#define A5XX_RBBM_STATUS3		0x0530
#define A5XX_RBBM_ISDB_CNT		0x0533
#define A5XX_CP_RB_BASE			0x0800	/* and _HI 0x0801 */
#define A5XX_CP_RB_CNTL			0x0802
#define A5XX_CP_RB_RPTR_ADDR		0x0804	/* and _HI 0x0805 */
#define A5XX_CP_RB_RPTR			0x0806
#define A5XX_CP_RB_WPTR			0x0807
#define A5XX_CP_ROQ_THRESHOLDS_1	0x081f
#define A5XX_CP_ROQ_THRESHOLDS_2	0x0820
#define A5XX_CP_MEQ_THRESHOLDS		0x0825
#define A5XX_CP_MERCIU_SIZE		0x0826
#define A5XX_CP_CHICKEN_DBG		0x0833
#define A5XX_CP_PFP_INSTR_BASE		0x0835	/* and _HI 0x0836 */
#define A5XX_CP_ME_INSTR_BASE		0x0838	/* and _HI 0x0839 */
#define A5XX_CP_ADDR_MODE_CNTL		0x0860
#define A5XX_CP_PROTECT(n)		(0x0880 + (n))
#define A5XX_CP_PROTECT_CNTL		0x08a0
#define A5XX_CP_INTERRUPT_STATUS	0x0b18
#define A5XX_CP_HW_FAULT		0x0b1a
#define A5XX_CP_PROTECT_STATUS		0x0b1c
#define A5XX_CP_PERFCTR_CP_SEL_0	0x0bb0
#define A5XX_VSC_ADDR_MODE_CNTL		0x0bc1
#define A5XX_GRAS_ADDR_MODE_CNTL	0x0c81
#define A5XX_RB_DBG_ECO_CNTL		0x0cc4
#define A5XX_RB_ADDR_MODE_CNTL		0x0cc5
#define A5XX_RB_MODE_CNTL		0x0cc6
#define A5XX_PC_DBG_ECO_CNTL		0x0d00
#define A5XX_PC_ADDR_MODE_CNTL		0x0d01
#define A5XX_HLSQ_DBG_ECO_CNTL		0x0e04
#define A5XX_HLSQ_ADDR_MODE_CNTL	0x0e05
#define A5XX_VFD_ADDR_MODE_CNTL		0x0e41
#define A5XX_VPC_DBG_ECO_CNTL		0x0e60
#define A5XX_VPC_ADDR_MODE_CNTL		0x0e61
#define A5XX_UCHE_ADDR_MODE_CNTL	0x0e80
#define A5XX_UCHE_MODE_CNTL		0x0e81
#define A5XX_UCHE_WRITE_THRU_BASE	0x0e87	/* 64 */
#define A5XX_UCHE_TRAP_BASE		0x0e89	/* 64 */
#define A5XX_UCHE_GMEM_RANGE_MIN	0x0e8b	/* 64 */
#define A5XX_UCHE_GMEM_RANGE_MAX	0x0e8d	/* 64 */
#define A5XX_UCHE_DBG_ECO_CNTL_2	0x0e8f
#define A5XX_UCHE_CACHE_WAYS		0x0e96
#define A5XX_SP_ADDR_MODE_CNTL		0x0ec1
#define A5XX_TPL1_ADDR_MODE_CNTL	0x0f01
#define A5XX_TPL1_MODE_CNTL		0x0f02
#define A5XX_VBIF_GATE_OFF_WRREQ_EN	0x302a
#define A5XX_VBIF_ROUND_ROBIN_QOS_ARB	0x3049
#define A5XX_VBIF_XIN_HALT_CTRL0	0x3080
#define A5XX_VBIF_XIN_HALT_CTRL1	0x3081
#define A5XX_RBBM_SECVID_TSB_TRUSTED_BASE 0xf800	/* 64 */
#define A5XX_RBBM_SECVID_TSB_TRUSTED_SIZE 0xf802
#define A5XX_RBBM_SECVID_TSB_CNTL	0xf803
#define A5XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL 0xf810

/* RBBM_STATUS: busy, but for the AHB master (HI_BUSY, bit 0). */
#define A5XX_RBBM_STATUS_HI_BUSY	(1u << 0)
/* RBBM_INT_0: the bits A5XX_INT_MASK enables (a5xx_gpu.c). */
#define A5XX_INT_AHB_ERROR		(1u << 1)
#define A5XX_INT_CP_SW			(1u << 8)
#define A5XX_INT_CP_HW_ERROR		(1u << 9)
#define A5XX_INT_CP_CACHE_FLUSH_TS	(1u << 20)
#define A5XX_INT_MISC_HANG_DETECT	(1u << 23)
#define A5XX_INT_MASK			0x1190037eu

/* CP_RB_CNTL: 32 KiB (BUFSZ log2(size/8)), 32-byte blocks, and the
 * processor never writes the read pointer back on its own (NO_UPDATE). */
#define A5XX_CP_RB_CNTL_VALUE		0x0800020cu
#define A5XX_RING_SIZE			(32 * 1024)

/* PM4 (adreno_pm4.xml, MIT): type 4 writes registers, type 7 is an
 * opcode; each header carries the parity of its fields. */
#define PM4_TYPE4			0x40000000u
#define PM4_TYPE7			0x70000000u
#define CP_EVENT_WRITE			0x46
#define CP_ME_INIT			0x48
#define CP_SET_PROTECTED_MODE		0x5f
#define CP_WHERE_AM_I			0x62
#define CP_SET_SECURE_MODE		0x66
#define CACHE_FLUSH_TS			0x04
#define CP_EVENT_WRITE_IRQ		(1u << 31)

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
/* Translate through bank 0, SHCFG NSH - the tablet's downstream value */
#define S2CR_ROUTE_CB0			0x00000300u

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
