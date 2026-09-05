/*
 * AArch64 address translation: build the tables, turn the MMU on, and move
 * the kernel into the upper half of the address space.
 *
 *
 * How the addressing works before the switch
 * ------------------------------------------
 * The kernel is linked at its virtual address (0xffff_0000_4000_0000) but
 * loaded at, and entered at, its physical one (0x4000_0000). Everything in
 * this file therefore runs at an address 0xffff_0000_0000_0000 below the one
 * the linker assigned it.
 *
 * That works because AArch64's small code model - the default, and what
 * Makefile.bringup asks for explicitly - reaches every symbol with adrp/add,
 * and adrp is PC-relative. What gets encoded is the link-time distance
 * between two parts of the image, so the whole image can execute at any
 * offset as long as it moves as a unit.
 *
 * The practical consequence, and the thing to remember when reading this
 * file: taking the address of a kernel symbol yields a physical address
 * before the switch and a virtual one after it. Both are correct - it is the
 * same object, named in whichever address space the code is running in.
 *
 * What must not appear in the pre-switch path is an address the linker
 * resolved to a constant: an initialised pointer in .data, a jump table, an
 * "ldr x0, =symbol". Those hold the upper-half value and are unusable until
 * the MMU is on. Nothing here uses any of them.
 *
 * The table-building routines dereference descriptor addresses directly,
 * which is only valid while VA equals PA. They are for boot; once paging is
 * live, mapping has to go through the kernel map instead.
 */

#include <stdint.h>

#include "bsp_serial.h"
#include "kprint.h"
#include "mmu.h"

/* Section boundaries from kernel.lds. */
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[];
extern char __kernel_end[];

/* Top of the boot stack, from head.S. */
extern char boot_stack_top[];

/*
 * Translation table format
 * ------------------------
 * 4 KiB granule, 48-bit virtual addresses, four levels:
 *
 *	level 0	VA[47:39]	512 GiB per entry, table descriptors only
 *	level 1	VA[38:30]	  1 GiB per entry, block allowed
 *	level 2	VA[29:21]	  2 MiB per entry, block allowed
 *	level 3	VA[20:12]	  4 KiB per entry, page descriptors
 */
#define PTRS_PER_TABLE		512
#define LEVEL_SHIFT(level)	(39 - 9 * (level))

/* Descriptor type, bits [1:0]. */
#define PTE_VALID		(1UL << 0)
#define PTE_TABLE		(1UL << 1)	/* levels 0..2: next table */
#define PTE_BLOCK		(0UL << 1)	/* levels 1..2: block */
#define PTE_PAGE		(1UL << 1)	/* level 3: page */

/* Output address field of any descriptor, bits [47:12]. */
#define PTE_ADDR_MASK		0x0000fffffffff000UL

/* Lower attributes of a block or page descriptor. */
#define PTE_ATTRINDX(n)		((uint64_t)(n) << 2)
#define PTE_AP_RW_EL1		(0UL << 6)	/* EL1 read/write, EL0 none */
#define PTE_AP_RW_ALL		(1UL << 6)
#define PTE_AP_RO_EL1		(2UL << 6)	/* EL1 read-only, EL0 none */
#define PTE_AP_RO_ALL		(3UL << 6)
#define PTE_SH_NONE		(0UL << 8)
#define PTE_SH_INNER		(3UL << 8)
#define PTE_AF			(1UL << 10)	/* access flag */
#define PTE_NG			(1UL << 11)	/* not global */

/* Upper attributes. */
#define PTE_PXN			(1UL << 53)	/* no execute at EL1 */
#define PTE_UXN			(1UL << 54)	/* no execute at EL0 */

/*
 * MAIR_EL1 slots. Index 0 is deliberately the strictest device type, so that
 * a descriptor built from a zeroed attribute field describes memory that
 * cannot be speculated into or reordered. That is the safe way to be wrong.
 */
#define MAIR_IDX_DEVICE_nGnRnE	0
#define MAIR_IDX_DEVICE_nGnRE	1
#define MAIR_IDX_NORMAL		2
#define MAIR_IDX_NORMAL_NC	3

