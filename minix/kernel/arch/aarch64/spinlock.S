/*
 * The spinlock the big kernel lock is built on, and the pause its waiters
 * use.
 *
 * In assembly rather than in C for the same reason usercopy.S is: what
 * matters is which instructions execute and in what order, and a compiler is
 * entitled to rearrange a C version in ways that are correct for one core and
 * wrong for several. The generic code takes this lock through
 * arch_spinlock_lock() and nothing else, so there is one acquire and one
 * release in the whole kernel.
 *
 * The pair is the ordinary ARMv8 one:
 *
 *   acquire  spins with ldaxr/stxr until it turns a zero into a one. ldaxr
 *            carries acquire semantics, so nothing inside the critical
 *            section can be observed before the lock is seen taken.
 *
 *   release  is a single stlr of zero. Release semantics: everything the
 *            critical section did is visible to whoever next sees the zero.
 *
 * wfe is what keeps a waiting core from burning power and, on a machine with
 * more strands than cores, from starving the holder. No sev is needed to
 * match it: ldaxr arms the exclusive monitor, the release's store clears it,
 * and clearing the monitor is itself the event that wakes wfe. Arming before
 * the test is also what makes the wait safe - a release that lands between
 * the test and the wfe clears the monitor and the wfe falls straight through.
 *
 * Nothing here is conditional on CONFIG_MAX_CPUS: the generic header compiles
 * the calls out entirely when the kernel is built for a single core.
 */

	.text

/*
 * void arch_spinlock_lock(atomic_t *lock)
 */
	.globl	arch_spinlock_lock
	.type	arch_spinlock_lock, %function
arch_spinlock_lock:
	mov	w2, #1
1:
	ldaxr	w1, [x0]		/* arms the monitor as well */
	cbnz	w1, 2f			/* already held */
	stxr	w3, w2, [x0]
	cbnz	w3, 1b			/* lost the race, try again */
	ret

2:
	wfe
	b	1b
	.size	arch_spinlock_lock, . - arch_spinlock_lock

/*
 * void arch_spinlock_unlock(atomic_t *lock)
 */
	.globl	arch_spinlock_unlock
	.type	arch_spinlock_unlock, %function
arch_spinlock_unlock:
	stlr	wzr, [x0]
	ret
	.size	arch_spinlock_unlock, . - arch_spinlock_unlock

/*
 * void arch_pause(void)
 *
 * A hint that this core is spinning on something another core will change.
 * yield is the architecture's name for it; on a machine with one strand per
 * core it is a nop, which is the right answer there.
 */
	.globl	arch_pause
	.type	arch_pause, %function
arch_pause:
	yield
	ret
	.size	arch_pause, . - arch_pause
