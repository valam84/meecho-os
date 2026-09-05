#ifndef _AARCH64_TRAP_H_
#define _AARCH64_TRAP_H_

/*
 * Exception entry: where the register frame lives, and what the entry code
 * hands the handler.
 *
 * The frame is struct stackframe_s from <machine/stackframe.h>, which is what
 * struct proc embeds as p_reg and what a signal context copies. This header
 * adds only the byte offsets exception.S needs; they are checked against the
 * structure in trap.c, which is the cheap modern form of what procoffsets.cf
 * does for the two 32-bit ports.
 */

/* Byte offsets into struct stackframe_s. */
#define FRAME_X(n)	((n) * 8)
#define FRAME_SP	(31 * 8)
#define FRAME_PC	(32 * 8)
#define FRAME_PSR	(33 * 8)
#define FRAME_SIZE	(34 * 8)

/*
 * Vector numbers, in the order VBAR_EL1 lays them out: four groups of four,
 * 0x80 bytes apart. All sixteen are filled in, including the AArch32 ones
 * that nothing in this port will ever reach - an empty entry means running
 * off into whatever follows the table, and the vector that cannot happen is
 * exactly the one that will.
 */
#define EXC_EL1T_SYNC		0
#define EXC_EL1T_IRQ		1
#define EXC_EL1T_FIQ		2
#define EXC_EL1T_SERROR		3
#define EXC_EL1H_SYNC		4
#define EXC_EL1H_IRQ		5
#define EXC_EL1H_FIQ		6
#define EXC_EL1H_SERROR		7
#define EXC_EL0_64_SYNC		8
#define EXC_EL0_64_IRQ		9
#define EXC_EL0_64_FIQ		10
#define EXC_EL0_64_SERROR	11
#define EXC_EL0_32_SYNC		12
#define EXC_EL0_32_IRQ		13
#define EXC_EL0_32_FIQ		14
#define EXC_EL0_32_SERROR	15

/* ESR_EL1 decoding: the exception class and the syndrome under it. */
#define ESR_EC_SHIFT		26
#define ESR_EC(esr)		(((esr) >> ESR_EC_SHIFT) & 0x3f)
#define ESR_ISS(esr)		((esr) & 0x1ffffff)

#define ESR_EC_UNKNOWN		0x00
#define ESR_EC_WFI		0x01
#define ESR_EC_FP_ACCESS	0x07	/* SVE/SIMD/FP with FPEN trapping */
#define ESR_EC_ILLEGAL_STATE	0x0e
#define ESR_EC_SVC64		0x15
#define ESR_EC_MSR_MRS		0x18
#define ESR_EC_IABORT_LOWER	0x20	/* instruction abort from EL0 */
#define ESR_EC_IABORT_SAME	0x21	/* instruction abort from EL1 */
#define ESR_EC_PC_ALIGN		0x22
#define ESR_EC_DABORT_LOWER	0x24	/* data abort from EL0 */
#define ESR_EC_DABORT_SAME	0x25	/* data abort from EL1 */
#define ESR_EC_SP_ALIGN		0x26
#define ESR_EC_BRK64		0x3c

#ifndef __ASSEMBLER__

#include <machine/stackframe.h>

struct proc;

/* Point VBAR_EL1 at the vector table. */
void trap_init(void);

/* Called from exception.S with the frame it just built. */
void trap_handler(struct stackframe_s *frame, u64_t kind, u64_t esr,
	u64_t far);

/*
 * Enter user mode through a process's own saved frame.
 *
 * SP_EL1 is set to the end of p->p_reg, which is where an exception from EL0
 * will build its frame - so the register file a process is entered from and
 * the one an exception saves are the same bytes, and going to user and coming
 * back from user are one piece of code. Declared in kernel/proto.h as well;
 * this comment is why the argument is what it is.
 */

/* Reserved by exception.S: two pages of stack per CPU. */
extern char k_stacks_area[];

#endif /* !__ASSEMBLER__ */

#endif /* _AARCH64_TRAP_H_ */
