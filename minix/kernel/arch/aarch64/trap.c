/*
 * What the kernel does with an exception once exception.S has built a frame.
 *
 * One function, and the whole shape of the kernel's control flow is in it:
 * an exception from EL0 never returns from here - it leaves through
 * switch_to_user(), which picks whatever process should run next - while an
 * exception from EL1 does return, resuming what it interrupted.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/proto.h"

#include <assert.h>
#include <signal.h>
#include <string.h>

#include <minix/com.h>
#include <machine/vm.h>
#include <machine/ipcconst.h>

#include "archconst.h"
#include "arch_proto.h"
#include "trap.h"

#include "bsp_intr.h"

/*
 * The offsets exception.S uses have to be the offsets the compiler gives
 * struct stackframe_s. The two 32-bit ports generate theirs with
 * procoffsets.cf and a build step; this is the same guarantee for the price
 * of a header, and it fails at compile time rather than at the first
 * exception.
 */
typedef int _trap_frame_offsets_check[
	(FRAME_X(0) == offsetof(struct stackframe_s, retreg) &&
	 FRAME_X(1) == offsetof(struct stackframe_s, x1) &&
	 FRAME_X(30) == offsetof(struct stackframe_s, lr) &&
	 FRAME_SP == offsetof(struct stackframe_s, sp) &&
	 FRAME_PC == offsetof(struct stackframe_s, pc) &&
	 FRAME_PSR == offsetof(struct stackframe_s, psr) &&
	 FRAME_SIZE == sizeof(struct stackframe_s)) ? 1 : -1];

/* struct proc is entered through its p_reg, so p_reg must come first. */
typedef int _trap_proc_reg_first_check[
	(offsetof(struct proc, p_reg) == 0) ? 1 : -1];

/* Defined in exception.S. */
extern char exception_vectors[];

/*===========================================================================*
 *				trap_init				     *
 *===========================================================================*/
void
trap_init(void)
{
	/*
	 * Called twice: once with the MMU off, where taking the address of
	 * the table yields its physical address, and again after the move to
	 * the upper half, where the same expression yields the virtual one.
	 * The early call is the one that matters most - a mistake in the page
	 * tables shows up as an exception taken with no vectors installed,
	 * which on this machine means silence.
	 */
	__asm__ volatile("msr vbar_el1, %0" :: "r"(exception_vectors));
	__asm__ volatile("isb");
}

/*===========================================================================*
 *				kind_from_user				     *
 *===========================================================================*/
static int
kind_from_user(u64_t kind)
{
	return kind >= EXC_EL0_64_SYNC;
}

/*===========================================================================*
 *				inkernel_disaster			     *
 *===========================================================================*/
static void __dead
inkernel_disaster(struct stackframe_s *frame, u64_t kind, u64_t esr, u64_t far)
{
	struct proc *saved = get_cpulocal_var(proc_ptr);

	printf("\nkernel exception: vector %d ESR 0x%lx EC 0x%lx FAR 0x%lx\n",
	    (int)kind, (unsigned long)esr, (unsigned long)ESR_EC(esr),
	    (unsigned long)far);
	printf("pc 0x%lx sp 0x%lx psr 0x%lx\n",
	    (unsigned long)frame->pc, (unsigned long)frame->sp,
	    (unsigned long)frame->psr);

	if (saved != NULL)
		proc_stacktrace(saved);

	panic("exception in the kernel");
}

/*===========================================================================*
 *				pagefault				     *
 *===========================================================================*/
