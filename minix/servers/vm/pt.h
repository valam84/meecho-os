
#ifndef _PT_H
#define _PT_H 1

#include <machine/vm.h>

#include "vm.h"
#include "pagetable.h"

/* A pagetable. */
typedef struct {
	/* The block of tables this address space is made of, and the
	 * physical address the hardware is given to start a walk at.
	 *
	 * On architectures that translate through the two levels this file
	 * works in, the block is the directory and the two pointers below
	 * are the same. Where there are more levels, the ones above the
	 * directory are a fixed spine at the front of the same block, and
	 * the walk starts there; see arch/aarch64/pagetable.h.
	 */
	pt_entry_t *pt_root;	/* aligned (ARCH_PAGEDIR_ALIGN) */
	phys_bytes pt_root_phys;	/* physical address of pt_root */

	/* Directory entries in VM addr space. */
	pt_entry_t *pt_dir;	/* ARCH_VM_DIR_ENTRIES of them */

	/* Pointers to page tables in VM address space. */
	pt_entry_t *pt_pt[ARCH_VM_DIR_ENTRIES];

	/* When looking for a hole in virtual address space, start
	 * looking here. This is in linear addresses, i.e.,
	 * not as the process sees it but the position in the page
	 * page table. This is just a hint.
	 */
	vir_bytes pt_virtop;
} pt_t;

#define CLICKSPERPAGE (VM_PAGE_SIZE/CLICK_SIZE)

#endif
