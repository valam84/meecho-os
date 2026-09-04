#ifndef _AARCH64_STACKFRAME_H
#define _AARCH64_STACKFRAME_H

/*
 * The saved register file of a process.
 *
 * This is the shape MINIX gives every architecture - on earm it is r0..r15
 * and the status register, and struct proc embeds one as p_reg - and it is
 * also, byte for byte, the frame the kernel's exception entry builds on the
 * way in. Keeping the two the same means entering the kernel, leaving it and
 * switching processes all speak about one layout.
 *
 * ESR and FAR are deliberately absent. They describe the cause of one
 * exception, not the state of the interrupted code, and a structure that is
 * also a process context has no business carrying them.
 *
 * The header is shared with the freestanding kernel build, which is why it
 * takes its types from <stdint.h> rather than <sys/types.h>.
 */

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef uint64_t reg_t;		/* machine register */

struct stackframe_s {
	reg_t retreg;		/* x0 */
	reg_t x1;
	reg_t x2;
	reg_t x3;
	reg_t x4;
	reg_t x5;
	reg_t x6;
	reg_t x7;
	reg_t x8;
	reg_t x9;
	reg_t x10;
	reg_t x11;
	reg_t x12;
	reg_t x13;
	reg_t x14;
	reg_t x15;
	reg_t x16;
	reg_t x17;
	reg_t x18;		/* platform register */
	reg_t x19;
	reg_t x20;
	reg_t x21;
	reg_t x22;
	reg_t x23;
	reg_t x24;
	reg_t x25;
	reg_t x26;
	reg_t x27;
	reg_t x28;
	reg_t fp;		/* x29 */
	reg_t lr;		/* x30 */
	reg_t sp;
	reg_t pc;		/* ELR_EL1 */
	reg_t psr;		/* SPSR_EL1 */
};

#endif /* !__ASSEMBLER__ */

#endif /* _AARCH64_STACKFRAME_H */