static void
pagefault(struct proc *pr, struct stackframe_s *frame, int is_nested,
	u64_t far, u64_t esr)
{
	message m_pagefault;
	int err;

	if (is_nested) {
		printf("pagefault in kernel at pc 0x%lx address 0x%lx\n",
		    (unsigned long)frame->pc, (unsigned long)far);
		inkernel_disaster(frame, EXC_EL1H_SYNC, esr, far);
	}

	/*
	 * VM is the one who resolves page faults, so a page fault by VM is
	 * not something anyone can resolve.
	 */
	if (pr->p_endpoint == VM_PROC_NR) {
		printf("pagefault for VM, pc 0x%lx addr 0x%lx esr 0x%lx\n",
		    (unsigned long)pr->p_reg.pc, (unsigned long)far,
		    (unsigned long)esr);
		proc_stacktrace(pr);
		panic("pagefault in VM");
	}

	/* Do not schedule this process until the fault has been handled. */
	RTS_SET(pr, RTS_PAGEFAULT);

	/*
	 * What travels in VPF_FLAGS is the architecture's syndrome, which VM
	 * decodes through the PFERR_* macros in its own arch/<arch>/pagetable.h.
	 * There is no aarch64 one yet - that file waits for servers/vm/pt.c to
	 * understand four levels of tables - so what is sent is ESR_EL1.ISS,
	 * and <machine/vm.h> already carries what it takes to read one:
	 * AARCH64_VM_FSC_* for the fault status and AARCH64_VM_ISS_WNR for
	 * whether the access was a write. See PORTING-LOG.md, stage 4 group 2.
	 */
	m_pagefault.m_source = pr->p_endpoint;
	m_pagefault.m_type = VM_PAGEFAULT;
	m_pagefault.VPF_ADDR = far;
	m_pagefault.VPF_FLAGS = (int)ESR_ISS(esr);

	if ((err = mini_send(pr, VM_PROC_NR, &m_pagefault, FROM_KERNEL)))
		panic("pagefault: mini_send returned %d", err);
}

/*===========================================================================*
 *				abort_exception				     *
 *===========================================================================*/
static void
abort_exception(struct proc *pr, struct stackframe_s *frame, int is_nested,
	u64_t kind, u64_t esr, u64_t far)
{
	unsigned fsc = ESR_ISS(esr) & AARCH64_VM_FSC_MASK;
	int signum;

	/*
	 * A fault inside the deliberate copy of a message from or to a
	 * process is expected and is not the kernel's to resolve: a wrong
	 * pointer from userland gets an error back, the way any other bad
	 * argument would. The check has to come first, because this is a
	 * fault taken inside another exception's handler.
	 */
	if (is_nested &&
	    ((frame->pc >= (reg_t)copy_msg_to_user &&
	      frame->pc <= (reg_t)__copy_msg_to_user_end) ||
	     (frame->pc >= (reg_t)copy_msg_from_user &&
	      frame->pc <= (reg_t)__copy_msg_from_user_end))) {
		frame->pc = (reg_t)__user_copy_msg_pointer_failure;
		return;
	}

	/* Translation, access flag and permission faults are page faults. */
	if (AARCH64_VM_PFE_IS_TRANS(fsc) || AARCH64_VM_PFE_IS_ACCESS(fsc) ||
	    AARCH64_VM_PFE_IS_PERM(fsc)) {
		pagefault(pr, frame, is_nested, far, esr);
		return;
	}

	if (is_nested)
		inkernel_disaster(frame, kind, esr, far);

	/*
	 * Some other kind of abort by a process: an unaligned access it was
	 * not allowed to make, or an external abort from the bus.
	 */
	if (fsc == AARCH64_VM_FSC_ALIGN) {
		signum = SIGBUS;
	} else {
		printf("KERNEL: unknown abort by proc %d, sending SIGSEGV "
		    "(far 0x%lx esr 0x%lx fsc 0x%x)\n", proc_nr(pr),
		    (unsigned long)far, (unsigned long)esr, fsc);
		signum = SIGSEGV;
	}

	cause_sig(proc_nr(pr), signum);
}

/*===========================================================================*
 *				do_syscall				     *
 *===========================================================================*/
