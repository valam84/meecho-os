/*
 * Exception entry: the saved register frame and the handler interface.
 *
 * The frame mirrors what MINIX already does on earm, where
 * include/arch/earm/include/stackframe.h is simply the register file in
 * order - r0..r15 then the status register - and struct proc embeds one as
 * p_reg. The AArch64 equivalent is x0..x30, then SP, PC and PSTATE. Keeping
 * that shape now means stage 2.6 inherits a frame the generic kernel already
 * knows how to think about, and this header becomes
 * minix/include/arch/aarch64/include/stackframe.h when there is a MINIX
 * userland to share it with.
 *
 * ESR and FAR are deliberately not in the frame. They describe the cause of
 * one exception, not the state of the interrupted code, and a frame that is
 * also a process context has no business carrying them. They are handed to
 * the handler as arguments instead.
 *
 * The offsets are used by exception.S and checked against the struct in
 * trap.c, which is the cheap modern form of what procoffsets.cf does for the
 * other architectures.
 */

#ifndef _AARCH64_TRAP_H_
#define _AARCH64_TRAP_H_

/* Byte offsets into struct stackframe_s. */
#define FRAME_X(n)	((n) * 8)
#define FRAME_SP	(31 * 8)
#define FRAME_PC	(32 * 8)
#define FRAME_PSR	(33 * 8)
#define FRAME_SIZE	(34 * 8)

/*
 * Vector numbers, in the order VBAR_EL1 lays them out: four groups of four,
 * 0x80 bytes apart. Only the EL1h group can happen today - head.S enters EL1
 * using SP_EL1, and there is no EL0 yet - but all sixteen are filled in.
 * A vector that cannot be reached is exactly the one that will be, and an
 * empty entry means running off into whatever follows the table.
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

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef uint64_t reg_t;

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

/*
 * Point VBAR_EL1 at the vector table. Called twice: once with the MMU off,
 * where the table's address is physical, and again after the move to the
 * upper half, where the same expression yields the virtual one.
 *
 * The early call is the one that matters most. A mistake in the page tables
 * shows up as an exception taken with no vectors installed, which on this
 * machine means silence.
 */
void trap_init(void);

/*
 * Expect the next synchronous data abort at EL1 rather than treating it as
 * fatal: the handler records it and returns somewhere useful instead of
 * printing a register dump and stopping.
 *
 * trap_expect_fault() resumes at the instruction after the faulting one,
 * which is what a single probing access wants. trap_expect_fault_at() resumes
 * at an address of the caller's choosing, which is what a copy loop wants:
 * stepping over one load in the middle of a copy would carry on with a hole
 * in the data. This is the mechanism earm calls phys_copy_fault.
 *
 * trap_took_fault() disarms the expectation and reports whether it fired;
 * trap_expect_clear() disarms without asking.
 */
void trap_expect_fault(void);
void trap_expect_fault_at(uint64_t resume_pc);
void trap_expect_clear(void);
int trap_took_fault(uint64_t *esr, uint64_t *far);

/*
 * The frame of the exception being handled. Valid only inside a handler, and
 * the way anything called from one - a system call, an interrupt handler
 * wanting to know whether it interrupted user code - reaches the interrupted
 * context.
 */
struct stackframe_s *trap_current_frame(void);

/* Was the current exception taken from EL0? */
int trap_from_user(void);

/*
 * Return to user mode through the frame at the top of the trap stack. Defined
 * in exception.S; see the comment there for why the frame lives where it
 * does.
 */
struct stackframe_s *trap_user_frame(void);
void restore_user_context(void) __attribute__((noreturn));

/* Called from exception.S. */
void trap_handler(struct stackframe_s *frame, uint64_t kind, uint64_t esr,
	uint64_t far);

#endif /* !__ASSEMBLER__ */

#endif /* _AARCH64_TRAP_H_ */
