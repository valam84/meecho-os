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
 * one of each kind, so only the low words are used.
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

/* JM_CONFIG: the IDVS group size, which panfrost sets on the G52. */
#define  JM_IDVS_GROUP_SIZE_SHIFT	16
#define  JM_DEFAULT_IDVS_GROUP_SIZE	0xf

/*
 * The job manager: one bit per slot in the low half (done) and the high
 * half (failed) of its interrupt registers, and a window per slot.  Each
 * slot has the job running now and a "next" set of registers for the one
 * to start after it; a job is given by writing the next set and the start
 * command.
 */
#define JOB_INT_RAWSTAT		0x1000
#define JOB_INT_CLEAR		0x1004
#define JOB_INT_MASK		0x1008
#define JOB_INT_STAT		0x100c
#define JOB_INT_JS_STATE	0x1010
#define  JOB_INT_DONE(js)	(1u << (js))
#define  JOB_INT_FAILED(js)	(1u << ((js) + 16))
#define  JOB_INT_ALL_SLOTS	0x00070007u	/* three slots on this part */

#define JS_BASE(js)		(0x1800 + 0x80 * (js))
#define JS_HEAD_LO(js)		(JS_BASE(js) + 0x00)
#define JS_HEAD_HI(js)		(JS_BASE(js) + 0x04)
#define JS_CONFIG(js)		(JS_BASE(js) + 0x18)
#define JS_COMMAND(js)		(JS_BASE(js) + 0x20)
#define JS_STATUS(js)		(JS_BASE(js) + 0x24)
#define JS_HEAD_NEXT_LO(js)	(JS_BASE(js) + 0x40)
#define JS_HEAD_NEXT_HI(js)	(JS_BASE(js) + 0x44)
#define JS_AFFINITY_NEXT_LO(js)	(JS_BASE(js) + 0x50)
#define JS_AFFINITY_NEXT_HI(js)	(JS_BASE(js) + 0x54)
#define JS_CONFIG_NEXT(js)	(JS_BASE(js) + 0x58)
#define JS_COMMAND_NEXT(js)	(JS_BASE(js) + 0x60)
#define JS_FLUSH_ID_NEXT(js)	(JS_BASE(js) + 0x70)
#define  JS_COMMAND_START		0x01
#define  JS_COMMAND_HARD_STOP		0x03
#define  JS_CONFIG_START_FLUSH_CLEAN_INV (3u << 8)
#define  JS_CONFIG_JOB_CHAIN_FLAG	(1u << 11)
#define  JS_CONFIG_END_FLUSH_CLEAN_INV	(3u << 12)
#define  JS_CONFIG_ENABLE_FLUSH_REDUCTION (1u << 14)
#define  JS_CONFIG_THREAD_PRI(n)	((n) << 16)

/*
 * The MMU: one interrupt bit per address space for a page fault (low half)
 * and for a bus error (high half), and a window per address space.  The
 * address space is in the legacy LPAE mode - the one the board's panfrost
 * runs - so only TRANSTAB and MEMATTR are written; TRANSCFG stays zero.
 */
#define MMU_INT_RAWSTAT		0x2000
#define MMU_INT_CLEAR		0x2004
#define MMU_INT_MASK		0x2008
#define MMU_INT_STAT		0x200c
#define  MMU_INT_PAGE_FAULT(as)	(1u << (as))
#define  MMU_INT_BUS_ERROR(as)	(1u << ((as) + 16))
#define  MMU_INT_AS(as)		(MMU_INT_PAGE_FAULT(as) | MMU_INT_BUS_ERROR(as))

