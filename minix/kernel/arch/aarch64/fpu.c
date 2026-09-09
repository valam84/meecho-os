/*
 * FP/SIMD: whose registers these are, and when they change hands.
 *
 * The generic kernel switches them lazily. One process per CPU owns the
 * register file; everybody else runs with FP trapping on, and the first FP
 * instruction such a process executes traps into
 * copr_not_available_handler(), which saves the owner's registers, restores
 * the newcomer's and makes it the owner. A process that never touches FP -
 * which is most of MINIX's servers - never pays for any of this.
 *
 * That scheme rests on one thing being true, and on this architecture it took
 * arranging: the kernel must not touch an FP register itself. Nothing saves
 * them on the way into an exception, so a kernel that used q0 would silently
 * change the arithmetic of whichever process happened to own it.
 *
 * The kernel's own C is built with -mgeneral-regs-only and always has been.
 * What did touch them was the code it borrows: printf() and the other
 * variadic entry points in libminc save q0..q7 into their argument save area,
 * which is harmless, but GCC also copies structures with q29..q31 in libminc,
 * libsys and libexec - libexec_pm_newexec() zeroed one with "movi v31.4s, #0".
 * Those four libraries are built with -mgeneral-regs-only for this
 * architecture too, which is where that rule is written down; the kernel now
 * contains no FP instruction outside fpu_asm.S. Check it with
 *
 *	aarch64-elf64-minix-objdump -d kernel | grep -E '[[:space:]][qv][0-9]+'
 *
 * and expect the two routines there and nothing else.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"

#include <assert.h>
#include <string.h>

#include <machine/fpu.h>

#include "archconst.h"
#include "arch_proto.h"

/* fpu_asm.S: the register file to and from a struct fpu_state. */
void fpu_save_regs(struct fpu_state *);
void fpu_restore_regs(const struct fpu_state *);

/*
 * CPACR_EL1.FPEN, bits [21:20]: 0b00 traps at EL0 and EL1 alike, 0b01 traps
 * at EL0 only, 0b11 does not trap at all.
 *
 * EL1 is never trapped. The kernel does use FP - in fpu_asm.S, to save and
 * restore what it is switching - and trapping itself would mean taking an
 * exception inside the handler for one. What EL0 may do is the whole of the
 * question here.
 */
#define CPACR_FPEN_SHIFT	20
#define CPACR_FPEN_MASK		(3UL << CPACR_FPEN_SHIFT)
#define CPACR_FPEN_TRAP_EL0	(1UL << CPACR_FPEN_SHIFT)
#define CPACR_FPEN_TRAP_NONE	(3UL << CPACR_FPEN_SHIFT)

static void
set_fpen(u64_t fpen)
{
	u64_t cpacr;

	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr = (cpacr & ~CPACR_FPEN_MASK) | fpen;
	__asm__ volatile("msr cpacr_el1, %0" :: "r"(cpacr));

	/*
	 * A change to CPACR_EL1 is not in force for instructions already in
	 * flight; without the isb the next FP instruction may be judged by
	 * the old value.
	 */
	__asm__ volatile("isb");
}

/*===========================================================================*
 *				fpu_init				     *
 *===========================================================================*/
void
fpu_init(void)
{
	/*
	 * Every ARMv8-A implementation has FP and SIMD - unlike VFP on
	 * ARMv7, they are not optional - so there is no feature to probe.
	 * What this flag says is that the kernel supports them, and it does.
	 */
	get_cpulocal_var(fpu_presence) = 1;

	set_fpen(CPACR_FPEN_TRAP_EL0);
}

/*===========================================================================*
 *				save_local_fpu				     *
 *===========================================================================*/
void
save_local_fpu(struct proc *pr, int retain)
{
	struct fpu_state *state = (struct fpu_state *)pr->p_seg.fpu_state;

	if (!is_fpu())
		return;

	assert(state);

	fpu_save_regs(state);

	/*
	 * retain says the caller wants the registers left as they are rather
	 * than treated as scratch. There is nothing to do for it: storing
	 * these registers does not disturb them. x87 needs the distinction
	 * because fnsave clears the unit and i386 has to reload after it.
	 */
	(void)retain;
}

/*===========================================================================*
 *				save_fpu				     *
 *===========================================================================*/
void
save_fpu(struct proc *pr)
{
#ifdef CONFIG_SMP
	if (cpuid != pr->p_cpu) {
		int stopped;

		/* Remember whether the process was already stopped. */
		stopped = RTS_ISSET(pr, RTS_PROC_STOP);

		/* Stop it where it runs and make it save its context. */
		smp_schedule_stop_proc_save_ctx(pr);

		if (!stopped)
			RTS_UNSET(pr, RTS_PROC_STOP);

		return;
	}
#endif

	if (get_cpulocal_var(fpu_owner) == pr) {
		disable_fpu_exception();
		save_local_fpu(pr, TRUE /*retain*/);
	}
}

/*===========================================================================*
 *				restore_fpu				     *
 *===========================================================================*/
int
restore_fpu(struct proc *pr)
{
	struct fpu_state *state = (struct fpu_state *)pr->p_seg.fpu_state;

	assert(state);

	if (!proc_used_fpu(pr)) {
		/*
		 * The first FP instruction this process has executed. It gets
		 * the architecture's default state - zeroed registers and an
		 * FPCR of zero, which is round-to-nearest with every
		 * exception untrapped. Clearing the buffer and loading it is
		 * the same thing as i386's fninit(), spelled the way this
		 * architecture allows.
		 */
		memset(state, 0, FPU_STATE_SIZE);
		pr->p_misc_flags |= MF_FPU_INITIALIZED;
	}

	fpu_restore_regs(state);

	/*
	 * Nothing here can fail. On i386 it can - a saved state can carry a
	 * reserved bit pattern that faults on the way back in, and the
	 * generic code turns that into SIGFPE for the process that owns it -
	 * but ldp of a q register interprets nothing, and FPCR is written
	 * from a word only this file ever produces.
	 */
	return OK;
}

/*===========================================================================*
 *			     enable_fpu_exception			     *
 *===========================================================================*/
void
enable_fpu_exception(void)
{
	set_fpen(CPACR_FPEN_TRAP_EL0);
}

/*===========================================================================*
 *			     disable_fpu_exception			     *
 *===========================================================================*/
void
disable_fpu_exception(void)
{
	set_fpen(CPACR_FPEN_TRAP_NONE);
}

/*===========================================================================*
 *				fpu_sigcontext				     *
 *===========================================================================*/
void
fpu_sigcontext(struct proc *pr, struct sigframe_sigcontext *fr,
	struct sigcontext *sc)
{
	(void)fr;

	if (!proc_used_fpu(pr))
		return;

	/* The live owner has the newest copy; make the saved area authoritative. */
	save_fpu(pr);
	assert(sizeof(sc->sc_fpu_state) == FPU_STATE_SIZE);
	memcpy(&sc->sc_fpu_state, pr->p_seg.fpu_state, FPU_STATE_SIZE);
}
