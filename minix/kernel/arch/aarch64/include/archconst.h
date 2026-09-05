#ifndef _AARCH64_ACONST_H
#define _AARCH64_ACONST_H

#include <machine/interrupt.h>
#include <machine/memory.h>
#include <machine/vm.h>

/*
 * The processor state a process starts with, as SPSR_EL1 holds it: the
 * exception level to return to in M[3:0] and the interrupt masks in DAIF.
 *
 * D, A and F are masked. Nothing in this kernel raises a debug exception, an
 * SError or an FIQ, and nothing would know what to do with one. I is clear,
 * so the timer can preempt: a process that could not be interrupted would
 * make the scheduler advisory.
 *
 * EL0t is EL0 using SP_EL0, which is the only stack pointer EL0 has. EL1h is
 * EL1 using SP_EL1, its own stack rather than the one it was interrupted on -
 * the same distinction the two 32-bit ports make between user mode and
 * supervisor mode.
 */
#define AARCH64_PSR_F		(1 << 6)	/* FIQ masked */
#define AARCH64_PSR_I		(1 << 7)	/* IRQ masked */
#define AARCH64_PSR_A		(1 << 8)	/* SError masked */
#define AARCH64_PSR_D		(1 << 9)	/* debug exceptions masked */
#define AARCH64_PSR_DAIF	(AARCH64_PSR_D | AARCH64_PSR_A | \
				 AARCH64_PSR_I | AARCH64_PSR_F)

#define AARCH64_PSR_M_EL0T	0x0
#define AARCH64_PSR_M_EL1H	0x5

#define INIT_PSR	(AARCH64_PSR_M_EL0T | AARCH64_PSR_D | \
			 AARCH64_PSR_A | AARCH64_PSR_F)
#define INIT_TASK_PSR	(AARCH64_PSR_M_EL1H | AARCH64_PSR_D | \
			 AARCH64_PSR_A | AARCH64_PSR_F)

/*
 * One page of kernel stack per CPU. The exception frame is 34 registers,
 * 272 bytes, and nothing in the kernel recurses deeply, so a page is ample;
 * what matters more is that the stacks are page-aligned and page-apart, so
 * that an overrun lands in a guard page instead of in the neighbouring CPU's
 * stack.
 *
 * Here rather than in arch_proto.h because vectors.S makes the reservation
 * and has to agree with the C side about its size.
 */
#define K_STACK_SIZE	AARCH64_PAGE_SIZE

/*
 * Bytes reserved at the top of a kernel stack for what has to be found
 * without a register to point at it: the CPU's own number, and on ARM the
 * process running on it. Same arrangement, and the same size, as on ARM.
 *
 * The process half turned out not to be needed here: an exception from EL0
 * arrives with SP_EL1 already pointing into that process's p_reg, because
 * that is where restore_user_context() left it - see vectors.S. The
 * reservation stays for the CPU number, which SMP will want.
 *
 * Written as a number rather than as sizeof(reg_t) because vectors.S reads
 * this header too, and the assembler has no sizeof.
 */
#define AARCH64_STACK_TOP_RESERVED	(2 * 8)		/* two 64-bit words */

/*
 * A process may change the condition flags and nothing else. The mode bits
 * and the interrupt masks are the kernel's, and a process that could set
 * them could return to EL1 with interrupts off.
 */
#define AARCH64_PSR_USER_MASK	0xf0000000UL

#define SET_USR_PSR(rp, npsr)						\
	((rp)->p_reg.psr = ((rp)->p_reg.psr & ~AARCH64_PSR_USER_MASK) |	\
	    ((npsr) & AARCH64_PSR_USER_MASK))

/*
 * CNTKCTL_EL1: what EL0 may do with the generic timer. Only the counter is
 * ever handed over; the timer registers and the event stream are the
 * kernel's.
 */
#define AARCH64_CNTKCTL_EL0PCTEN	(1UL << 0)	/* physical counter */
#define AARCH64_CNTKCTL_EL0VCTEN	(1UL << 1)	/* virtual counter */

#define PG_ALLOCATEME ((phys_bytes)-1)

#endif /* _AARCH64_ACONST_H */
