#ifndef _PAGETABLE_H
#define _PAGETABLE_H 1

#include <stdint.h>
#include <machine/vm.h>

#include "vm.h"

/*
 * Four levels of translation, seen through a file written for two.
 *
 * pagetable.c works in a page directory and a page table: one array of
 * entries indexed by the high bits of an address, each naming a table of
 * entries indexed by the low bits, each naming a page. VMSAv8-64 with a 4 KiB
 * granule has four such arrays, so the other two ports' pagetable.h - which
 * is a list of renamings and nothing else - has no counterpart here.
 *
 *
 * What is a directory here
 * ------------------------
 * The bottom two hardware levels are exactly the shape pagetable.c wants: an
 * L2 table of 512 entries, each naming an L3 table of 512 entries, each
 * naming a 4 KiB page. What does not fit is that one L2 table spans only
 * 1 GiB, and a process address space is four of those.
 *
 * So the four L2 tables of a process are allocated side by side, physically
 * contiguous, and treated as one flat array of 2048 entries indexed by
 * address bits [32:21]. That array is pt_dir. It is not a fiction maintained
 * beside the hardware's tables - it is the hardware's tables, four of them
 * that happen to adjoin, and the L1 entries above name the four pages in
 * order. Nothing has to be kept in step, because there is only one copy.
 *
 * Above them sits a spine of two tables that never changes after pt_new()
 * builds it: an L1 whose first four entries name the four L2 pages, and an
 * L0 whose first entry names the L1. The spine is allocated in the same
 * block, which is why ARCH_PAGEDIR_SIZE is six pages: L0, L1, then the four
 * L2 tables. One allocation, one free, one pool of spares during bootstrap.
 *
 * pt_dir_phys is the physical address of that block, which is the L0 table,
 * which is what TTBR0_EL1 is given - so pt_bind() hands the kernel the right
 * number without knowing any of this.
 *
 *
 * Which is to say the address space is 4 GiB
 * ------------------------------------------
 * 2048 entries of 2 MiB. The ceiling is USR_DATATOP, still the 0xF0000000
 * the 32-bit ports use, and this covers it with the room above spare for the
 * kernel's own mappings (see ARCH_KERNMAP_BASE below).
 *
 * It is a decision to revisit, not a limit of the architecture: the tables
 * translate 48 bits and the kernel's TCR_EL1.T0SZ is set for all of them.
 * Going beyond 4 GiB means pt_pt[] can no longer be a flat array - at
 * 2 MiB per entry a 256 TiB space would need 128M of them - so it is the
 * shape of pagetable.c that would have to change, not this header. Until
 * something needs the room, four gigabytes of user address space costs
 * 16 KiB of pointers per process and answers the question.
 */

/*
 * A translation table entry. Eight bytes here, four on the 32-bit ports,
 * which is why this is a type and not a promise about u32_t.
 */
typedef u64_t pt_entry_t;

#define VM_PAGE_SIZE		AARCH64_PAGE_SIZE

/* The leaf level: an L3 table, 512 entries of one page each. */
#define ARCH_VM_PT_ENTRIES	AARCH64_VM_PT_ENTRIES
#define ARCH_VM_PTE(v)		AARCH64_VM_INDEX(v, 3)

/* The directory: the four L2 tables read as one array. */
#define ARCH_VM_DIR_ENTRIES	2048
#define ARCH_VM_PDE(v)		((unsigned long)(v) >> AARCH64_VM_L2_SHIFT)

/* L0, L1, then the four L2 tables. */
#define ARCH_PT_SPINE_PAGES	2
#define ARCH_PT_DIR_PAGES	(ARCH_VM_DIR_ENTRIES / AARCH64_VM_PT_ENTRIES)
#define ARCH_PAGEDIR_SIZE	\
	((ARCH_PT_SPINE_PAGES + ARCH_PT_DIR_PAGES) * VM_PAGE_SIZE)

/*
 * Alignment the block has to be allocated at, which is not its size.
 *
 * On the 32-bit ports the two are the same, because a TTBR or a CR3 wants
 * the whole directory aligned to its own size. Here the register is given a
 * single 4 KiB table and every other table is reached through a descriptor,
 * so page alignment is all the hardware asks - and ARCH_PAGEDIR_SIZE is six
 * pages, which is not a power of two and could not be an alignment anyway.
 */
#define ARCH_PAGEDIR_ALIGN	VM_PAGE_SIZE

/* Where the directory starts inside the block. */
#define ARCH_PT_DIR_OFFSET	(ARCH_PT_SPINE_PAGES * VM_PAGE_SIZE)

/*
 * An L2 block descriptor: 2 MiB mapped without an L3 table.
 *
 * VM never writes one - the kernel maps itself in TTBR1 and nothing else
 * wants a big page - but the assertions that say so have to be able to ask,
 * and the answer is not a single bit here: at levels 0 to 2 a valid
 * descriptor is a table if bit 1 is set and a block if it is clear. Hence a
 * macro where the other ports have a mask.
 */
#define ARCH_BIG_PAGE_SIZE	AARCH64_VM_L2_BLOCK_SIZE
#define ARCH_VM_IS_BIGPAGE(e)	\
	(((e) & AARCH64_VM_VALID) && !((e) & AARCH64_VM_TABLE))

