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
 *
 * The process's side is accessed with LDTR and STTR: loads and stores the
 * core checks against the EL0 permissions of the page, as if the process had
 * made them. Two things follow.
 *
 *  - A pointer into the kernel's half is refused. Nothing checked the
 *    message pointer's range before, and LDP/STP at EL1 reach the kernel's
 *    own pages; the kernel's pages are not EL0's, so now such a pointer is
 *    a permission fault and the process gets SIGSEGV like any bad pointer
 *    (port/test/pan/kptr.c checks it).
 *  - PAN does not apply to them, so these copies work with PAN on - which
 *    is what lets PAN stop every other EL1 access to a user address
 *    (pan.c).
 *
 * LDTR and STTR have no pair form, so the process's side takes sixteen
 * instructions where it took eight; the kernel's side stays in pairs. They
 * are ARMv8.0, so this is the code on a core without PAN too.
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
 * Two halves of 64 bytes, each eight loads and then eight stores, so that
 * the loads of a half can be in flight together rather than each waiting
 * for the store before it.
 *
 * Unaligned addresses are fine: a message is aligned to M_MESSAGE_ALIGN on
 * both sides by construction, but even a misaligned user pointer works,
 * because SCTLR_EL1.A is clear and these are normal memory accesses. It would
 * fault on device memory, which a process has no business passing here and
 * which the abort handler treats like any other bad pointer.
 */

/* 64 bytes at offset \o: the process's side by LDTR, the kernel's by STP. */
	.macro	half_from_user o
	ldtr	x2, [x0, #(\o + 0)]
	ldtr	x3, [x0, #(\o + 8)]
	ldtr	x4, [x0, #(\o + 16)]
	ldtr	x5, [x0, #(\o + 24)]
	ldtr	x6, [x0, #(\o + 32)]
	ldtr	x7, [x0, #(\o + 40)]
	ldtr	x8, [x0, #(\o + 48)]
	ldtr	x9, [x0, #(\o + 56)]
	stp	x2, x3, [x1, #(\o + 0)]
	stp	x4, x5, [x1, #(\o + 16)]
	stp	x6, x7, [x1, #(\o + 32)]
	stp	x8, x9, [x1, #(\o + 48)]
	.endm

/* The mirror image: the kernel's side by LDP, the process's by STTR. */
	.macro	half_to_user o
	ldp	x2, x3, [x0, #(\o + 0)]
	ldp	x4, x5, [x0, #(\o + 16)]
	ldp	x6, x7, [x0, #(\o + 32)]
	ldp	x8, x9, [x0, #(\o + 48)]
	sttr	x2, [x1, #(\o + 0)]
	sttr	x3, [x1, #(\o + 8)]
	sttr	x4, [x1, #(\o + 16)]
	sttr	x5, [x1, #(\o + 24)]
	sttr	x6, [x1, #(\o + 32)]
	sttr	x7, [x1, #(\o + 40)]
	sttr	x8, [x1, #(\o + 48)]
	sttr	x9, [x1, #(\o + 56)]
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
	half_from_user 0
	half_from_user 64
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
	half_to_user 0
	half_to_user 64
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

/*
 * u64_t kernel_probe_read(const void *addr)
 *	returns 0 if an ordinary EL1 load of the byte at addr succeeded,
 *	and the ESR_EL1 of the fault if it did not
 *
 * The opposite of the copies above: the access a kernel bug would make, made
 * on purpose to see whether the core lets it through. pan_verify() points it
 * at a process's stack and expects a permission fault. The abort handler
 * sends a fault here to __kernel_probe_read_fault with the syndrome in x0.
 */
	.globl	kernel_probe_read
kernel_probe_read:
	ldrb	w1, [x0]
	.globl	__kernel_probe_read_end
__kernel_probe_read_end:
	mov	x0, #0
	ret

	.globl	__kernel_probe_read_fault
__kernel_probe_read_fault:
	ret

	.size	kernel_probe_read, . - kernel_probe_read
