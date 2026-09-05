/* System dependent functions for use inside the whole kernel. */

#include "kernel/kernel.h"

#include <assert.h>
#include <string.h>
#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "kernel/proc.h"
#include "kernel/debug.h"

/*
 * No architecture glo.h. earm and i386 have one to declare their TSS array,
 * the per-CPU structure the processor reads to find the kernel stack on the
 * way into an exception. AArch64 has no such structure: SP_EL1 is a register
 * per exception level, so the kernel stack is simply what is in it.
 */

/*
 * Base of the per-CPU kernel stacks, set by the architecture layer once it
 * knows where its reservation landed. Declared in arch_proto.h.
 */
void *k_stacks;

/*===========================================================================*
 *				arch_proc_reset				     *
 *===========================================================================*/
void
arch_proc_reset(struct proc *pr)
{
	assert(pr->p_nr < NR_PROCS);

	/*
	 * Every register starts at zero, which for this frame means a
	 * process that would fault immediately if it were run before
	 * something set its program counter and stack pointer. That is the
	 * intent: a process reset and not yet given a context is not
	 * runnable.
	 */
	memset(&pr->p_reg, 0, sizeof(pr->p_reg));

	pr->p_reg.psr = iskerneln(pr->p_nr) ? INIT_TASK_PSR : INIT_PSR;
}

/*===========================================================================*
 *				arch_proc_init				     *
 *===========================================================================*/
void
arch_proc_init(struct proc *pr, const vir_bytes ip, const vir_bytes sp,
	const vir_bytes ps_str, char *name)
{
	arch_proc_reset(pr);
	strlcpy(pr->p_name, name, sizeof(pr->p_name));

	pr->p_reg.pc = ip;
	pr->p_reg.sp = sp;

	/*
	 * The first argument of a fresh process is the address of its
	 * ps_strings block, and the procedure call standard puts a first
	 * argument in x0 - which is retreg, the same register a system call
	 * returns in. The two 32-bit ports do the same with r0 and eax.
	 */
	pr->p_reg.retreg = ps_str;
}

/*===========================================================================*
 *				arch_proc_setcontext			     *
 *===========================================================================*/
void
arch_proc_setcontext(struct proc *p, struct stackframe_s *state, int isuser,
	int trapstyle)
{
	assert(sizeof(p->p_reg) == sizeof(*state));

	if (state != &p->p_reg)
		memcpy(&p->p_reg, state, sizeof(*state));

	/* Tell the rest of the kernel not to touch this context again. */
	p->p_misc_flags |= MF_CONTEXT_SET;

	if (!(p->p_rts_flags)) {
		printf("WARNING: setting full context of runnable process\n");
		print_proc(p);
		util_stacktrace();
	}
}

/*===========================================================================*
 *			arch_set_secondary_ipc_return			     *
 *===========================================================================*/
void
arch_set_secondary_ipc_return(struct proc *p, u32_t val)
{
	/*
	 * The second value a system call returns. The call ABI settled at
	 * stage 1 puts the result in x0 and the status in x1, and the kernel
	 * writes back only those two.
	 */
	p->p_reg.x1 = val;
}

/*===========================================================================*
 *				arch_get_sp				     *
 *===========================================================================*/
reg_t
arch_get_sp(struct proc *p)
{
	return p->p_reg.sp;
}

/*===========================================================================*
 *				arch_do_syscall				     *
 *===========================================================================*/
void
arch_do_syscall(struct proc *proc)
{
	/* do_ipc assumes that it is running because of the current process. */
	assert(proc == get_cpulocal_var(proc_ptr));

	/*
	 * Call number in x0, endpoint in x1, message in x2, and the result
	 * back in x0. Which trap it was - a kernel call or an IPC call - was
	 * decided by the svc immediate and is not repeated here.
	 */
	proc->p_reg.retreg = do_ipc(proc->p_reg.retreg, proc->p_reg.x1,
	    proc->p_reg.x2);
}

/*===========================================================================*
 *				cpu_identify				     *
 *===========================================================================*/
void
cpu_identify(void)
{
	u64_t midr;
	unsigned cpu = cpuid;

	__asm__ volatile("mrs %0, midr_el1" : "=r"(midr));

	cpu_info[cpu].implementer = (u32_t)((midr >> 24) & 0xff);
	cpu_info[cpu].variant = (u32_t)((midr >> 20) & 0xf);
	cpu_info[cpu].arch = (u32_t)((midr >> 16) & 0xf);
	cpu_info[cpu].part = (u32_t)((midr >> 4) & 0xfff);
	cpu_info[cpu].revision = (u32_t)(midr & 0xf);

	/*
	 * Clock frequency in MHz. MIDR does not carry it and there is no
	 * architectural register that does - CNTFRQ_EL0 is the rate of the
	 * system counter, which is deliberately not the core clock. It comes
	 * from the device tree, and until that is parsed there is nothing
	 * honest to put here.
	 */
	cpu_info[cpu].freq = 0;
}

/*===========================================================================*
 *				arch_init				     *
 *===========================================================================*/
void
arch_init(void)
{
	u64_t cntkctl;

	/*
	 * Let EL0 read the virtual counter. libsys reads it for read_tsc()
	 * and for the free-running clock, and this bit is what makes that a
	 * plain instruction instead of a trap into the kernel. It is the
	 * counter and not the PMU cycle counter on purpose: one timebase for
	 * every core, which the cycle counter is not. See
	 * port/PORTING-LOG.md, stage 1.
	 *
	 * The event stream and EL0 access to the timer registers stay off:
	 * nothing in userland programs a timer, and a bit granted without a
	 * user is a bit nobody will think to revoke.
	 */
	__asm__ volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl));
	cntkctl |= AARCH64_CNTKCTL_EL0VCTEN;
	__asm__ volatile("msr cntkctl_el1, %0" :: "r"(cntkctl));

	/*
	 * The per-CPU kernel stacks are not set up here yet. They are what
	 * the exception path lands on, and the reservation belongs beside the
	 * code that switches to it; both arrive with the exception vectors.
	 * ARM also calls bsp_init() here, which this BSP does not have: it is
	 * a console and nothing else so far.
	 */
}

/*===========================================================================*
 *				do_ser_debug				     *
 *===========================================================================*/
void
do_ser_debug(void)
{
	/*
	 * Called from the clock interrupt to see whether a key was typed on
	 * the serial console asking the kernel to dump its state. Nothing
	 * here reads the console yet, so there is nothing to check; the same
	 * is true on ARM.
	 */
}
