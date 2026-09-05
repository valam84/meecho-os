/*
 * FP/SIMD - not implemented yet, and deliberately failing loudly rather than
 * quietly.
 *
 * The generic kernel switches the FPU lazily: the owner of the registers is
 * tracked per CPU, a process that is not the owner runs with FP trapping, and
 * the trap handler saves the old owner's state and restores the new one's.
 * Everything above the four calls in this file already works that way.
 *
 * What is missing is the state itself: 32 128-bit V registers plus FPSR and
 * FPCR, 528 bytes per process, and the pool to keep them in. That is stage 4
 * group 5 and it is written from scratch - the bring-up kernel never enabled
 * FP, and ARM has nothing to copy because MINIX/ARM is built soft-float.
 *
 *
 * Why these stubs refuse instead of pretending
 * --------------------------------------------
 * ARM's versions of these are empty and restore_fpu() there returns OK. That
 * is harmless on a soft-float port: nothing ever touches a V register, so
 * claiming the state was restored costs nothing.
 *
 * It would not be harmless here. The AArch64 procedure call standard passes
 * floating-point arguments in v0..v7, and the libc this port builds uses SIMD
 * registers in memcpy and friends - so on this architecture user code touches
 * FP state immediately and constantly. Stubs that answered OK would let two
 * processes share one set of registers and corrupt each other silently, which
 * is the worst way for an unimplemented feature to behave.
 *
 * So restore_fpu() reports failure, the generic code turns that into SIGFPE
 * for the process that asked, and FP trapping is left on. Until group 5 lands
 * this kernel boots and runs anything that does not touch FP, and anything
 * that does gets a signal naming the problem instead of wrong arithmetic
 * somewhere else later.
 *
 * The trap itself - EC 0x07, "access to SVE, Advanced SIMD or FP" - has to
 * reach copr_not_available_handler() from the exception path, which arrives
 * with group 4.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"

#include <assert.h>

#include "archconst.h"
#include "arch_proto.h"

/* CPACR_EL1.FPEN, bits [21:20]. 0b11 is "do not trap"; 0b00 traps at EL0
 * and EL1 alike, which is the reset value and the one we keep. */
#define CPACR_FPEN_SHIFT	20
#define CPACR_FPEN_MASK		(3UL << CPACR_FPEN_SHIFT)
#define CPACR_FPEN_TRAP_ALL	(0UL << CPACR_FPEN_SHIFT)
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
	 * Every Cortex-A72 has FP and SIMD - they are not optional in ARMv8-A
	 * the way VFP was in ARMv7 - so this says "no FPU" about the kernel's
	 * support for it, not about the hardware. is_fpu() answers from this
	 * flag, and the generic kernel is written to work when it is false.
	 */
	get_cpulocal_var(fpu_presence) = 0;

	set_fpen(CPACR_FPEN_TRAP_ALL);
}

/*===========================================================================*
 *				save_local_fpu				     *
 *===========================================================================*/
void
save_local_fpu(struct proc *pr, int retain)
{
	/*
	 * Only ever called for the current owner of the registers, and there
	 * is never an owner: restore_fpu() below refuses, so the generic code
	 * never records one.
	 */
	assert(get_cpulocal_var(fpu_owner) == NULL);
}

/*===========================================================================*
 *				save_fpu				     *
 *===========================================================================*/
void
save_fpu(struct proc *pr)
{
	assert(get_cpulocal_var(fpu_owner) == NULL);
}

/*===========================================================================*
 *				restore_fpu				     *
 *===========================================================================*/
int
restore_fpu(struct proc *pr)
{
	/*
	 * There is no saved state to put back and nowhere to have kept it.
	 * Failing here is what turns a process's first FP instruction into a
	 * SIGFPE for that process - a diagnosable event naming the process
	 * that did it - rather than into shared registers.
	 */
	return EINVAL;
}

/*===========================================================================*
 *			     enable_fpu_exception			     *
 *===========================================================================*/
void
enable_fpu_exception(void)
{
	set_fpen(CPACR_FPEN_TRAP_ALL);
}

/*===========================================================================*
 *			     disable_fpu_exception			     *
 *===========================================================================*/
void
disable_fpu_exception(void)
{
	/*
	 * Called on the way into copr_not_available_handler() so that the
	 * handler itself could touch FP state. This one does not - it is
	 * about to refuse - and leaving the trap on is what keeps a process
	 * from running FP instructions with nobody saving them. The window
	 * the generic code would otherwise open closes on the next entry to
	 * user mode, which re-enables the trap; this closes it now.
	 *
	 * Becomes set_fpen(CPACR_FPEN_TRAP_NONE) when group 5 has state to
	 * restore.
	 */
}

/*===========================================================================*
 *				fpu_sigcontext				     *
 *===========================================================================*/
void
fpu_sigcontext(struct proc *pr, struct sigframe_sigcontext *fr,
	struct sigcontext *sc)
{
	/*
	 * Where the FP state would be copied into the signal frame, so that a
	 * handler sees the arithmetic state the signal interrupted and
	 * sigreturn puts it back. There is no state to copy yet.
	 *
	 * struct sigcontext here was written at stage 1 to mirror
	 * stackframe_s so the kernel can copy it as a block; the FP half of
	 * it is what group 5 adds, and the layout has to be settled with
	 * libc, not just with the kernel. ARM leaves this empty for the same
	 * reason it leaves the rest empty.
	 */
}
