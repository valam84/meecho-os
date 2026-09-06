#ifndef _AARCH64_FPU_H
#define _AARCH64_FPU_H

/*
 * The FP/SIMD state of a process.
 *
 * Thirty-two 128-bit registers and the two status and control registers, and
 * nothing else: AArch64 has no separate floating-point stack, no tag word and
 * no exception-address registers, so this is the whole of it. FPSR carries the
 * cumulative exception bits and FPCR the rounding mode and trap enables, and
 * both belong to the process rather than to the machine.
 *
 * v0 to v31 are stored as pairs of 64-bit halves rather than as a 128-bit
 * type, so that the layout is the same to a compiler that has no __int128 and
 * to one that does, and so that the header can be read by the freestanding
 * kernel build - which is also why the types come from <stdint.h>.
 */

#include <stdint.h>

#define FPU_NREGS	32	/* v0 .. v31 */

/*
 * Sixteen, because the registers are saved in pairs with stp, and a pair of
 * q registers wants its address aligned to its own size. Unaligned would be
 * allowed on normal memory and slower for nothing.
 */
#define FPU_ALIGN	16

struct fpu_state {
	uint64_t	fpu_regs[FPU_NREGS * 2];	/* low half first */
	uint32_t	fpu_fpsr;
	uint32_t	fpu_fpcr;
};

#define FPU_STATE_SIZE	sizeof(struct fpu_state)

#endif /* _AARCH64_FPU_H */
