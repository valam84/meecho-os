/*
 * Privileged Access Never: the kernel may not load or store through an
 * address a process can reach.
 *
 * What it protects against is the kernel's own mistakes. Everything the
 * kernel does with a process's memory goes through the linear map - an
 * address in the upper half, resolved by walking the process's tables first
 * (virtual_copy_f, vm_memset, cachectl; see memory.c) - and PAN does not
 * touch the linear map at all. The two message copies in usercopy.S reach
 * the process's own addresses, and they use LDTR and STTR, which are checked
 * as EL0 accesses and which PAN therefore does not stop either. So with PAN
 * on, an EL1 load or store at a user address is never the kernel's intent:
 * it is a pointer that came from a process and was dereferenced by mistake,
 * and the fault turns a silent read of whatever the process placed there
 * into a kernel exception with the address in it.
 *
 * The bit is per core and per exception. PSTATE.PAN is set here once, and
 * SCTLR_EL1.SPAN is cleared, which tells the core to set PSTATE.PAN on every
 * exception taken to EL1. Leaving SPAN at its reset value of one would leave
 * PAN as it was in the interrupted context - and the context interrupted is
 * nearly always EL0, whose SPSR carries a PAN of zero (INIT_PSR and every
 * process frame built from it). PAN would be on until the first system call
 * and off from then on. Linux makes the same choice for the same reason.
 *
 * SPAN is one of the bits head.S writes as one because it is RES1 on
 * ARMv8.0, so it is cleared only on a core that has PAN (ID_AA64MMFR1_EL1
 * says so): a core without it gets the register it always had.
 *
 * PSTATE.UAO, the ARMv8.2 bit that makes LDTR/STTR behave as ordinary
 * accesses, is never set by this kernel, and taking an exception to EL1
 * clears it - so usercopy.S always gets the unprivileged access it asks for.
 *
 * One place writes to user addresses from EL1 on purpose: arch_boot_proc()
 * loads VM through the bootstrap page table at VM's own addresses, because
 * VM is the one process that cannot be loaded by VM. It opens a window with
 * pan_user_access_begin() and closes it after.
 *
 * And the setting is checked, not assumed: the first time a core takes an
 * interrupt from a process, it loads a byte of that process's stack with an
 * ordinary instruction - after the process has entered the kernel at least
 * once, so a wrong SPAN would show - and expects a permission fault. That
 * is pan_verify(), and its one line per core at boot is the evidence that
 * the core enforces what this file asked for. See port/PORTING-LOG.md,
 * "PAN".
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/proto.h"

#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "trap.h"

/* ID_AA64MMFR1_EL1.PAN: 0 none, 1 PAN, 2 and 3 PAN with later additions. */
#define ID_AA64MMFR1_PAN(r)	(((r) >> 20) & 0xf)

/* SCTLR_EL1.SPAN: zero sets PSTATE.PAN on every exception to EL1. */
#define SCTLR_EL1_SPAN		((u64_t)1 << 23)

/*
 * PSTATE.PAN by the immediate form, written out: the assembler is told
 * ARMv8.0 and knows no "pan". The register form, S3_0_C4_C2_3 with the
 * bit at 22, is used to read it back.
 */
#define PAN_SET()	__asm__ volatile(".inst 0xd500419f" ::: "memory")
#define PAN_CLEAR()	__asm__ volatile(".inst 0xd500409f" ::: "memory")
#define PAN_BIT		((u64_t)1 << 22)

/* What this core has, and how far the check has got. */
#define PAN_ABSENT	0
#define PAN_ON		1	/* enabled, not yet seen to fault */
#define PAN_VERIFIED	2	/* a read of a user address has faulted */
#define PAN_FAILED	3	/* a read of a user address went through */

static int pan_state[CONFIG_MAX_CPUS];

/*===========================================================================*
 *				pan_init				     *
 *===========================================================================*/
/* On every core, from arch_init(). */
void
pan_init(void)
{
	unsigned cpu = cpuid;
	u64_t mmfr1, sctlr, pstate;

	__asm__ volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(mmfr1));

	if (ID_AA64MMFR1_PAN(mmfr1) == 0) {
		pan_state[cpu] = PAN_ABSENT;
		if (cpu_is_bsp(cpu))
			printf("PAN: not implemented by this core\n");
		return;
	}

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr &= ~SCTLR_EL1_SPAN;
	__asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(sctlr) : "memory");

	PAN_SET();
	__asm__ volatile("isb; mrs %0, s3_0_c4_c2_3" : "=r"(pstate));

	if (!(pstate & PAN_BIT))
		panic("PAN: cpu %u reports PAN but did not take the bit", cpu);

	pan_state[cpu] = PAN_ON;
}

/*===========================================================================*
 *			pan_user_access_begin / end			     *
 *===========================================================================*/
/*
 * The window arch_boot_proc() loads VM in. Nothing else may open one: a
 * copy to or from a process belongs in usercopy.S or behind the linear map.
 */
void
pan_user_access_begin(void)
{
	if (pan_state[cpuid] != PAN_ABSENT)
		PAN_CLEAR();
}

void
pan_user_access_end(void)
{
	if (pan_state[cpuid] != PAN_ABSENT)
		PAN_SET();
}

/*===========================================================================*
 *				pan_verify				     *
 *===========================================================================*/
/*
 * From an interrupt taken at EL0, until it has an answer. pr is the process
 * the interrupt came from, so its tables are the ones in TTBR0 and its
 * stack pointer is an address it has just been using.
 *
 * The answer has to be a permission fault. A translation fault says the
 * page is not there - VM has not mapped it yet, or the stack pointer sits on
 * a page boundary - and says nothing about PAN, so the next interrupt asks
 * again.
 */
void
pan_verify(struct proc *pr)
{
	unsigned cpu = cpuid;
	u64_t esr;
	unsigned fsc;

	if (pan_state[cpu] != PAN_ON)
		return;

	if (get_cpulocal_var(ptproc) != pr)
		return;

	esr = kernel_probe_read((const void *)pr->p_reg.sp);

	if (esr == 0) {
		pan_state[cpu] = PAN_FAILED;
		printf("PAN: FAILED on cpu %u: the kernel read %s's stack at "
		    "0x%lx without a fault\n", cpu, pr->p_name,
		    (unsigned long)pr->p_reg.sp);
		return;
	}

	fsc = (unsigned)(ESR_ISS(esr) & AARCH64_VM_FSC_MASK);
	if (!AARCH64_VM_PFE_IS_PERM(fsc))
		return;

	pan_state[cpu] = PAN_VERIFIED;
	printf("PAN: on, cpu %u: a kernel read of %s's stack faults "
	    "(esr 0x%lx)\n", cpu, pr->p_name, (unsigned long)esr);
}