#define MAIR_ATTR_DEVICE_nGnRnE	0x00UL
#define MAIR_ATTR_DEVICE_nGnRE	0x04UL
#define MAIR_ATTR_NORMAL_WB	0xffUL	/* inner+outer write-back, RW-alloc */
#define MAIR_ATTR_NORMAL_NC	0x44UL	/* inner+outer non-cacheable */

#define MAIR_VALUE \
	((MAIR_ATTR_DEVICE_nGnRnE << (8 * MAIR_IDX_DEVICE_nGnRnE)) | \
	 (MAIR_ATTR_DEVICE_nGnRE  << (8 * MAIR_IDX_DEVICE_nGnRE))  | \
	 (MAIR_ATTR_NORMAL_WB     << (8 * MAIR_IDX_NORMAL))        | \
	 (MAIR_ATTR_NORMAL_NC     << (8 * MAIR_IDX_NORMAL_NC)))

/*
 * Attribute sets, one per protection class of the image.
 *
 * Nothing the kernel maps for itself is executable from EL0, so UXN is set
 * everywhere. Only .text is executable at EL1, and it is read-only: a kernel
 * that can write its own code has given away the guarantee the MMU was turned
 * on for.
 */
#define MMU_NORMAL	(PTE_ATTRINDX(MAIR_IDX_NORMAL) | PTE_SH_INNER | PTE_AF)

#define MMU_KERNEL_RX	(MMU_NORMAL | PTE_AP_RO_EL1 | PTE_UXN)
#define MMU_KERNEL_RO	(MMU_NORMAL | PTE_AP_RO_EL1 | PTE_UXN | PTE_PXN)
#define MMU_KERNEL_RW	(MMU_NORMAL | PTE_AP_RW_EL1 | PTE_UXN | PTE_PXN)

/*
 * Device registers. Shareability is ignored for device memory, so SH stays at
 * zero rather than pretending to describe something.
 */
#define MMU_DEVICE	(PTE_ATTRINDX(MAIR_IDX_DEVICE_nGnRnE) | PTE_SH_NONE | \
			 PTE_AF | PTE_AP_RW_EL1 | PTE_UXN | PTE_PXN)

/*
 * User mappings. All non-global, so their TLB entries carry an ASID and one
 * process's translations do not have to be flushed to run another.
 *
 * PXN is set on user text as firmly as UXN is on kernel text, and for the
 * same reason in reverse: EL1 must not execute a page EL0 can write to, which
 * is the whole shape of a large family of exploits.
 */
#define MMU_USER_RX	(MMU_NORMAL | PTE_AP_RO_ALL | PTE_PXN | PTE_NG)
#define MMU_USER_RW	(MMU_NORMAL | PTE_AP_RW_ALL | PTE_PXN | PTE_UXN | \
			 PTE_NG)

/* TCR_EL1 fields. */
#define TCR_T0SZ(x)		((uint64_t)(x) << 0)
#define TCR_EPD0		(1UL << 7)	/* no TTBR0 walks */
#define TCR_IRGN0_WBWA		(1UL << 8)
#define TCR_ORGN0_WBWA		(1UL << 10)
#define TCR_SH0_INNER		(3UL << 12)
#define TCR_TG0_4K		(0UL << 14)
#define TCR_T1SZ(x)		((uint64_t)(x) << 16)
#define TCR_EPD1		(1UL << 23)
#define TCR_IRGN1_WBWA		(1UL << 24)
#define TCR_ORGN1_WBWA		(1UL << 26)
#define TCR_SH1_INNER		(3UL << 28)
#define TCR_TG1_4K		(2UL << 30)	/* not the TG0 encoding */
#define TCR_IPS(x)		((uint64_t)(x) << 32)

/* SCTLR_EL1 fields we touch. */
#define SCTLR_M			(1UL << 0)	/* MMU */
#define SCTLR_C			(1UL << 2)	/* data cache */
#define SCTLR_I			(1UL << 12)	/* instruction cache */

/*
 * 48 bits of virtual address in each half. TnSZ counts the address bits that
 * are *not* translated.
 */
#define VA_BITS			48
#define TnSZ			(64 - VA_BITS)

/*
 * Page tables for boot: two roots, one per TTBR.
 *
 * The pool is sized by counting what the mappings below need - six tables for
 * the identity map, six for the kernel map - with headroom for the linear map
 * of RAM, which arrives once there is a device tree to say how much RAM there
 * is.
 */
