#include <machine/asm.h>
#include <ucontextoffsets.h>


IMPORT(getuctx)
IMPORT(setuctx)
IMPORT(resumecontext)
IMPORT(__errno)

/*
 * The AArch64 form of the earm routines. What has to be saved is what the
 * procedure call standard makes the callee's responsibility: x19-x28, the
 * frame pointer, the stack pointer and the return address. Argument and
 * temporary registers belong to the caller and are not preserved across
 * getcontext(), exactly as they are not preserved across any other call.
 *
 * errno is reached through __errno() rather than by address so that the
 * same code serves the static and the position-independent library.
 */

/* int getcontext(ucontext_t *ucp)
 *	Initialise the structure pointed to by ucp to the current user context
 *	of the calling thread. */
ENTRY(getcontext)
ENTRY(_getcontext)
	/* In case a process does not use the FPU and is neither interested in
	 * saving its signal mask, then we can skip the context switch to
	 * PM and kernel altogether and only save general-purpose registers. */

	mov	x9, x30		/* Save return address:
				 * When setcontext or swapcontext is called,
				 * we jump to this address and continue
				 * running. */

	/* x0 = ucp */

	/* Check null pointer */
	cbz	x0, 9f			/* ucp == NULL? */

	/* Check flags */
	ldr	w1, [x0, #UC_FLAGS]	/* w1 = ucp->uc_flags */
	mov	w2, #(_UC_IGNFPU|_UC_IGNSIGM)
	and	w1, w1, w2
	cmp	w1, w2			/* Allowed to ignore both? */
	b.eq	1f			/* If so, skip getuctx */

	stp	x0, x9, [sp, #-16]!
	bl	_C_LABEL(getuctx)	/* getuctx(ucp) */
	ldp	x0, x9, [sp], #16

1:
	/* Save the context */
	str	x9, [x0, #LRREG]	/* Save lr */
	str	x9, [x0, #PCREG]	/* Save real RTA in mcp struct */
	mov	x1, sp
	str	x1, [x0, #SPREG]	/* Save stack pointer */
	str	x29, [x0, #FPREG]	/* Save fp */
	stp	x19, x20, [x0, #REG19]	/* Save the callee-saved registers */
	stp	x21, x22, [x0, #REG21]
	stp	x23, x24, [x0, #REG23]
	stp	x25, x26, [x0, #REG25]
	stp	x27, x28, [x0, #REG27]

	/* 0xc0ffee is neither a shifted 16-bit nor a bitmask immediate, so
	 * it takes two instructions to build. */
	movz	w1, #(MCF_MAGIC & 0xffff)
	movk	w1, #(MCF_MAGIC >> 16), lsl #16
	str	w1, [x0, #MAGIC]	/* Set magic value */

	str	xzr, [x0, #REG0]	/* Return 0 when resumed */
	mov	x0, #0			/* Return 0 */

	mov	x30, x9			/* Restore return address */
	ret

9:	/* ucp == NULL: errno = EFAULT, return -1 */
	stp	x29, x30, [sp, #-16]!
	bl	_C_LABEL(__errno)
	mov	w1, #EFAULT
	str	w1, [x0]		/* errno = EFAULT */
	ldp	x29, x30, [sp], #16
	mov	x0, #-1			/* return -1 */
	ret
END(_getcontext)
END(getcontext)


/* int setcontext(const ucontext_t *ucp)
 *	Restore the user context pointed to by ucp. A successful call to
 *	setcontext does not return; program execution resumes at the point
 *	specified by the ucp argument. If ucp was created with getcontext(),
 *	program execution continues as if the corresponding call of getcontext()
 *	had just returned. If ucp was created with makecontext(), program
 *	execution continues with the function passed to makecontext(). */
ENTRY(setcontext)
	/* In case a process does not use the FPU and is neither interested in
	 * restoring its signal mask, then we can skip the context switch to
	 * PM and kernel altogether and restore state here. */

	/* x0 = ucp */

	/* Check null pointer */
	cbz	x0, 9f			/* ucp == NULL? */

	/* Check magic: is the context valid? */
	ldr	w1, [x0, #MAGIC]	/* w1 = ucp->mc_context.mc_magic */
	movz	w2, #(MCF_MAGIC & 0xffff)
	movk	w2, #(MCF_MAGIC >> 16), lsl #16
	cmp	w1, w2
	b.ne	8f			/* not set, return EINVAL */

	/* Check flags */
	ldr	w1, [x0, #UC_FLAGS]	/* w1 = ucp->uc_flags */
	mov	w2, #(_UC_IGNFPU|_UC_IGNSIGM)
	and	w1, w1, w2
	cmp	w1, w2			/* Allowed to ignore both? */
	b.eq	1f			/* Then don't bother restoring FPU
					 * state and signal mask */

	str	x0, [sp, #-16]!
	bl	_C_LABEL(setuctx)	/* setuctx(ucp) */
	ldr	x0, [sp], #16

1:	/* Restore the registers */
	ldp	x19, x20, [x0, #REG19]
	ldp	x21, x22, [x0, #REG21]
	ldp	x23, x24, [x0, #REG23]
	ldp	x25, x26, [x0, #REG25]
	ldp	x27, x28, [x0, #REG27]
	ldr	x29, [x0, #FPREG]	/* Restore fp */
	ldr	x1, [x0, #SPREG]
	mov	sp, x1			/* Restore sp */
	ldr	x30, [x0, #LRREG]	/* Restore lr */
	ldr	x9, [x0, #PCREG]	/* Resume address */
	ldr	x0, [x0, #REG0]		/* Restore x0: getcontext's result */
	br	x9			/* Restore pc */

8:	/* Invalid context: errno = EINVAL, return -1 */
	stp	x29, x30, [sp, #-16]!
	bl	_C_LABEL(__errno)
	mov	w1, #EINVAL
	str	w1, [x0]		/* errno = EINVAL */
	ldp	x29, x30, [sp], #16
	mov	x0, #-1			/* return -1 */
	ret

9:	/* ucp == NULL: errno = EFAULT, return -1 */
	stp	x29, x30, [sp, #-16]!
	bl	_C_LABEL(__errno)
	mov	w1, #EFAULT
	str	w1, [x0]		/* errno = EFAULT */
	ldp	x29, x30, [sp], #16
	mov	x0, #-1			/* return -1 */
	ret
END(setcontext)


/* void ctx_start()
 *	A wrapper to call resumecontext. Makecontext puts the ucp in x19.
 *	This function moves the ucp into x0 so that the ucp is the first
 *	parameter for resumecontext. The call to resumecontext will start
 *	the next context in the linked list (or exit the program if there
 *	is no context). */
ENTRY(ctx_start)
	mov	x0, x19
	b	_C_LABEL(resumecontext)
END(ctx_start)
