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

	/*
	 * The interrupted stack pointer. Taken from EL1h this is simply the
	 * current one before the frame was pushed. An exception from EL0 will
	 * have left its stack in SP_EL0 instead; that path arrives with EL0,
	 * at stage 2.6.
	 */
	add	x1, sp, #FRAME_SIZE
	str	x1, [sp, #FRAME_SP]

	mrs	x1, elr_el1
	str	x1, [sp, #FRAME_PC]
	mrs	x1, spsr_el1
	str	x1, [sp, #FRAME_PSR]

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
	 * ELR and SPSR are reloaded from the frame, not left as they were:
	 * the handler is allowed to change them, and stepping over a faulting
	 * instruction is exactly that.
	 *
	 * x0 is scratch here and gets its saved value back below, so the
	 * order matters.
	 */
	ldr	x0, [sp, #FRAME_PC]
	msr	elr_el1, x0
	ldr	x0, [sp, #FRAME_PSR]
	msr	spsr_el1, x0

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