#define PT_POOL_PAGES		24

static uint64_t pt_pool[PT_POOL_PAGES][PTRS_PER_TABLE]
    __attribute__((aligned(PAGE_SIZE)));
static unsigned pt_used;

/*
 * Roots are held as physical addresses because that is what a TTBR takes, and
 * because a page table is reached through a different address depending on
 * when you ask - see pt() below.
 */
static uint64_t identity_root;
static uint64_t kernel_root;

/*
 * Device register ranges the BSP has asked for, with the variable each driver
 * keeps its base address in. Small and fixed: a bring-up kernel knows every
 * device it has, and a driver that cannot register is better than one that
 * silently gets no mapping.
 */
#define MAX_DEVICE_MAPS		8

struct device_map {
	uint64_t base;
	uint64_t size;
	uint64_t *slot;
};

static struct device_map device_map[MAX_DEVICE_MAPS];
static unsigned device_maps;

static void
early_stop(const char *why)
{
	kputs("mmu: ");
	kputs(why);
	kputs("\n");

	for (;;)
		__asm__ volatile("wfi");
}

void
mmu_map_device(uint64_t base, uint64_t size, uint64_t *slot)
{
	if (device_maps >= MAX_DEVICE_MAPS)
		early_stop("too many device mappings");

	device_map[device_maps].base = base;
	device_map[device_maps].size = size;
	device_map[device_maps].slot = slot;
	device_maps++;

	/*
	 * Until the tables exist the physical address is the usable one, and
	 * drivers do talk to their hardware in that window - the console
	 * prints from it, and the interrupt controller is programmed there.
	 */
	*slot = base;
}

void
mmu_activate_device_maps(void)
{
	unsigned i;

	for (i = 0; i < device_maps; i++)
		*device_map[i].slot = phys_to_virt(device_map[i].base);
}

unsigned
mmu_device_map_count(void)
{
	return device_maps;
}

int
mmu_device_map_get(unsigned i, uint64_t *base, uint64_t *size)
{
	if (i >= device_maps)
		return 0;

	*base = device_map[i].base;
	*size = device_map[i].size;
	return 1;
}

/*
 * Is the kernel executing from the upper half yet?
 *
 * Asked of the program counter rather than of SCTLR_EL1.M, because the
 * question is not whether translation is on but which addresses this code is
 * using. Between enabling the MMU and branching high, translation is on and
 * the kernel is still running physically.
 */
static int
running_high(void)
{
	uint64_t pc;

	__asm__ volatile("adr %0, ." : "=r"(pc));
	return pc >= KERNEL_VA_OFFSET;
}

/*
 * An addressable pointer to the page table at physical address pa.
 *
 * Descriptors hold physical addresses; the code that writes them needs an
 * address it can dereference. Before the branch into the upper half that is
 * the physical address itself, through the identity map. Afterwards the
 * identity map is gone and the same page is reached through the kernel map.
 */
static uint64_t *
pt(uint64_t pa)
{
	return (uint64_t *)(running_high() ? phys_to_virt(pa) : pa);
}

/* The physical address of a kernel object, named however we can see it. */
static uint64_t
sym_phys(const void *p)
{
	uint64_t a = (uint64_t)p;

	return a >= KERNEL_VA_OFFSET ? virt_to_phys(a) : a;
}

/*
 * Take a page from the pool and return its physical address.
 *
 * The pool is a fixed array in BSS. A kernel with a memory allocator gets its
 * page tables from that instead; there is no allocator until VM exists, and
 * counting the pages this kernel actually needs is more honest than
 * pretending to have one.
 */
static uint64_t
alloc_table(void)
{
	uint64_t *table;
	unsigned i;

	if (pt_used >= PT_POOL_PAGES)
		early_stop("out of page tables");

	table = pt_pool[pt_used++];
	for (i = 0; i < PTRS_PER_TABLE; i++)
		table[i] = 0;

	return sym_phys(table);
}

/*
 * Map size bytes at va to pa with the given attributes, creating tables as
 * needed. Block descriptors are used wherever alignment and length allow,
 * which keeps the identity map down to a handful of entries and saves a level
 * of walking on every miss.
 */
