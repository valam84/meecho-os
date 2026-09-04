/*
 * AArch64 exception vectors.
 *
 * VBAR_EL1 points at a table of sixteen entries, each 0x80 bytes and the
 * whole thing aligned to 2048. The entries are grouped by where the
 * exception came from - current EL on SP_EL0, current EL on SP_ELx, a lower
 * EL in AArch64, a lower EL in AArch32 - and within each group by kind:
 * synchronous, IRQ, FIQ, SError.
 *
 * There is no room to do anything in 0x80 bytes, so each entry does the least
 * that cannot be shared - claim stack space, free up two registers, record
 * which vector it was - and branches to the common path.
 *
 * Everything here is PC-relative, like the rest of the boot path, because the
 * vectors are installed before the MMU is on as well as after. See mmu.c.
 */

#include "trap.h"

	.text

/*
 * One vector entry. x0 and x1 are saved first because they are needed as
 * scratch, and the vector number goes in x0 for the common path.
 */
	.macro	VECTOR kind
	.balign	0x80
	sub	sp, sp, #FRAME_SIZE
	stp	x0, x1, [sp, #FRAME_X(0)]
	mov	x0, #\kind
	b	exception_entry
	.endm

	.balign	2048
	.globl	exception_vectors
exception_vectors:
	/* Current EL, SP_EL0. Unreachable while the kernel runs as EL1h. */
	VECTOR	EXC_EL1T_SYNC
	VECTOR	EXC_EL1T_IRQ
	VECTOR	EXC_EL1T_FIQ
	VECTOR	EXC_EL1T_SERROR

	/* Current EL, SP_EL1. This is where the kernel's own faults arrive. */
	VECTOR	EXC_EL1H_SYNC
	VECTOR	EXC_EL1H_IRQ
	VECTOR	EXC_EL1H_FIQ
	VECTOR	EXC_EL1H_SERROR

	/* Lower EL in AArch64. Stage 2.6, when there is an EL0 to come from. */
	VECTOR	EXC_EL0_64_SYNC
	VECTOR	EXC_EL0_64_IRQ
	VECTOR	EXC_EL0_64_FIQ
	VECTOR	EXC_EL0_64_SERROR

	/* Lower EL in AArch32. Nothing in this port will ever run AArch32. */
	VECTOR	EXC_EL0_32_SYNC
	VECTOR	EXC_EL0_32_IRQ
	VECTOR	EXC_EL0_32_FIQ
	VECTOR	EXC_EL0_32_SERROR

	.size	exception_vectors, . - exception_vectors

/*
 * Common entry. On arrival: sp already points at the frame, x0 holds the
 * vector number, and x0/x1 are already saved.
 *
 * The frame is built on the kernel stack the exception was taken on. That is
 * fine while the only thing that faults is the kernel itself; giving
 * exceptions a stack of their own is a stage 2.6 question, along with SP_EL0.
 */
exception_entry:
	stp	x2, x3, [sp, #FRAME_X(2)]
	stp	x4, x5, [sp, #FRAME_X(4)]
	stp	x6, x7, [sp, #FRAME_X(6)]
	stp	x8, x9, [sp, #FRAME_X(8)]
	stp	x10, x11, [sp, #FRAME_X(10)]
	stp	x12, x13, [sp, #FRAME_X(12)]
	stp	x14, x15, [sp, #FRAME_X(14)]
	stp	x16, x17, [sp, #FRAME_X(16)]
	stp	x18, x19, [sp, #FRAME_X(18)]
	stp	x20, x21, [sp, #FRAME_X(20)]
	stp	x22, x23, [sp, #FRAME_X(22)]
	stp	x24, x25, [sp, #FRAME_X(24)]
	stp	x26, x27, [sp, #FRAME_X(26)]
	stp	x28, x29, [sp, #FRAME_X(28)]
	str	x30, [sp, #FRAME_X(30)]

	mrs	x1, spsr_el1
	str	x1, [sp, #FRAME_PSR]

	/*
	 * The interrupted stack pointer.
	 *
	 * Taken from EL1h it is simply the current one before the frame was
	 * pushed. Taken from EL0 the process was using SP_EL0, and SP_EL1 -
	 * which is what "sp" means here - is the kernel's own stack and has
	 * nothing to do with the process.
	 *
	 * SPSR.M[3:0] is zero exactly when the exception came from EL0t, so
	 * the saved PSR is the thing to ask. It is also what the return path
	 * asks, which keeps the two from disagreeing.
	 */
	tst	x1, #0xf
	mrs	x2, sp_el0
	add	x3, sp, #FRAME_SIZE
	csel	x1, x2, x3, eq
	str	x1, [sp, #FRAME_SP]

	mrs	x1, elr_el1
	str	x1, [sp, #FRAME_PC]

	/*
	 * Read the cause here rather than in C. Nothing can overwrite ESR and
	 * FAR between the exception and this point, which stops being true the
	 * moment the handler is capable of faulting itself.
	 */
	mrs	x2, esr_el1
	mrs	x3, far_el1

	mov	x1, x0			/* vector number */
	mov	x0, sp			/* frame */
	bl	trap_handler

/*
 * The return half, reached both by falling out of the handler and by
 * restore_user_context() below.
 *
 * ELR and SPSR are reloaded from the frame, not left as they were: the
 * handler is allowed to change them, and stepping over a faulting instruction
 * or setting a system call's return value is exactly that.
 *
 * x0 and x1 are scratch here and get their saved values back below, so the
 * order matters.
 */
exception_return:
	ldr	x0, [sp, #FRAME_PC]
	msr	elr_el1, x0
	ldr	x0, [sp, #FRAME_PSR]
	msr	spsr_el1, x0

	/*
	 * Returning to EL0 means giving the process its stack pointer back.
	 * Returning to EL1h does not: sp is restored by unwinding the frame.
	 */
	tst	x0, #0xf
	b.ne	1f
	ldr	x1, [sp, #FRAME_SP]
	msr	sp_el0, x1
1:

	ldp	x0, x1, [sp, #FRAME_X(0)]
	ldp	x2, x3, [sp, #FRAME_X(2)]
	ldp	x4, x5, [sp, #FRAME_X(4)]
	ldp	x6, x7, [sp, #FRAME_X(6)]
	ldp	x8, x9, [sp, #FRAME_X(8)]
	ldp	x10, x11, [sp, #FRAME_X(10)]
	ldp	x12, x13, [sp, #FRAME_X(12)]
	ldp	x14, x15, [sp, #FRAME_X(14)]
	ldp	x16, x17, [sp, #FRAME_X(16)]
	ldp	x18, x19, [sp, #FRAME_X(18)]
	ldp	x20, x21, [sp, #FRAME_X(20)]
	ldp	x22, x23, [sp, #FRAME_X(22)]
	ldp	x24, x25, [sp, #FRAME_X(24)]
	ldp	x26, x27, [sp, #FRAME_X(26)]
	ldp	x28, x29, [sp, #FRAME_X(28)]
	ldr	x30, [sp, #FRAME_X(30)]

	add	sp, sp, #FRAME_SIZE
	eret

	.size	exception_entry, . - exception_entry

/*
 * Enter user mode.
 *
 * The frame a process is entered through sits at the top of the kernel's trap
 * stack, in exactly the place an exception from EL0 would build one. That is
 * not a coincidence: it means going to user and coming back from user use the
 * same code, and that SP_EL1 lands back at trap_stack_top every time the
 * process runs, whether it got there from a system call, an interrupt or from
 * being started.
 *
 * A kernel with more than one process copies the frame in and out of that
 * process's struct proc around this; with one process the frame is the
 * context.
 *
 * Does not return - whatever called it is abandoning its own stack.
 */
	.globl	restore_user_context
restore_user_context:
	adrp	x0, trap_stack_top
	add	x0, x0, :lo12:trap_stack_top
	sub	sp, x0, #FRAME_SIZE
	b	exception_return

	.size	restore_user_context, . - restore_user_context

/*
 * The stack the kernel runs on once there is a process.
 *
 * Separate from the boot stack because entry from EL0 has to land somewhere
 * known: the frame's position is what ties restore_user_context() to the
 * vectors. The boot stack is abandoned at the same moment, along with
 * everything kernel_main() had on it.
 */
	.section .bss.trapstack, "aw", @nobits
	.balign	4096
	.globl	trap_stack
trap_stack:
	.space	16384
	.globl	trap_stack_top
trap_stack_top:
