/*
 * AArch64 exception vectors, and the two paths out of them.
 *
 * VBAR_EL1 points at sixteen entries of 0x80 bytes, aligned to 2048, grouped
 * by where the exception came from and then by kind. There is no room to do
 * anything in 0x80 bytes, so each entry claims its frame, frees two registers,
 * records which vector it was, and branches.
 *
 *
 * Where the frame goes, and why it is not always the same place
 * ------------------------------------------------------------
 * An exception from EL0 saves the process's registers into that process's own
 * struct proc. It can do that without a spare register to point with, because
 * SP_EL1 already points there: restore_user_context() left it at the end of
 * p->p_reg, and p_reg is the first member of struct proc. So "sub sp, sp,
 * #FRAME_SIZE" lands exactly on p_reg, and the frame the entry code builds is
 * the process context - no copy, and going to user and coming back from user
 * speak about one layout.
 *
 * The entry then moves SP_EL1 onto the real kernel stack before calling C,
 * because the handler must not write below a struct proc.
 *
 * An exception from EL1 is a nested one - a fault inside the kernel, or an
 * interrupt while the kernel runs - and builds its frame on the kernel stack
 * where it already is, and returns through it.
 *
 * The two 32-bit ports make the same distinction and call it
 * test_int_in_kernel. What is different here is that the EL0 case needs no
 * register to find the process with: the stack pointer is the answer.
 *
 * Everything is PC-relative, like the rest of the boot path, because the
 * vectors are installed before the MMU is on as well as after.
 */

#include "kernel/kernel.h"	/* CONFIG_MAX_CPUS */
#include "trap.h"
#include "archconst.h"
#include <machine/vm.h>

	.text