static void
map_range(uint64_t root, uint64_t va, uint64_t pa, uint64_t size,
	uint64_t attrs)
{
	uint64_t end;

	end = (va + size + PAGE_MASK) & ~PAGE_MASK;
	va &= ~PAGE_MASK;
	pa &= ~PAGE_MASK;

	while (va < end) {
		uint64_t table_pa = root;
		unsigned level;

		for (level = 0; ; level++) {
			unsigned shift = LEVEL_SHIFT(level);
			uint64_t span = 1UL << shift;
			unsigned idx = (va >> shift) & (PTRS_PER_TABLE - 1);
			uint64_t *table = pt(table_pa);

			if (level == 3) {
				table[idx] = pa | attrs | PTE_PAGE | PTE_VALID;
				va += PAGE_SIZE;
				pa += PAGE_SIZE;
				break;
			}

			/*
			 * A block fits if it is aligned in both address spaces
			 * and the request still has that much left. Level 0
			 * blocks do not exist in this format.
			 */
			if (level > 0 && (va & (span - 1)) == 0 &&
			    (pa & (span - 1)) == 0 && end - va >= span) {
				table[idx] = pa | attrs | PTE_BLOCK | PTE_VALID;
				va += span;
				pa += span;
				break;
			}

			if ((table[idx] & PTE_VALID) == 0) {
				table[idx] = alloc_table() | PTE_TABLE |
				    PTE_VALID;
			} else if ((table[idx] & PTE_TABLE) == 0) {
				/*
				 * A block already covers this range. Splitting
				 * it is real work and nothing here needs it,
				 * so say so rather than corrupt the map.
				 */
				early_stop("would have to split a block");
			}

			table_pa = table[idx] & PTE_ADDR_MASK;
		}
	}
}

/*
 * Map the kernel image, one range per protection class. Called twice: with
 * offset 0 for the identity map, and with KERNEL_VA_OFFSET for the map the
 * kernel will live in.
 *
 * The section boundaries are page aligned by kernel.lds and contiguous, so
 * .data through the end of .bss is a single writable range.
 */
static void
map_kernel_image(uint64_t root, uint64_t offset)
{
	uint64_t text = (uint64_t)__text_start;
	uint64_t rodata = (uint64_t)__rodata_start;
	uint64_t data = (uint64_t)__data_start;
	uint64_t end = (uint64_t)__kernel_end;

	map_range(root, text + offset, text,
	    (uint64_t)__text_end - text, MMU_KERNEL_RX);
	map_range(root, rodata + offset, rodata,
	    (uint64_t)__rodata_end - rodata, MMU_KERNEL_RO);
	map_range(root, data + offset, data, end - data, MMU_KERNEL_RW);
}

/*
 * Clean and invalidate a range from the data caches.
 *
 * With SCTLR_EL1.C clear our writes went straight to memory, but the table
 * walker is about to read those same locations as cacheable, and anything the
 * bootloader left behind in the caches would shadow them. QEMU does not model
 * this; a real Cortex-A72 does, and the failure mode is a machine that dies
 * without a character of output.
 */
static void
dcache_clean_inval(uint64_t start, uint64_t size)
{
	uint64_t ctr, line, addr, end;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

	/* CTR_EL0.DminLine is log2 of the line size in words. */
	line = 4UL << ((ctr >> 16) & 0xf);

	end = start + size;
	for (addr = start & ~(line - 1); addr < end; addr += line)
		__asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");

	__asm__ volatile("dsb sy" ::: "memory");
}

/*
 * Physical address size this core supports, in the encoding TCR_EL1.IPS
 * wants. Asking the hardware beats hardcoding it: an IPS larger than the
 * implementation supports is a constrained-unpredictable value, and the
 * answer differs between the Cortex-A72 and whatever QEMU is told to be.
 *
 * 52-bit output needs a different descriptor format, so clamp at 48.
 */
static uint64_t
supported_ips(void)
{
	uint64_t mmfr0;

	__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
	mmfr0 &= 0xf;

	return mmfr0 > 5 ? 5 : mmfr0;
}

void
mmu_setup(void)
{
	uint64_t mair, tcr, sctlr;
	unsigned i;

	identity_root = alloc_table();
	kernel_root = alloc_table();

	/*
	 * The kernel map: the image at its link address, plus every device
	 * range the BSP registered.
	 *
	 * There is no linear map of RAM yet. Nothing needs one before there
	 * is a device tree to say where RAM is and how much of it there is,
	 * and mapping memory that may not exist is a good way to fault on
	 * something that never gets diagnosed.
	 */
	map_kernel_image(kernel_root, KERNEL_VA_OFFSET);

	/*
	 * The identity map exists for one instruction: the one after
	 * SCTLR_EL1.M is set, which is still fetched from a physical PC. It
	 * carries the devices too, so that window is debuggable rather than
	 * blind, and so a driver keeps working across it.
	 */
	map_kernel_image(identity_root, 0);

	for (i = 0; i < device_maps; i++) {
		uint64_t base = device_map[i].base;
		uint64_t size = device_map[i].size;

		map_range(kernel_root, phys_to_virt(base), base, size,
		    MMU_DEVICE);
		map_range(identity_root, base, base, size, MMU_DEVICE);
	}

	dcache_clean_inval((uint64_t)pt_pool, sizeof(pt_pool));

	mair = MAIR_VALUE;

	/*
	 * Both halves alike: 4 KiB granule, 48-bit addresses, inner
	 * shareable, write-back cacheable table walks. Walks have to be
	 * cacheable and shareable for the same reason the mappings are - on
	 * SMP the other cores' TLB maintenance has to reach them.
	 */
	tcr = TCR_T0SZ(TnSZ) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA |
	    TCR_SH0_INNER | TCR_TG0_4K |
	    TCR_T1SZ(TnSZ) | TCR_IRGN1_WBWA | TCR_ORGN1_WBWA |
	    TCR_SH1_INNER | TCR_TG1_4K |
	    TCR_IPS(supported_ips());

	__asm__ volatile(
		"msr	mair_el1, %0\n\t"
		"msr	tcr_el1, %1\n\t"
		"msr	ttbr0_el1, %2\n\t"
		"msr	ttbr1_el1, %3\n\t"
		"isb"
		:: "r"(mair), "r"(tcr),
		   "r"(identity_root), "r"(kernel_root)
		: "memory");

	/*
	 * Nothing has been translated on this CPU yet, but the TLB is not
	 * guaranteed to start empty and the bootloader ran with a map of its
	 * own.
	 */
	__asm__ volatile(
		"tlbi	vmalle1\n\t"
		"dsb	nsh\n\t"
		"isb"
		::: "memory");

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr |= SCTLR_M | SCTLR_C | SCTLR_I;

	/*
	 * The isb is what makes the next instruction fetch use the new
	 * configuration. Until it retires the CPU may still be running on the
	 * old one - which is the whole reason the identity map has to exist.
	 */
	__asm__ volatile(
		"msr	sctlr_el1, %0\n\t"
		"isb"
		:: "r"(sctlr) : "memory");
}

void
mmu_switch_high(void (*entry)(void))
{
	uint64_t target = (uint64_t)entry + KERNEL_VA_OFFSET;
	uint64_t stack = (uint64_t)boot_stack_top + KERNEL_VA_OFFSET;

	/*
	 * The stack moves to its upper-half alias - the same memory under a
	 * different name - but the frames below are abandoned. There is no
	 * return path across this branch, and there is not meant to be.
	 */
	__asm__ volatile(
		"mov	sp, %1\n\t"
		"br	%0"
		:: "r"(target), "r"(stack) : "memory");

	__builtin_unreachable();
}

void
mmu_drop_identity(void)
{
	uint64_t tcr;

	/*
	 * Clearing TTBR0_EL1 on its own would leave the walker pointed at
	 * physical address zero rather than switched off. EPD0 is what makes
	 * a low address fault, and that is the point: from here on a
	 * leftover physical pointer is a bug that announces itself.
	 */
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr |= TCR_EPD0;

	__asm__ volatile(
		"msr	ttbr0_el1, xzr\n\t"
		"msr	tcr_el1, %0\n\t"
		"isb\n\t"
		"tlbi	vmalle1\n\t"
		"dsb	nsh\n\t"
		"isb"
		:: "r"(tcr) : "memory");
}

