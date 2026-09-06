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
	unsigned asid = 0;

	if (pg_asids_usable()) {
		/*
		 * The tag is the process slot plus one. It needs no allocator
		 * and nothing to release: a slot holds one address space at a
		 * time, so the tags of live spaces are distinct by
		 * construction, and zero is left to the table the kernel
		 * booted on.
		 *
		 * A slot does outlive the address space in it, though - the
		 * next process to land here gets the same tag over a
		 * different set of tables - so whatever the TLB still holds
		 * under this tag has to go before the new root is installed.
		 * On every core, not just this one: the previous occupant may
		 * have run anywhere.
		 */
		asid = AARCH64_PROC_ASID(p);
		refresh_tlb_asid(asid);
	}

	p->p_seg.p_ttbr = ttbr | AARCH64_TTBR_ASID(asid);
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
		/*
		 * VM has changed some process's mappings - and does not say
		 * whose. Every caller in servers/vm asks as
		 * sys_vmctl(SELF, VMCTL_FLUSHTLB, 0), so SVMCTL_WHO resolves
		 * to VM itself no matter which address space was rewritten.
		 *
		 * So the whole TLB goes, inner-shareable, rather than one
		 * ASID's worth. Retiring by tag here looks like the tagged
		 * scheme paying off and is simply wrong: it drops VM's own
		 * entries and keeps the ones belonging to the process whose
		 * page table just changed.
		 *
		 * That mistake cost the first boot on the CB2. ds took a page
		 * fault, VM mapped the page and flushed - its own tag - and
		 * the fault came straight back, forever; the console showed
		 * nothing but VMINHIBIT_SET, VMINHIBIT_CLEAR and FLUSHTLB
		 * repeating for one endpoint. It cannot happen under QEMU,
		 * whose TCG TLB is not tagged at all - which is precisely the
		 * warning already recorded for this scheme: an emulator can
		 * show that tagging is correct and cannot show what it is for.
		 *
		 * The tags still earn their keep where they were meant to:
		 * __switch_address_space() retires nothing on a context
		 * switch. This path runs when a mapping changes, which is
		 * rarer, and where correctness is not negotiable.
		 */
		refresh_tlb_all_is();
		return OK;
	}

	printf("arch_do_vmctl: strange param %d\n", m_ptr->SVMCTL_PARAM);
	return EINVAL;
}