/*
 * Mapping flags.
 *
 * PTF_PRESENT carries more than presence, because several bits have to be on
 * in every entry VM writes and there is nowhere else to put them:
 *
 *  - PAGE, because at level 3 a descriptor with bit 1 clear is reserved, not
 *    a block;
 *  - AF, the access flag, because nothing here handles access flag faults -
 *    an entry with AF clear faults on first touch and would be reported as a
 *    fault on a page that is plainly mapped;
 *  - NG, so the TLB entry carries an ASID and does not outlive its address
 *    space once ASIDs are allocated;
 *  - PXN, because EL1 must never execute a page EL0 can write.
 *
 * Not UXN. These are the pages a process runs from, and MINIX's region
 * flags do not say which of them hold code: pt_writemap() is told an address
 * and a length. So user pages are executable, as they are on i386 without
 * NX. Distinguishing them is a change to the region layer, not to this file.
 */
#define PTF_PRESENT	(AARCH64_VM_VALID | AARCH64_VM_PAGE | \
			 AARCH64_VM_AF | AARCH64_VM_NG | AARCH64_VM_PXN)

/*
 * Access permissions are a two-bit field, not two independent bits: writable
 * is the absence of AP[2], and reachable from EL0 is AP[1]. PTF_WRITE is
 * therefore zero and means "do not set the read-only bit", the same shape
 * earm uses.
 */
#define PTF_WRITE	0UL
#define PTF_READ	AARCH64_VM_AP_RO
#define PTF_USER	AARCH64_VM_AP_RW_ALL
#define PTF_SUPER	0UL		/* EL1 only: AP[1] clear */

#define PTF_NOCACHE	AARCH64_VM_PTE_DEVICE
#define PTF_CACHEWB	AARCH64_VM_PTE_CACHED
/*
 * Write-through has no MAIR index of its own. The four the kernel programs
 * are device, device-nGnRE, normal write-back and normal non-cacheable; a
 * fifth would have to be agreed with the kernel, and nothing in the tree
 * asks for write-through. Callers that ask get write-back, which is correct
 * for every use and only ever faster.
 */
#define PTF_CACHEWT	AARCH64_VM_PTE_CACHED
#define PTF_SHARE	AARCH64_VM_SH_INNER

#define PTF_ALLFLAGS	(PTF_READ | PTF_WRITE | PTF_PRESENT | PTF_SUPER | \
			 PTF_USER | PTF_NOCACHE | PTF_CACHEWB | \
			 PTF_CACHEWT | PTF_SHARE)

/*
 * The same values under the names pagetable.c uses when it is writing an
 * entry rather than describing a region.
 *
 * ARCH_VM_PTE_PRESENT is tested as a mask as well as written, and that works
 * because an entry is either zero or fully formed: the bits above are always
 * written together, so any one of them being set means the entry is live.
 * The cleared entry pt_writemap() leaves behind - MAP_NONE with no flags -
 * has none of them.
 */
#define ARCH_VM_PTE_PRESENT	PTF_PRESENT
#define ARCH_VM_PTE_USER	PTF_USER
#define ARCH_VM_PTE_RW		PTF_WRITE
#define ARCH_VM_PTE_RO		PTF_READ
#define ARCH_VM_PTE_CACHED	AARCH64_VM_PTE_CACHED
#define ARCH_VM_PTE_SUPER	PTF_SUPER

/* A directory entry names a table, never a block. */
#define ARCH_VM_PDE_PRESENT	(AARCH64_VM_VALID | AARCH64_VM_TABLE)
#define ARCH_VM_PDE_MASK	AARCH64_VM_ADDR_MASK
#define ARCH_VM_ADDR_MASK	AARCH64_VM_ADDR_MASK

/*
 * The questions pagetable.c has to ask an entry that no single mask answers
 * on every architecture: see arch/i386/pagetable.h.
 *
 * A table descriptor carries no permissions of its own here. It can - bits
 * 59 to 62 restrict what the levels below may do - but leaving them clear
 * means the leaf entry alone decides, which is what pagetable.c assumes when
 * it writes a directory entry once and changes protections in the table.
 */
#define ARCH_VM_PTE_ISWRITABLE(e)	(!((e) & AARCH64_VM_AP_RO))
#define ARCH_VM_PDE_MAKE(phys, flags)	\
	(((phys) & AARCH64_VM_ADDR_MASK) | ARCH_VM_PDE_PRESENT)

/*
 * The kernel is in TTBR1 and no process page table describes it. This is the
 * one architectural difference pagetable.c cannot paper over with a
 * renaming: whole pieces of it - mapping the kernel into every address
 * space, reserving directory entries for it, publishing page directories
 * back to it - exist only because i386 and ARM have one table for both.
 */
#define ARCH_VM_KERNEL_IN_PROC_PT	0

/*
 * Where the kernel's own mappings live in every process's address space.
 *
 * The kernel does not need mapping here at all - it is in TTBR1, which no
 * process shares and none can name - but the pages it publishes to user
 * space do: minix_kerninfo and what it points at. They go above the
 * process's data top, in the part of the directory that regions never reach,
 * which is the same place i386 puts them and for the same reason.
 */
#define ARCH_KERNMAP_BASE	((vir_bytes)VM_DATATOP)

/*
 * Page fault syndrome, as the kernel sends it: ESR_EL1.ISS. An access flag
 * fault counts as a missing page - VM's answer to both is to map one - and
 * would only arise from an entry this file did not write.
 */
#define PFERR_PROT(e)	AARCH64_VM_PFE_IS_PERM(e)
#define PFERR_NOPAGE(e)	(AARCH64_VM_PFE_IS_TRANS(e) || \
			 AARCH64_VM_PFE_IS_ACCESS(e))
/*
 * WnR, which exists only in a data abort's syndrome. An instruction abort
 * leaves the bit clear and so reads as a read, which is what it is.
 */
#define PFERR_WRITE(e)	((e) & AARCH64_VM_ISS_WNR)
#define PFERR_READ(e)	(!((e) & AARCH64_VM_ISS_WNR))

#endif