/* An entry taken from EL1: the frame goes on the stack already in use. */
	.macro	VECTOR_KERNEL kind
	.balign	0x80
	sub	sp, sp, #FRAME_SIZE
	stp	x0, x1, [sp, #FRAME_X(0)]
	mov	x0, #\kind
	b	exception_entry_kernel
	.endm

/* An entry taken from EL0: the frame goes into the process's own p_reg. */
	.macro	VECTOR_USER kind
	.balign	0x80
	sub	sp, sp, #FRAME_SIZE
	stp	x0, x1, [sp, #FRAME_X(0)]
	mov	x0, #\kind
	b	exception_entry_user
	.endm

	.balign	2048
	.globl	exception_vectors
exception_vectors:
	/*
	 * Current EL on SP_EL0. The kernel runs as EL1h and never uses
	 * SP_EL0, so arriving here means the mode bits were corrupted;
	 * the handler says so rather than the table being empty.
	 */
	VECTOR_KERNEL	EXC_EL1T_SYNC
	VECTOR_KERNEL	EXC_EL1T_IRQ
	VECTOR_KERNEL	EXC_EL1T_FIQ
	VECTOR_KERNEL	EXC_EL1T_SERROR

	/* Current EL on SP_EL1: the kernel's own faults and interrupts. */
	VECTOR_KERNEL	EXC_EL1H_SYNC
	VECTOR_KERNEL	EXC_EL1H_IRQ
	VECTOR_KERNEL	EXC_EL1H_FIQ
	VECTOR_KERNEL	EXC_EL1H_SERROR

	/* Lower EL in AArch64: a process. */
	VECTOR_USER	EXC_EL0_64_SYNC
	VECTOR_USER	EXC_EL0_64_IRQ
	VECTOR_USER	EXC_EL0_64_FIQ
	VECTOR_USER	EXC_EL0_64_SERROR

	/*
	 * Lower EL in AArch32. Nothing in this port runs AArch32 - the
	 * userland is LP64 and there is no compatibility layer - so these are
	 * reported and killed like any other impossible exception.
	 */
	VECTOR_USER	EXC_EL0_32_SYNC
	VECTOR_USER	EXC_EL0_32_IRQ
	VECTOR_USER	EXC_EL0_32_FIQ
	VECTOR_USER	EXC_EL0_32_SERROR

	.size	exception_vectors, . - exception_vectors

/*
 * Save everything but x0 and x1, which the vector already stored, and record
 * the interrupted SP, PC and PSR.
 */
	.macro	SAVE_FRAME_TAIL
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
	 * The interrupted stack pointer. From EL1h it is the current one
	 * before the frame was pushed; from EL0 the process was on SP_EL0 and
	 * SP_EL1 has nothing to do with it. SPSR.M[3:0] is zero exactly when
	 * the exception came from EL0t, and the return path asks the same
	 * question of the same field, so the two cannot disagree.
	 */
	tst	x1, #0xf
	mrs	x2, sp_el0
	add	x3, sp, #FRAME_SIZE
	csel	x1, x2, x3, eq
	str	x1, [sp, #FRAME_SP]

	mrs	x1, elr_el1
	str	x1, [sp, #FRAME_PC]
	.endm

/*
 * From EL0. The frame is in the process's p_reg, so sp is the process; move
 * to the kernel stack before calling C.
 */
exception_entry_user:
	SAVE_FRAME_TAIL

	mov	x1, x0			/* vector number */
	mov	x0, sp			/* frame, and the struct proc */

	mrs	x2, esr_el1
	mrs	x3, far_el1

	/*
	 * Onto this CPU's kernel stack. k_stacks points at the reservation
	 * below; the top of the upper of each CPU's two pages is the stack
	 * pointer, which leaves the lower page as a guard.
	 *
	 * One CPU for now, so this is the first pair. When there are more,
	 * the index comes from MPIDR_EL1 and the two extra instructions go
	 * here.
	 */
	adrp	x4, k_stacks
	ldr	x4, [x4, #:lo12:k_stacks]
	add	x4, x4, #(2 * K_STACK_SIZE)
	sub	x4, x4, #AARCH64_STACK_TOP_RESERVED
	mov	sp, x4

	/*
	 * A zero frame pointer stops a stack trace here rather than letting
	 * it walk off into whatever the process left in x29.
	 */
	mov	x29, xzr

	bl	trap_handler

	/*
	 * Never returns here: the handler leaves through switch_to_user(),
	 * which picks the next process and ends in restore_user_context().
	 * That is what makes the kernel's control flow after an exception be
	 * the scheduler's rather than this file's.
	 */
	bl	switch_to_user
	b	.

	.size	exception_entry_user, . - exception_entry_user

/*
 * From EL1: a fault inside the kernel, or an interrupt taken while the kernel
 * was running. The frame is on the kernel stack and the return goes back
 * through it, resuming exactly what was interrupted.
 */
exception_entry_kernel:
	SAVE_FRAME_TAIL

	mov	x1, x0			/* vector number */
	mov	x0, sp			/* frame */

	mrs	x2, esr_el1
	mrs	x3, far_el1

	bl	trap_handler
	/* falls through */

/*
 * The return half, reached by falling out of the nested handler and by
 * restore_user_context() below.
 *
 * ELR and SPSR are reloaded from the frame rather than left as they were: the
 * handler is allowed to change them, and stepping over a faulting instruction
 * or redirecting a faulting user copy is exactly that.
 */
exception_return:
	ldr	x0, [sp, #FRAME_PC]
	msr	elr_el1, x0
	ldr	x0, [sp, #FRAME_PSR]
	msr	spsr_el1, x0

	/* Returning to EL0 gives the process its stack pointer back. */
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

	.size	exception_entry_kernel, . - exception_entry_kernel

/*
 * void restore_user_context(struct proc *p)
 *
 * Enter user mode through p's own frame. Does not return: whatever called
 * this is abandoning its kernel stack, which is the point - after an
 * exception the control flow belongs to the scheduler, not to a chain of
 * returns.
 *
 * p_reg is the first member of struct proc, so the process pointer is the
 * frame pointer. Setting sp to it means the trailing "add sp, sp, #FRAME_SIZE"
 * in exception_return leaves SP_EL1 at the end of p_reg, which is exactly
 * where the next exception from EL0 expects to find it.
 */
	.globl	restore_user_context
restore_user_context:
	mov	sp, x0
	b	exception_return

	.size	restore_user_context, . - restore_user_context

/*
 * Kernel stacks, two pages per CPU: the top of the upper page is the stack
 * pointer, the lower one is the guard an overrun lands in.
 *
 * Reserved here rather than in the link script so that the size can be
 * written in terms of K_STACK_SIZE and CONFIG_MAX_CPUS, which is what the two
 * 32-bit ports do and what arch_proto.h already describes.
 */
	.section .bss.kstacks, "aw", @nobits
	.balign	K_STACK_SIZE
	.globl	k_stacks_start
k_stacks_start:
	.space	2 * K_STACK_SIZE * CONFIG_MAX_CPUS
	.globl	k_stacks_end
k_stacks_end:
