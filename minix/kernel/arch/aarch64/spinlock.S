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
 * int arch_spinlock_lock_until(atomic_t *lock, u64_t deadline)
 *
 * The same acquire, giving up once CNTVCT_EL0 reaches deadline: 0 when the
 * lock was taken, 1 when time ran out first. A deadline already in the past
 * makes it a single attempt - a trylock.
 *
 * The wfe needs something to wake it that does not depend on the holder,
 * because the case this exists for is a holder that will never release. That
 * is the timer's event stream, which arch_init() turns on for every core;
 * without it a waiter on a dead holder would sleep in wfe for good and never
 * get to look at the clock.
 */
	.globl	arch_spinlock_lock_until
	.type	arch_spinlock_lock_until, %function
arch_spinlock_lock_until:
	mov	w2, #1
1:
	ldaxr	w3, [x0]		/* arms the monitor as well */
	cbnz	w3, 2f			/* already held */
	stxr	w4, w2, [x0]
	cbnz	w4, 1b			/* lost the race, try again */
	mov	w0, #0
	ret

2:
	isb				/* order the counter read, as read_tsc_64 */
	mrs	x4, cntvct_el0
	cmp	x4, x1
	b.hs	3f
	wfe
	b	1b

3:
	clrex
	mov	w0, #1
	ret
	.size	arch_spinlock_lock_until, . - arch_spinlock_lock_until

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
