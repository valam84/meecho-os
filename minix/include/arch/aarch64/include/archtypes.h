
#ifndef _AARCH64_TYPES_H
#define _AARCH64_TYPES_H

#include <minix/sys_config.h>
#include <machine/stackframe.h>
#include <sys/cdefs.h>

typedef struct segframe {
	reg_t	p_ttbr;		/* page table root: TTBR0_EL1 value, ASID included */
	u64_t	*p_ttbr_v;	/* the root table through the kernel's mapping */
	char	*fpu_state;	/* FP/SIMD registers, once the process has used them */
} segframe_t;

/* Decoded from MIDR_EL1. */
struct cpu_info {
	u32_t	arch;
	u32_t	implementer;
	u32_t	part;
	u32_t	variant;
	u32_t	freq;		/* in MHz */
	u32_t	revision;
};

typedef u32_t atomic_t;	/* access to an aligned 32bit value is atomic on AArch64 */

#endif /* #ifndef _AARCH64_TYPES_H */
