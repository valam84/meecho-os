#ifndef _MALIREG_H
#define _MALIREG_H

/*
 * The registers of an ARM Mali GPU with a job manager - Midgard and
 * Bifrost, which is what the Mali-G52 of the RK3566 is.  The numbers are
 * facts about the hardware, read out of the register map that both Linux
 * drivers carry (panfrost_regs.h and kbase's regmap, of the vendor kernel
 * that runs on the board); nothing else is taken from them.
 *
 * Three blocks in one window: the GPU control block at 0, the job manager
 * at 0x1000 with its slots from 0x1800, and the MMU at 0x2000 with its
 * address spaces from 0x2400.  Each block raises its own interrupt line,
 * which is why the device tree names three.
 */

/* GPU control: identity. */
#define GPU_ID			0x000
#define  GPU_ID_PRODUCT(x)	((x) >> 16)
#define  GPU_ID_ARCH_MAJOR(x)	(((x) >> 28) & 0xf)
#define  GPU_ID_VER_MAJOR(x)	(((x) >> 12) & 0xf)
#define  GPU_ID_VER_MINOR(x)	(((x) >> 4) & 0xff)
#define  GPU_ID_VER_STATUS(x)	((x) & 0xf)
#define GPU_L2_FEATURES		0x004
#define GPU_CORE_FEATURES	0x008
#define GPU_TILER_FEATURES	0x00c
#define GPU_MEM_FEATURES	0x010
#define GPU_MMU_FEATURES	0x014	/* VA bits 7:0, PA bits 15:8 */
#define GPU_AS_PRESENT		0x018
#define GPU_JS_PRESENT		0x01c

/* GPU control: interrupts and commands. */
#define GPU_INT_RAWSTAT		0x020
#define GPU_INT_CLEAR		0x024
#define GPU_INT_MASK		0x028
#define GPU_INT_STAT		0x02c
#define  GPU_IRQ_FAULT			(1u << 0)
#define  GPU_IRQ_MULTIPLE_FAULT		(1u << 7)
#define  GPU_IRQ_RESET_COMPLETED	(1u << 8)
#define  GPU_IRQ_POWER_CHANGED		(1u << 9)
#define  GPU_IRQ_POWER_CHANGED_ALL	(1u << 10)
#define  GPU_IRQ_PERFCNT_SAMPLE		(1u << 16)
#define  GPU_IRQ_CLEAN_CACHES		(1u << 17)
#define  GPU_IRQ_ALL			0x00030781u
#define GPU_CMD			0x030
#define  GPU_CMD_SOFT_RESET		0x01
#define  GPU_CMD_HARD_RESET		0x02
#define  GPU_CMD_CLEAN_CACHES		0x07
#define  GPU_CMD_CLEAN_INV_CACHES	0x08
#define GPU_STATUS		0x034
#define GPU_LATEST_FLUSH_ID	0x038
#define GPU_FAULTSTATUS		0x03c
#define GPU_FAULTADDRESS_LO	0x040
#define GPU_FAULTADDRESS_HI	0x044
#define GPU_AFBC_FEATURES	0x04c

#define GPU_THREAD_MAX_THREADS	0x0a0
#define GPU_THREAD_MAX_WORKGROUP 0x0a4
#define GPU_THREAD_MAX_BARRIER	0x0a8
#define GPU_THREAD_FEATURES	0x0ac
#define GPU_TEXTURE_FEATURES(n)	(0x0b0 + 4 * (n))
#define GPU_JS_FEATURES(n)	(0x0c0 + 4 * (n))

/*
 * The cores, each kind as a bitmap: present (fixed), ready (powered and
 * usable), and two write-only registers that power a set on or off.  The
 * high halves are there for parts with more than 32 cores; this one has
 * two shader cores, so only the low words are used.
 */
#define GPU_SHADER_PRESENT_LO	0x100
#define GPU_TILER_PRESENT_LO	0x110
#define GPU_L2_PRESENT_LO	0x120
#define GPU_SHADER_READY_LO	0x140
#define GPU_TILER_READY_LO	0x150
#define GPU_L2_READY_LO		0x160
#define GPU_SHADER_PWRON_LO	0x180
#define GPU_TILER_PWRON_LO	0x190
#define GPU_L2_PWRON_LO		0x1a0
#define GPU_SHADER_PWROFF_LO	0x1c0
#define GPU_TILER_PWROFF_LO	0x1d0
#define GPU_L2_PWROFF_LO	0x1e0
#define GPU_SHADER_PWRTRANS_LO	0x200
#define GPU_TILER_PWRTRANS_LO	0x210
#define GPU_L2_PWRTRANS_LO	0x220

#define GPU_COHERENCY_FEATURES	0x300
#define GPU_COHERENCY_ENABLE	0x304
#define GPU_STACK_PRESENT_LO	0xe00

/* Implementation-specific configuration, where the errata quirks go. */
#define GPU_JM_CONFIG		0xf00
#define GPU_SHADER_CONFIG	0xf04
#define GPU_TILER_CONFIG	0xf08
#define GPU_L2_MMU_CONFIG	0xf0c

/* The job manager. */
#define JOB_INT_RAWSTAT		0x1000
#define JOB_INT_CLEAR		0x1004
#define JOB_INT_MASK		0x1008
#define JOB_INT_STAT		0x100c
#define JOB_INT_JS_STATE	0x1010

/* The MMU. */
#define MMU_INT_RAWSTAT		0x2000
#define MMU_INT_CLEAR		0x2004
#define MMU_INT_MASK		0x2008
#define MMU_INT_STAT		0x200c

#endif /* _MALIREG_H */