uint64_t
mmu_kern_phys(const void *p)
{
	return sym_phys(p);
}

uint64_t
mmu_user_create(void)
{
	return alloc_table();
}

void
mmu_user_map_text(uint64_t root, uint64_t va, uint64_t pa, uint64_t size)
{
	map_range(root, va, pa, size, MMU_USER_RX);
}

void
mmu_user_map_data(uint64_t root, uint64_t va, uint64_t pa, uint64_t size)
{
	map_range(root, va, pa, size, MMU_USER_RW);
}

void
mmu_user_enter(uint64_t root, unsigned asid)
{
	uint64_t tcr;

	/*
	 * Undo what mmu_drop_identity() did: the lower half has a legitimate
	 * occupant again.
	 */
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr &= ~TCR_EPD0;

	/*
	 * The ASID rides in the top sixteen bits of TTBR0_EL1 - TCR_EL1.A1 is
	 * clear, so TTBR0 is what defines it.
	 *
	 * Everything is invalidated here rather than just this ASID. With one
	 * process that is the same thing and it is one instruction; a
	 * scheduler switching between address spaces wants "tlbi aside1is"
	 * for the outgoing ASID instead, and nothing at all when the incoming
	 * ASID is still valid, which is the point of having them.
	 */
	__asm__ volatile(
		"msr	ttbr0_el1, %0\n\t"
		"msr	tcr_el1, %1\n\t"
		"isb\n\t"
		"tlbi	vmalle1\n\t"
		"dsb	nsh\n\t"
		"isb"
		:: "r"(root | ((uint64_t)asid << 48)), "r"(tcr) : "memory");
}

void
mmu_sync_icache(uint64_t va, uint64_t size)
{
	uint64_t ctr, dline, iline, addr, end;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	dline = 4UL << ((ctr >> 16) & 0xf);	/* DminLine */
	iline = 4UL << (ctr & 0xf);		/* IminLine */

	end = va + size;

	/*
	 * Clean to the point of unification so instruction fetch can see the
	 * writes, then throw away whatever the instruction cache already had
	 * for those addresses.
	 *
	 * By virtual address, and the address used is the kernel's - the
	 * Cortex-A72's caches are physically tagged, so cleaning an alias
	 * covers every other mapping of the same page, including the one the
	 * process will run from.
	 */
	for (addr = va & ~(dline - 1); addr < end; addr += dline)
		__asm__ volatile("dc cvau, %0" :: "r"(addr) : "memory");
	__asm__ volatile("dsb ish" ::: "memory");

	for (addr = va & ~(iline - 1); addr < end; addr += iline)
		__asm__ volatile("ic ivau, %0" :: "r"(addr) : "memory");
	__asm__ volatile("dsb ish" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

int
mmu_probe(uint64_t va, unsigned access, uint64_t *pa)
{
	uint64_t par;

	/*
	 * The operation is part of the instruction encoding, so the three
	 * cases cannot be folded into one.
	 */
	if (access == MMU_ACCESS_EL1_WRITE)
		__asm__ volatile("at s1e1w, %0" :: "r"(va) : "memory");
	else if (access == MMU_ACCESS_EL0_READ)
		__asm__ volatile("at s1e0r, %0" :: "r"(va) : "memory");
	else
		__asm__ volatile("at s1e1r, %0" :: "r"(va) : "memory");

	__asm__ volatile("isb" ::: "memory");
	__asm__ volatile("mrs %0, par_el1" : "=r"(par));

	/* PAR_EL1.F: the translation faulted, for whatever reason. */
	if ((par & 1) != 0)
		return 0;

	*pa = (par & PTE_ADDR_MASK) | (va & PAGE_MASK);
	return 1;
}

void
mmu_report(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
	kput_line("SCTLR_EL1   : ", v);
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(v));
	kput_line("TCR_EL1     : ", v);
	__asm__ volatile("mrs %0, mair_el1" : "=r"(v));
	kput_line("MAIR_EL1    : ", v);
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
	kput_line("TTBR0_EL1   : ", v);
	__asm__ volatile("mrs %0, ttbr1_el1" : "=r"(v));
	kput_line("TTBR1_EL1   : ", v);
	kput_line("tables used : ", pt_used);
	kput_line("device maps : ", device_maps);
}
