/* The kernel call implemented in this file:
 *   m_type:	SYS_VMCTL
 *
 * The parameters for this kernel call are:
 *   	SVMCTL_WHO	which process
 *    	SVMCTL_PARAM	set this setting (VMCTL_*)
 *    	SVMCTL_VALUE	to this value
 *
 * The architecture-specific half: everything here is about TTBR0, which is
 * the only thing that changes when an address space does. The kernel's own
 * mapping is in TTBR1 and is never rebuilt, so the requests the two 32-bit
 * ports answer about their kernel window - VMCTL_NOPAGEZERO,
 * VMCTL_I386_KERNELLIMIT - have no counterpart.
 */

#include "kernel/system.h"

#include <assert.h>
#include <minix/type.h>

#include <machine/vm.h>

#include "arch_proto.h"

/*===========================================================================*
 *				set_ttbr				     *
 *===========================================================================*/
static void
set_ttbr(struct proc *p, phys_bytes ttbr, u64_t *v)
{
	p->p_seg.p_ttbr = ttbr;
	assert(p->p_seg.p_ttbr);

	/*
	 * The root table as VM sees it. The kernel does not dereference it -
	 * it reaches any table through phys2vir() of the address above - but
	 * VM asks for it back through VMCTL_GET_PDBR, and release_address_space()
	 * uses it as the record that the space is gone.
	 */
	p->p_seg.p_ttbr_v = v;

	if (p == get_cpulocal_var(ptproc))
		write_ttbr0(p->p_seg.p_ttbr);

	if (p->p_nr == VM_PROC_NR) {
		if (arch_enable_paging(p) != OK)
			panic("arch_enable_paging failed");
	}

	RTS_UNSET(p, RTS_VMINHIBIT);
}

/*===========================================================================*
 *				arch_do_vmctl				     *
 *===========================================================================*/
int
arch_do_vmctl(register message *m_ptr, struct proc *p)
{
	switch (m_ptr->SVMCTL_PARAM) {
	case VMCTL_GET_PDBR:
		/*
		 * The physical address of the process's root table.
		 *
		 * Not p_ttbr as it stands: a TTBR value carries the ASID in
		 * its top sixteen bits, and VM is asking for an address. The
		 * two are the same number today only because every address
		 * space is still ASID 0.
		 */
		m_ptr->SVMCTL_VALUE = p->p_seg.p_ttbr & AARCH64_VM_ADDR_MASK;
		return OK;

	case VMCTL_SETADDRSPACE:
		/*
		 * SVMCTL_PTROOT is m1_i3, an int, and the value in it is a
		 * physical address. On LP64 that matters twice over: a root
		 * at or above 2 GiB arrives with its sign bit set, and
		 * assigning it straight to phys_bytes would sign-extend it
		 * into 0xffffffff_8xxxxxxx. QEMU's virt machine puts RAM at
		 * 0x4000_0000, so a machine with a gigabyte of memory reaches
		 * that on its own. The cast through u32_t is what keeps the
		 * field's 32 bits meaning what VM put in them.
		 *
		 * Widening the field would not buy anything yet: VM allocates
		 * these tables out of memory the kernel already truncates at
		 * VM_MAX_PHYS_MEM, for the same reason - servers/vm/pt.c
		 * addresses physical memory in 32 bits. Both ceilings lift
		 * together, when that file learns about four levels of
		 * tables. See <machine/memory.h>.
		 */
		set_ttbr(p, (phys_bytes)(u32_t)m_ptr->SVMCTL_PTROOT,
		    (u64_t *)m_ptr->SVMCTL_PTROOT_V);
		return OK;

	case VMCTL_FLUSHTLB:
		refresh_tlb();
		return OK;
	}

	printf("arch_do_vmctl: strange param %d\n", m_ptr->SVMCTL_PARAM);
	return EINVAL;
}
