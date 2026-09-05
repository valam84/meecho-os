/*
 * What the kernel sets up once it is running in the upper half, on the way
 * from kmain() to the first process.
 *
 * The file keeps the name the other two ports use, where it is about
 * protected mode and the descriptor tables that define it. There is no
 * equivalent here - AArch64 has no segmentation, and the protection the
 * kernel relies on is in the page tables pg_utils.c already built - so what
 * is left is the part of that sequence which genuinely cannot happen in
 * pre_init(): making the page table the boot process will run in, and handing
 * it to VM.
 */

#include "kernel/kernel.h"

#include <assert.h>
#include <string.h>

#include <machine/vm.h>

#include "archconst.h"
#include "arch_proto.h"
#include "kernel/proc.h"

int prot_init_done = 0;

/*===========================================================================*
 *				prot_init				     *
 *===========================================================================*/
void
prot_init(void)
{
	/*
	 * A page table for the lower half, so that VM can be loaded into it
	 * and run. Until now the lower half has held nothing: pre_init() used
	 * TTBR0 for the identity map and then switched TTBR0 walks off
	 * altogether, so that a leftover physical pointer would fault.
	 *
	 * The 32-bit ports rebuild their kernel mapping at this point too,
	 * because the one pre_init made lives in memory that is about to be
	 * freed. Here the kernel's mapping is ordinary kernel memory and sits
	 * in TTBR1, which nothing switches and nothing frees, so there is
	 * nothing to redo.
	 *
	 * VBAR_EL1 is written here on ARM, to point at the exception vectors.
	 * On this architecture it is written twice and much earlier - with
	 * the table's physical address before the MMU comes on, and with its
	 * virtual address right after the move - because a fault in between
	 * has to be diagnosable. That lands with the exception path.
	 */
	pg_clear();
	pg_load();

	prot_init_done = 1;
}

/*===========================================================================*
 *				arch_post_init				     *
 *===========================================================================*/
void
arch_post_init(void)
{
	struct proc *vm;

	/*
	 * Tell the memory code which address space is loaded. VM is the boot
	 * process and it runs in the bootstrap page table, so the two are the
	 * same thing until VM has built one of its own.
	 */
	vm = proc_addr(VM_PROC_NR);
	get_cpulocal_var(ptproc) = vm;
	pg_info(&vm->p_seg.p_ttbr, &vm->p_seg.p_ttbr_v);
}
