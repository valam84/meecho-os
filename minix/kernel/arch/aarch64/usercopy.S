/*
 * Copying to and from a process's memory.
 *
 * The address comes from the process and may be anything, so the copy has to
 * be able to fail. The kernel arms trap_expect_fault_at() with the address of
 * usercopy_fault below; if a load or store here aborts, the handler puts the
 * program counter on that label and the routine returns an error instead of
 * the kernel dying of a bad pointer a process handed it. MINIX does the same
 * thing on earm and calls it phys_copy_fault.
 *
 * This is in assembly rather than C on purpose. The obvious C version uses a
 * label address and a computed goto, and it does not work: the compiler is
 * entitled to assume such a label is only reached by a jump it can see, and
 * at -O2 it merges the two exits so that arriving through an exception return
 * skips setting the result. That version silently reported success for a
 * fault. Here the register holding the result is set by the instruction the
 * fault return lands on, and there is nothing to reason about.
 *
 * One routine serves both directions. A byte copy does not care which side is
 * the untrusted one - only the caller does. That changes when PAN is enabled:
 * the kernel then cannot touch user memory with ordinary loads and stores,
 * and this splits into an LDTR version and an STTR one.
 */

	.text

/*
 * usercopy_bytes(void *dst, const void *src, unsigned long len)
 *	x0 = destination, x1 = source, x2 = length
 *	returns 0, or -1 if the copy faulted
 *
 * x30 is untouched, so the fault path can simply return to the caller.
 */
	.globl	usercopy_bytes
usercopy_bytes:
	cbz	x2, 2f
1:
	ldrb	w3, [x1], #1
	strb	w3, [x0], #1
	subs	x2, x2, #1
	b.ne	1b
2:
	mov	x0, #0
	ret

	.size	usercopy_bytes, . - usercopy_bytes

/*
 * usercopy_pairs(void *dst, const void *src, unsigned long len)
 *	x0 = destination, x1 = source, x2 = length, a multiple of 16
 *	returns 0, or -1 if the copy faulted
 *
 * The same routine for the one case the IPC path actually has: a whole
 * message, at an address the compiler aligned, of a length known at compile
 * time. Sixteen bytes an iteration instead of one is the difference between
 * a message costing four instructions per sixteen bytes and four per byte,
 * and stage 3 is about what a message costs.
 *
 * It shares usercopy_fault with the byte version. The resume label only sets
 * the result and returns, and both routines reach it with x30 untouched.
 */
	.globl	usercopy_pairs
usercopy_pairs:
	cbz	x2, 2f
1:
	ldp	x3, x4, [x1], #16
	stp	x3, x4, [x0], #16
	subs	x2, x2, #16
	b.ne	1b
2:
	mov	x0, #0
	ret

	.size	usercopy_pairs, . - usercopy_pairs

/*
 * Where a faulting access above resumes. Not called; jumped to by the
 * exception handler.
 */
	.globl	usercopy_fault
usercopy_fault:
	mov	x0, #-1
	ret

	.size	usercopy_fault, . - usercopy_fault