static void
do_syscall(struct proc *pr, u64_t esr)
{
	/*
	 * Which trap it was is the svc immediate, which the hardware has
	 * already put in ESR_EL1.ISS - so the kernel knows the trap type
	 * before it has read a word of the frame, and no register is spent
	 * carrying it. earm passes it in r3.
	 */
	unsigned imm = (unsigned)(ESR_ISS(esr) & 0xffff);

	/*
	 * Stop charging the process for time before running kernel code on
	 * its behalf. Everything below this point is the kernel's time.
	 */
	context_stop(pr);

	switch (imm) {
	case KERVEC_INTR:
		/* A kernel call: the message pointer is in x0. */
		kernel_call((message *)pr->p_reg.retreg, pr);
		break;

	case IPCVEC_INTR:
		arch_do_syscall(pr);
		break;

	default:
		/* Not a trap this kernel offers; tell the process so. */
		pr->p_reg.retreg = (reg_t)-1;
		break;
	}
}

/*===========================================================================*
 *				trap_handler				     *
 *===========================================================================*/
void
trap_handler(struct stackframe_s *frame, u64_t kind, u64_t esr, u64_t far)
{
	int from_user = kind_from_user(kind);
	struct proc *pr = get_cpulocal_var(proc_ptr);
	unsigned ec = ESR_EC(esr);

	if (from_user) {
		/*
		 * The frame is the process's own p_reg, so the process the
		 * exception came from is the frame. Checked against what the
		 * scheduler believes, because a disagreement here would mean
		 * the kernel is about to charge the wrong process for
		 * everything that follows.
		 */
		assert((struct proc *)frame == pr);
		assert(!iskernelp(pr));
	}

	switch (kind) {
	case EXC_EL1H_IRQ:
	case EXC_EL0_64_IRQ:
	case EXC_EL0_32_IRQ:
		/*
		 * Interrupts. Which line it was and how it is acknowledged is
		 * the interrupt controller's business, which is why this is
		 * one call into the BSP - the GICv2 driver, on this machine -
		 * and nothing else. It arrives with group 3.
		 */
		bsp_irq_handle();
		break;

	case EXC_EL1H_SYNC:
	case EXC_EL0_64_SYNC:
		switch (ec) {
		case ESR_EC_SVC64:
			if (!from_user)
				inkernel_disaster(frame, kind, esr, far);
			do_syscall(pr, esr);
			break;

		case ESR_EC_DABORT_LOWER:
		case ESR_EC_DABORT_SAME:
		case ESR_EC_IABORT_LOWER:
		case ESR_EC_IABORT_SAME:
			abort_exception(pr, frame, !from_user, kind, esr, far);
			break;

		case ESR_EC_FP_ACCESS:
			/*
			 * A process touched FP or SIMD while trapping was on.
			 * The generic handler decides who owns the registers;
			 * until group 5 it refuses and the process gets
			 * SIGFPE. See fpu.c.
			 */
			if (!from_user)
				inkernel_disaster(frame, kind, esr, far);
			copr_not_available_handler();
			break;

		default:
			if (!from_user)
				inkernel_disaster(frame, kind, esr, far);
			printf("KERNEL: unhandled exception by proc %d, EC "
			    "0x%x, pc 0x%lx\n", proc_nr(pr), ec,
			    (unsigned long)frame->pc);
			cause_sig(proc_nr(pr), SIGILL);
			break;
		}
		break;

	default:
		/*
		 * FIQ, SError, anything taken on SP_EL0, and anything from
		 * AArch32. None of these can happen on a machine set up the
		 * way this one is - FIQ and SError stay masked, the kernel
		 * runs as EL1h, and there is no AArch32 userland - so
		 * reaching here means the machine is not in the state the
		 * kernel believes.
		 */
		if (!from_user)
			inkernel_disaster(frame, kind, esr, far);

		printf("KERNEL: impossible exception %d by proc %d, killing "
		    "it\n", (int)kind, proc_nr(pr));
		cause_sig(proc_nr(pr), SIGILL);
		break;
	}
}
