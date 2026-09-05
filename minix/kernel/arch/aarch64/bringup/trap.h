/*
 * Exception entry: the saved register frame and the handler interface.
 *
 * The frame is struct stackframe_s from <stackframe.h>, which lives in
 * minix/include/arch/aarch64/include and is shared with userland: it is
 * what struct proc embeds as p_reg and what a signal context copies. This
 * header adds only what the kernel needs on top - the byte offsets used by
 * exception.S, checked against the struct in trap.c, which is the cheap
 * modern form of what procoffsets.cf does for the other architectures.
 *
 * ESR and FAR are deliberately not in the frame. They describe the cause of
 * one exception, not the state of the interrupted code, and a frame that is
 * also a process context has no business carrying them. They are handed to
 * the handler as arguments instead.
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
#include <stackframe.h>		/* struct stackframe_s, reg_t */

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