#define AS_BASE(as)		(0x2400 + 0x40 * (as))
#define AS_TRANSTAB_LO(as)	(AS_BASE(as) + 0x00)
#define AS_TRANSTAB_HI(as)	(AS_BASE(as) + 0x04)
#define AS_MEMATTR_LO(as)	(AS_BASE(as) + 0x08)
#define AS_MEMATTR_HI(as)	(AS_BASE(as) + 0x0c)
#define AS_LOCKADDR_LO(as)	(AS_BASE(as) + 0x10)
#define AS_LOCKADDR_HI(as)	(AS_BASE(as) + 0x14)
#define AS_COMMAND(as)		(AS_BASE(as) + 0x18)
#define AS_FAULTSTATUS(as)	(AS_BASE(as) + 0x1c)
#define AS_FAULTADDRESS_LO(as)	(AS_BASE(as) + 0x20)
#define AS_FAULTADDRESS_HI(as)	(AS_BASE(as) + 0x24)
#define AS_STATUS(as)		(AS_BASE(as) + 0x28)
#define AS_TRANSCFG_LO(as)	(AS_BASE(as) + 0x30)
#define AS_TRANSCFG_HI(as)	(AS_BASE(as) + 0x34)
#define  AS_COMMAND_UPDATE		0x01
#define  AS_COMMAND_LOCK		0x02
#define  AS_COMMAND_FLUSH_MEM		0x05
#define  AS_STATUS_ACTIVE		0x01
#define  AS_TRANSTAB_ADRMODE_TABLE	0x3
#define  AS_TRANSTAB_READ_INNER		(1u << 2)
#define  AS_LOCKADDR_ALL		63	/* log2 of the region minus one */

/*
 * FAULTSTATUS: the exception in 7:0 - a translation fault is 0xc0 plus
 * the level of the walk that found nothing, a permission fault 0xc8 plus
 * the level - the access in 9:8, and the unit that made it in 31:16.
 */
#define  AS_FAULT_EXCEPTION(x)		((x) & 0xff)
#define  AS_FAULT_ACCESS(x)		(((x) >> 8) & 3)
#define  AS_FAULT_SOURCE(x)		((x) >> 16)
#define  AS_FAULT_DECODER		(1u << 10)
#define  AS_ACCESS_ATOMIC		0
#define  AS_ACCESS_EXECUTE		1
#define  AS_ACCESS_READ			2
#define  AS_ACCESS_WRITE		3
#define  MALI_EXC_TRANSLATION(e)	(((e) & 0xf8) == 0xc0)
#define  MALI_EXC_PERMISSION(e)		(((e) & 0xf8) == 0xc8)

/*
 * MEMATTR: eight one-byte attributes, indexed by a page entry's AttrIndx.
 * Panfrost's: 0 and 2 "implementation-defined" (0x88), 1 write-allocate
 * (0x8d).  Mali has no non-cacheable type; a non-coherent GPU uses 0.
 */
#define MALI_MEMATTR		0x00888d88u

/*
 * Page entries of the Mali LPAE format.  Four levels of 512 entries over
 * 4 KiB pages whatever the address width - "Mali seems to need a full
 * 4-level table" - with a table entry of type 3 and a leaf of type 1 at
 * every level, the last one included (in the CPU's format the last level
 * takes 3).  Permissions are stage-2-like (read 1<<6, write 2<<6), the
 * shareability "outer" (outside the GPU), and there is no access flag.
 */
#define MALI_PTE_TABLE		0x3ull
#define MALI_PTE_LEAF		0x1ull
#define MALI_PTE_READ		(1ull << 6)
#define MALI_PTE_WRITE		(2ull << 6)
#define MALI_PTE_SH_OUTER	(2ull << 8)
#define MALI_PTE_ADDR		0x000ffffffffff000ull

/*
 * A job descriptor (Mesa's genxml v7.xml).  A 32-byte header - word 0 the
 * exception status the GPU writes back (1 when done), word 4 "is 64-bit"
 * in bit 0, the type in 7:1 and the job's index in 31:16, words 6-7 the
 * next job of the chain - then the payload; WRITE_VALUE's is an address, a
 * value type and the value.
 */
#define JOB_HDR_STATUS		0
#define JOB_HDR_CONTROL		16
#define JOB_HDR_NEXT		24
#define  JOB_CTL_64BIT		1u
#define  JOB_CTL_TYPE(t)	((t) << 1)
#define  JOB_CTL_INDEX(i)	((uint32_t)(i) << 16)
#define  JOB_TYPE_WRITE_VALUE	2
#define JOB_WV_ADDRESS		32
#define JOB_WV_TYPE		40
#define JOB_WV_VALUE		48
#define  JOB_WV_IMMEDIATE32	6
#define JOB_DESC_SIZE		64
#define JOB_STATUS_DONE		1

#endif /* _MALIREG_H */
