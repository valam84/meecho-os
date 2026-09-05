/*
 * Moving a message between a process and the kernel.
 *
 * The address comes from the process and may be anything at all, so these two
 * routines are allowed to fault. They are the only place in the kernel that
 * dereferences a user pointer directly: everything else resolves an address
 * through the process's page tables first and reaches the page through the
 * linear map (see memory.c). Here that would cost a table walk on the hottest
 * path in the system, and the address is being used exactly once, so instead
 * the access is made and the fault is caught.
 *
 * How the catching works: the data abort handler compares the faulting return
 * address against the span of each routine, and if it lies inside, points the
 * return at __user_copy_msg_pointer_failure instead of resolving anything. A
 * wrong pointer from userland is not the kernel's problem to fix; the caller
 * gets an error and passes it back as it would for any bad argument. earm and
 * i386 do the same thing with the same three symbols.
 *
 * This is assembly, and not because it is faster. The obvious C version arms
 * a fault handler with a label address and a computed goto, and at -O2 the
 * compiler merges the two exits so that arriving through an exception return
 * skips setting the result - it silently reports success for a fault. That
 * was written, debugged and thrown away at stage 2.6; see PORTING-LOG.md.
 * Here the register holding the result is set by the instruction the fault
 * return lands on, and there is nothing for the compiler to reason about.
 */

#include <minix/ipcconst.h>

/*
 * The message is copied by an unrolled sequence, so its size is built into
 * the code rather than read from it.
 *
 * This is the check earm and i386 do not have. Both of them copy exactly 64
 * bytes here in unrolled code, and stage 3 made a message 128; those two
 * kernels now copy half a message and say nothing about it. Neither builds in
 * this tree, so the fix belongs with them, but the trap is worth closing here
 * rather than repeating: change M_MESSAGE_SIZE and this stops assembling.
 */
#if M_MESSAGE_SIZE != 128
#error "the unrolled message copy below is written for a 128-byte message"
#endif

/*
 * Eight pairs, issued as two groups of four loads and four stores. Sixteen
 * bytes an instruction, and the loads of a group can be in flight together
 * rather than each waiting for the store before it.
 *
 * Unaligned addresses are fine: a message is aligned to M_MESSAGE_ALIGN on
 * both sides by construction, but even a misaligned user pointer works,
 * because SCTLR_EL1.A is clear and these are normal memory accesses. It would
 * fault on device memory, which a process has no business passing here and
 * which the abort handler treats like any other bad pointer.
 */
	.macro	copy_message
	ldp	x2, x3, [x0, #0]
	ldp	x4, x5, [x0, #16]
	ldp	x6, x7, [x0, #32]
	ldp	x8, x9, [x0, #48]
	stp	x2, x3, [x1, #0]
	stp	x4, x5, [x1, #16]
	stp	x6, x7, [x1, #32]
	stp	x8, x9, [x1, #48]
	ldp	x2, x3, [x0, #64]
	ldp	x4, x5, [x0, #80]
	ldp	x6, x7, [x0, #96]
	ldp	x8, x9, [x0, #112]
	stp	x2, x3, [x1, #64]
	stp	x4, x5, [x1, #80]
	stp	x6, x7, [x1, #96]
	stp	x8, x9, [x1, #112]
	.endm

	.text

/*
 * int copy_msg_from_user(message *user_mbuf, message *dst)
 *	x0 = the process's message, x1 = the kernel's buffer
 *	returns 0, or -1 if the copy faulted
 *
 * Assumes the process's address space is the one loaded in TTBR0, which is
 * true on every path that reaches here: the message is copied while its
 * sender is the current process.
 *
 * Only caller-saved registers are touched and x30 is untouched, so the
 * failure path can simply return to the caller.
 */
	.globl	copy_msg_from_user
copy_msg_from_user:
	copy_message
	.globl	__copy_msg_from_user_end
__copy_msg_from_user_end:
	mov	x0, #0
	ret

	.size	copy_msg_from_user, . - copy_msg_from_user

/*
 * int copy_msg_to_user(message *src, message *user_mbuf)
 *	x0 = the kernel's message, x1 = the process's buffer
 *
 * The mirror image, and everything said above applies.
 */
	.globl	copy_msg_to_user
copy_msg_to_user:
	copy_message
	.globl	__copy_msg_to_user_end
__copy_msg_to_user_end:
	mov	x0, #0
	ret

	.size	copy_msg_to_user, . - copy_msg_to_user

/*
 * Where a faulting copy resumes. Not called; the abort handler puts the
 * return address here, so this runs with the caller's x30 still in place and
 * returns to it with an error.
 */
	.globl	__user_copy_msg_pointer_failure
__user_copy_msg_pointer_failure:
	mov	x0, #-1
	ret

	.size	__user_copy_msg_pointer_failure, \
		. - __user_copy_msg_pointer_failure
