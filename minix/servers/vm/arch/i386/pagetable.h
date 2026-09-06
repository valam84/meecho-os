
#ifndef _PAGETABLE_H
#define _PAGETABLE_H 1

#include <stdint.h>
#include <machine/vm.h>

#include "vm.h"

/*
 * A translation table entry, and with it the width of everything made out of
 * one. Four bytes here; eight where the architecture translates through more
 * levels than the two pagetable.c works in.
 */
typedef u32_t pt_entry_t;

/* Mapping flags. */
#define PTF_WRITE	I386_VM_WRITE
#define PTF_READ	I386_VM_READ
#define PTF_PRESENT	I386_VM_PRESENT
#define PTF_USER	I386_VM_USER
#define PTF_GLOBAL	I386_VM_GLOBAL
#define PTF_NOCACHE	(I386_VM_PWT | I386_VM_PCD)

#define ARCH_VM_DIR_ENTRIES	I386_VM_DIR_ENTRIES
#define ARCH_BIG_PAGE_SIZE	I386_BIG_PAGE_SIZE
#define ARCH_VM_ADDR_MASK	I386_VM_ADDR_MASK
#define ARCH_VM_PAGE_PRESENT    I386_VM_PRESENT
/* The address field of a page directory entry. I386_VM_PDE_MASK, which this
 * named until now, does not exist in <machine/vm.h> and never has: nothing
 * referred to it, because every i386 path spelled out I386_VM_ADDR_MASK
 * instead. It is the same field.
 */
#define ARCH_VM_PDE_MASK        I386_VM_ADDR_MASK
#define ARCH_VM_PDE_PRESENT     I386_VM_PRESENT
#define ARCH_VM_PTE_PRESENT	I386_VM_PRESENT
#define ARCH_VM_PTE_USER	I386_VM_USER
#define ARCH_VM_PTE_RW		I386_VM_WRITE
#define ARCH_PAGEDIR_SIZE	I386_PAGE_SIZE
#define ARCH_VM_BIGPAGE		I386_VM_BIGPAGE
#define ARCH_VM_PT_ENTRIES      I386_VM_PT_ENTRIES

/*
 * The three questions pagetable.c has to ask an entry that it cannot ask
 * with one mask on every architecture.
 *
 * Read-only is the absence of a bit here and the presence of one on ARM;
 * a big page is a bit here and the absence of one where a descriptor says
 * table-or-block; a directory entry is assembled differently everywhere.
 * They used to be spelled out at each use with #if defined(__i386__) /
 * #elif defined(__arm__), which is how a third port turns into three times
 * the branches.
 */
#define ARCH_VM_PTE_ISWRITABLE(e)	((e) & I386_VM_WRITE)
#define ARCH_VM_IS_BIGPAGE(e)		((e) & I386_VM_BIGPAGE)
#define ARCH_VM_PDE_MAKE(phys, flags)	\
	(((phys) & I386_VM_ADDR_MASK) | (flags) | \
	 I386_VM_PRESENT | I386_VM_USER | I386_VM_WRITE)

/* Alignment the page directory has to be allocated at: its own size. */
#define ARCH_PAGEDIR_ALIGN	ARCH_PAGEDIR_SIZE

/* No read-only bit and no cacheability bits in a normal mapping. */
#define ARCH_VM_PTE_RO		0
#define ARCH_VM_PTE_CACHED	0
#define ARCH_VM_PTE_SUPER	0

/* Two levels of translation: the directory is the whole of the block, and
 * nothing sits above it. */
#define ARCH_PT_SPINE_PAGES	0

/* The kernel lives in the same page table as the process. */
#define ARCH_VM_KERNEL_IN_PROC_PT	1

/* For arch-specific PT routines to check if no bits outside
 * the regular flags are set.
 */
#define PTF_ALLFLAGS   (PTF_READ|PTF_WRITE|PTF_PRESENT|PTF_USER|PTF_GLOBAL|PTF_NOCACHE)

#define PFERR_NOPAGE(e)	(!((e) & I386_VM_PFE_P))
#define PFERR_PROT(e)	(((e) & I386_VM_PFE_P))
#define PFERR_WRITE(e)	((e) & I386_VM_PFE_W)
#define PFERR_READ(e)	(!((e) & I386_VM_PFE_W))

#define VM_PAGE_SIZE	I386_PAGE_SIZE

/* virtual address -> pde, pte macros */
#define ARCH_VM_PTE(v) I386_VM_PTE(v)
#define ARCH_VM_PDE(v) I386_VM_PDE(v)

#endif
