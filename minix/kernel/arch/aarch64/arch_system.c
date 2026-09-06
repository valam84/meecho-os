/* System dependent functions for use inside the whole kernel. */

#include "kernel/kernel.h"

#include <assert.h>
#include <string.h>
#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "trap.h"
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

/*
 * Somewhere to keep each process's FP/SIMD registers.
 *
 * A fixed array indexed by process number, as on i386, and for the same
 * reason: this is handed out during arch_proc_reset(), which runs before
 * there is an allocator and cannot fail. Half a kilobyte per slot is the
 * price of the register file being what it is.
 */
static struct fpu_state fpu_states[NR_PROCS] __aligned(FPU_ALIGN);

/*===========================================================================*
 *				arch_proc_reset				     *
 *===========================================================================*/
void
arch_proc_reset(struct proc *pr)
{
	assert(pr->p_nr < NR_PROCS);

	/*
	 * The FP state, cleared: a process that has not used FP yet gets
	 * zeroed registers and a zero FPCR, which is round-to-nearest with
	 * no exception trapping - the state the architecture calls default.
	 * Tasks have negative process numbers and no slot; they are kernel
	 * code and do not touch FP.
	 */
	if (pr->p_nr >= 0) {
		pr->p_seg.fpu_state = (char *)&fpu_states[pr->p_nr];
		memset(pr->p_seg.fpu_state, 0, FPU_STATE_SIZE);
	} else
		pr->p_seg.fpu_state = NULL;

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
 *				sync_icache				     *
 *===========================================================================*/
/*
 * Make instructions the kernel has just written fetchable.
 *
 * An exec puts a program in memory with ordinary stores: VFS reads the file
 * into the process through the kernel, which reaches its pages by the linear
 * map. Those stores land in the data cache. Instruction fetch does not look
 * there - the caches are separate, and on this architecture nothing keeps
 * them in step - so without this the core can execute whatever the physical
 * page held the last time it was somebody's text, which is a crash with a
 * plausible program counter in a program that never had that code.
 *
 * This is new work, not a bug fixed: while user pages were Device memory
 * (see <machine/vm.h>) their instructions were never cached in the first
 * place. Making them cacheable is what made the instruction cache able to be
 * wrong.
 *
 * Two halves, and the first is often unnecessary. CTR_EL0.IDC says the data
 * cache is already coherent with fetch, which is true of many ARMv8.2 cores
 * and of nothing older; when it is not, the data has to be cleaned out to
 * the point instruction fetch reads from, and the only way to do that
 * without knowing the addresses is by set and way, level by level, up to
 * CLIDR_EL1.LoUIS. That is a few thousand instructions once per exec, which
 * is the price of not knowing the range - the kernel is told an entry point,
 * not an image.
 *
 * QEMU reports IDC clear, so the long path is the one the development cycle
 * exercises; the board may well take the short one.
 */
static void
sync_icache(void)
{
	unsigned long ctr, clidr, ccsidr;
	unsigned level, louis, log2line, ways, sets, waybits, w, set;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

	if (!(ctr & CTR_EL0_IDC)) {
		__asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
		louis = (unsigned)((clidr >> CLIDR_LOUIS_SHIFT) & 7);

		for (level = 0; level < louis; level++) {
			__asm__ volatile("msr csselr_el1, %0"
			    :: "r"((unsigned long)level << 1));
			isb();
			__asm__ volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));

			/* Line size is log2 of the size in words, plus two. */
			log2line = (unsigned)(ccsidr & 7) + 4;
			ways = (unsigned)((ccsidr >> 3) & 0x3ff) + 1;
			sets = (unsigned)((ccsidr >> 13) & 0x7fff) + 1;

			/*
			 * The way number sits at the top of the operand, in
			 * as many bits as the associativity needs.
			 */
			for (waybits = 0; (1U << waybits) < ways; waybits++)
				;

			for (w = 0; w < ways; w++) {
				for (set = 0; set < sets; set++) {
					unsigned long op =
					    ((unsigned long)w << (32 - waybits)) |
					    ((unsigned long)set << log2line) |
					    ((unsigned long)level << 1);

					__asm__ volatile("dc csw, %0"
					    :: "r"(op) : "memory");
				}
			}
		}
	}

	dsb();
	__asm__ volatile("ic ialluis" ::: "memory");
	dsb();
	isb();
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

	/* The image is in memory by now; make it fetchable. */
	sync_icache();

	pr->p_reg.pc = ip;
	pr->p_reg.sp = sp;

	/*
	 * Where a fresh process finds the address of its ps_strings block:
	 * the third argument register, not the first.
	 *
	 * lib/csu/arch/aarch64/crt0.S branches straight into
	 *
	 *	___start(void (*cleanup)(void), const Obj_Entry *obj,
	 *		 struct ps_strings *ps_strings)
	 *
	 * on the registers it was entered with, so ps_strings has to arrive
	 * in x2. NetBSD's arm crt0 shuffles r0 into r2 on the way, because
	 * NetBSD/arm hands it over in the first register; the aarch64 file
	 * does no shuffling, because NetBSD/aarch64 does not need it to.
	 *
	 * The kernel is what decides this, and there is no reason for MINIX
	 * to disagree with the platform it is running on. Passing it in x2
	 * leaves that file exactly as NetBSD wrote it - this tree resyncs
	 * with NetBSD, and an unmodified file stays unmodified - which is
	 * worth more than symmetry with r0 on earm and eax on i386.
	 *
	 * Getting it wrong is silent. ___start reads x2, finds the zero
	 * arch_proc_reset() left there, and _FATAL("ps_strings missing")
	 * writes to file descriptor 2 - which this early in the boot means a
	 * VFS that is not running yet - and then _exit()s. The process dies
	 * before main() with nothing said about why.
	 */
	pr->p_reg.x2 = ps_str;
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
	 * The per-CPU kernel stacks. exception.S reserves them and the EL0
	 * entry path switches onto them; this is where the pointer the two
	 * agree on gets set, exactly as ARM does it.
	 */
	k_stacks = (void *)&k_stacks_start;
	assert(!((vir_bytes)k_stacks % K_STACK_SIZE));

	/*
	 * The vectors are already installed - pre_init() writes VBAR_EL1
	 * twice, once against the table's physical address and once against
	 * its virtual one after the move, because a fault in between has to
	 * be diagnosable. Doing it once more here costs two instructions and
	 * means VBAR_EL1 is known to be right on a CPU that never went
	 * through pre_init(), which is what the secondary cores will be.
	 *
	 * ARM also calls bsp_init() here, which this BSP does not have: it is
	 * a console and a reset path so far.
	 */
	trap_init();
}

/*===========================================================================*
 *			  arch_finish_switch_to_user			     *
 *===========================================================================*/
struct proc *
arch_finish_switch_to_user(void)
{
	struct proc *p = get_cpulocal_var(proc_ptr);

	/*
	 * ARM records the process on the kernel stack here, so that entry
	 * from user mode can find it again without a spare register to point
	 * with. This does not have to: restore_user_context() sets SP_EL1 to
	 * the end of p->p_reg, so the stack pointer an exception from EL0
	 * arrives with already is the process. See exception.S.
	 *
	 * AARCH64_STACK_TOP_RESERVED stays reserved anyway - SMP will want
	 * the CPU number in it - but nothing needs it while there is one CPU.
	 *
	 * What is left is making sure the process runs with interrupts
	 * enabled. A process that could not be interrupted would make the
	 * scheduler advisory: the quantum would never end.
	 */
	p->p_reg.psr &= ~(reg_t)AARCH64_PSR_I;

	return p;
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
