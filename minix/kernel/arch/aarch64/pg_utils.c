/*
 * Physical memory and the translation tables the kernel builds for itself.
 *
 * Two jobs, in the order boot needs them. First the memory map: which
 * physical ranges are RAM, and which parts of them are already spoken for by
 * the kernel image, the device tree and the boot modules. Then the tables:
 * an identity map that keeps this code addressable while the MMU is switched
 * on, and the kernel map it lives in afterwards.
 *
 *
 * Which address a symbol has, and when
 * ------------------------------------
 * The kernel is linked at its upper-half virtual address and loaded at its
 * physical one, so until the move to the upper half this code runs
 * KERNEL_VA_OFFSET below where the linker put it. -mcmodel=small reaches
 * every symbol with PC-relative adrp/add, so taking the address of a kernel
 * symbol yields a physical address before the switch and a virtual one after
 * it. Both are right: it is the same object, named in whichever address
 * space the code is running in. sym_phys() and pt() below are where that
 * difference is handled, and running_high() is how they tell which world
 * they are in.
 *
 * What must not appear in the pre-switch path is an address the linker
 * resolved to a constant - an initialised pointer in .data, or an absolute
 * symbol from the link script such as _kern_vir_base, whose adrp is only
 * correct at the link-time PC. Everything here uses the section-relative
 * symbols instead, which move with the image.
 *
 *
 * How this differs from the two 32-bit ports
 * ------------------------------------------
 * earm and i386 map all of physical memory 1:1 and keep that map forever, so
 * pg_identity() there means "the whole address space". Here the identity map
 * covers only the kernel image and the device registers, exists for the few
 * instructions between setting SCTLR_EL1.M and branching high, and is then
 * taken away - because the kernel's own view of physical memory is the linear
 * map in TTBR1, at a fixed offset, which is a better answer to the same
 * question and leaves the whole lower half to processes.
 *
 * The linear map is also why there is no equivalent of freepdes here: the
 * kernel can already reach any physical page by adding a constant, so it
 * never has to borrow a page directory entry to look at one.
 */

#include "kernel/kernel.h"

#include <assert.h>
#include <string.h>

#include <machine/vm.h>
#include <machine/memory.h>

#include "archconst.h"
#include "arch_proto.h"

/*
 * Section boundaries from kernel.lds. Section-relative, so they follow the
 * image: physical before the switch, virtual after it.
 */
extern char __kernel_start[], __kernel_end[];
extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];

/* Top of the boot stack, from head.S. */
extern char boot_stack_top[];

/*
 * Attribute sets, one per protection class.
 *
 * Nothing the kernel maps for itself is executable from EL0, so UXN is set
 * everywhere. Only .text is executable at EL1, and it is read-only: a kernel
 * that can write its own code has given away the guarantee the MMU was
 * turned on for. The linear map of RAM is neither, which is what stops a
 * page a process can write from also being a page the kernel can jump to.
 */
#define PG_NORMAL	(AARCH64_VM_PTE_CACHED | AARCH64_VM_AF)

#define PG_KERNEL_RX	(PG_NORMAL | AARCH64_VM_AP_RO_EL1 | AARCH64_VM_UXN)
#define PG_KERNEL_RO	(PG_NORMAL | AARCH64_VM_AP_RO_EL1 | AARCH64_VM_UXN | \
			 AARCH64_VM_PXN)
#define PG_KERNEL_RW	(PG_NORMAL | AARCH64_VM_AP_RW_EL1 | AARCH64_VM_UXN | \
			 AARCH64_VM_PXN)

/*
 * Device registers. Shareability is ignored for device memory, so SH stays
 * at zero rather than pretending to describe something.
 */
#define PG_DEVICE	(AARCH64_VM_PTE_DEVICE | AARCH64_VM_AF | \
			 AARCH64_VM_AP_RW_EL1 | AARCH64_VM_UXN | \
			 AARCH64_VM_PXN)

/*
 * User mappings for the boot process. Non-global, so their TLB entries carry
 * an ASID; PXN because EL1 must not execute a page EL0 can write, which is
 * the shape of a large family of exploits.
 */
#define PG_USER_RW	(PG_NORMAL | AARCH64_VM_AP_RW_ALL | AARCH64_VM_PXN | \
			 AARCH64_VM_UXN | AARCH64_VM_NG)

/* MAIR_EL1, laid out to match the indices in <machine/vm.h>. */
#define MAIR_ATTR_DEVICE_nGnRnE	0x00UL
#define MAIR_ATTR_DEVICE_nGnRE	0x04UL
#define MAIR_ATTR_NORMAL_WB	0xffUL	/* inner+outer write-back, RW-alloc */
#define MAIR_ATTR_NORMAL_NC	0x44UL	/* inner+outer non-cacheable */

#define MAIR_VALUE \
	((MAIR_ATTR_DEVICE_nGnRnE << (8 * AARCH64_MAIR_DEVICE_nGnRnE)) | \
	 (MAIR_ATTR_DEVICE_nGnRE  << (8 * AARCH64_MAIR_DEVICE_nGnRE))  | \
	 (MAIR_ATTR_NORMAL_WB     << (8 * AARCH64_MAIR_NORMAL))        | \
	 (MAIR_ATTR_NORMAL_NC     << (8 * AARCH64_MAIR_NORMAL_NC)))

/* TCR_EL1 fields. */
#define TCR_T0SZ(x)		((u64_t)(x) << 0)
#define TCR_EPD0		(1UL << 7)	/* no TTBR0 walks */
#define TCR_IRGN0_WBWA		(1UL << 8)
#define TCR_ORGN0_WBWA		(1UL << 10)
#define TCR_SH0_INNER		(3UL << 12)
#define TCR_TG0_4K		(0UL << 14)
#define TCR_T1SZ(x)		((u64_t)(x) << 16)
#define TCR_IRGN1_WBWA		(1UL << 24)
#define TCR_ORGN1_WBWA		(1UL << 26)
#define TCR_SH1_INNER		(3UL << 28)
#define TCR_TG1_4K		(2UL << 30)	/* not the TG0 encoding */
#define TCR_IPS(x)		((u64_t)(x) << 32)

/* SCTLR_EL1 fields this file touches. */
#define SCTLR_M			(1UL << 0)	/* MMU */
#define SCTLR_C			(1UL << 2)	/* data cache */
#define SCTLR_I			(1UL << 12)	/* instruction cache */

/* TnSZ counts the address bits that are *not* translated. */
#define TnSZ			(64 - AARCH64_VA_BITS)

/*
 * Roots of the three translation trees, as physical addresses because that
 * is what a TTBR takes.
 *
 * identity_root exists only for the instructions between enabling the MMU and
 * branching into the upper half. kernel_root is TTBR1 and never changes.
 * boot_user_root is the TTBR0 tree the boot process - VM - is loaded into,
 * which prot_init() creates and arch_post_init() hands over.
 */
static phys_bytes identity_root;
static phys_bytes kernel_root;
static phys_bytes boot_user_root;

/*
 * The extent of the pages handed out as translation tables, so they can be
 * flushed from the data caches in one go before the MMU is switched on. They
 * come one after another out of the same memory chunk, so this is normally
 * exactly the tables; when a chunk runs out it is a superset, and cleaning a
 * few extra pages of RAM costs nothing and is harmless.
 */
static phys_bytes tables_lo, tables_hi;

/*
 * The RAM ranges as the device tree describes them - all of RAM, not just
 * the part that is still free. The free list in kinfo has the kernel image
 * and the boot modules cut out of it; the linear map has to cover them too,
 * so it is built from here.
 */
#define MAX_RAM_RANGES	8

static struct {
	phys_bytes base;
	phys_bytes size;
} ram[MAX_RAM_RANGES];

static unsigned nram;

/*===========================================================================*
 *				running_high				     *
 *===========================================================================*/
static int
running_high(void)
{
	vir_bytes pc;

	/*
	 * Asked of the program counter rather than of SCTLR_EL1.M, because
	 * the question is not whether translation is on but which addresses
	 * this code is using. Between enabling the MMU and branching high,
	 * translation is on and the kernel still runs physically.
	 */
	__asm__ volatile("adr %0, ." : "=r"(pc));
	return pc >= KERNEL_VA_OFFSET;
}

/*===========================================================================*
 *				sym_phys				     *
 *===========================================================================*/
/* The physical address of a kernel object, named however we can see it. */
static phys_bytes
sym_phys(const void *p)
{
	vir_bytes a = (vir_bytes)p;

	return a >= KERNEL_VA_OFFSET ? (phys_bytes)(a - KERNEL_VA_OFFSET) :
	    (phys_bytes)a;
}

/*===========================================================================*
 *				pt					     *
 *===========================================================================*/
/*
 * An addressable pointer to the translation table at physical address pa.
 *
 * Descriptors hold physical addresses; the code that writes them needs an
 * address it can dereference. With the MMU off that is the physical address
 * itself. Once the kernel runs high it is the linear map, which is what
 * makes editing a page table after boot an ordinary memory write.
 */
static u64_t *
pt(phys_bytes pa)
{
	return (u64_t *)(running_high() ? phys2vir(pa) : (vir_bytes)pa);
}

/*===========================================================================*
 *				pg_roundup				     *
 *===========================================================================*/
phys_bytes
pg_roundup(phys_bytes b)
{
	return (b + AARCH64_PAGE_MASK) & ~(phys_bytes)AARCH64_PAGE_MASK;
}

/*===========================================================================*
 *				pg_rounddown				     *
 *===========================================================================*/
phys_bytes
pg_rounddown(phys_bytes b)
{
	return b & ~(phys_bytes)AARCH64_PAGE_MASK;
}

/*===========================================================================*
 *				pg_add_ram				     *
 *===========================================================================*/
void
pg_add_ram(phys_bytes base, phys_bytes size)
{
	if (size == 0)
		return;

	if (nram >= MAX_RAM_RANGES)
		panic("more RAM ranges than this kernel can map");

	ram[nram].base = pg_roundup(base);
	ram[nram].size = pg_rounddown(base + size) - ram[nram].base;
	nram++;
}

/*===========================================================================*
 *				pg_is_ram				     *
 *===========================================================================*/
/*
 * Whether a physical address is one the linear map covers.
 *
 * phys2vir() is arithmetic and will happily produce an address for anything,
 * but only RAM is mapped in the upper half: device registers are mapped
 * where kern_phys_map asked for them and everything else is not mapped at
 * all. So the memory code has to ask before it dereferences a physical
 * address that came out of a page table, or out of a process. Getting it
 * wrong would be a data abort inside the kernel with no handler - a panic
 * where the honest answer is EFAULT.
 *
 * The list is one or two entries long on every machine we have seen, so this
 * is a couple of comparisons and not worth an index.
 */
int
pg_is_ram(phys_bytes pa)
{
	unsigned i;

	for (i = 0; i < nram; i++)
		if (pa >= ram[i].base && pa - ram[i].base < ram[i].size)
			return 1;

	return 0;
}

/*===========================================================================*
 *				add_memmap				     *
 *===========================================================================*/
void
add_memmap(kinfo_t *cbi, u64_t addr, u64_t len)
{
	int m;

	/*
	 * The ceiling is VM's, not the kernel's: phys_bytes is 64 bits here
	 * and the memory map has always carried 64-bit values, but VM hands
	 * out physical pages through the two-level, 32-bit page tables of
	 * servers/vm/pt.c. Showing it memory it cannot address would not
	 * fail, it would quietly wrap. See <machine/memory.h>, where the
	 * limit is named, and port/PORTING-LOG.md, stage 4 group 1.
	 */
	if (addr >= VM_MAX_PHYS_MEM)
		return;
	if (addr + len > VM_MAX_PHYS_MEM)
		len = VM_MAX_PHYS_MEM - addr;

	assert(cbi->mmap_size < MAXMEMMAP);

	addr = pg_roundup(addr);
	len = pg_rounddown(len);
	if (len == 0)
		return;

	assert(kernel_may_alloc);

	for (m = 0; m < MAXMEMMAP; m++) {
		phys_bytes highmark;

		if (cbi->memmap[m].mm_length)
			continue;

		cbi->memmap[m].mm_base_addr = addr;
		cbi->memmap[m].mm_length = len;
		cbi->memmap[m].type = MULTIBOOT_MEMORY_AVAILABLE;
		if (m >= cbi->mmap_size)
			cbi->mmap_size = m + 1;

		highmark = addr + len;
		if (highmark > cbi->mem_high_phys)
			cbi->mem_high_phys = highmark;

		return;
	}

	panic("no available memmap slot");
}

/*===========================================================================*
 *				cut_memmap				     *
 *===========================================================================*/
void
cut_memmap(kinfo_t *cbi, phys_bytes start, phys_bytes end)
{
	int m;

	start = pg_rounddown(start);
	end = pg_roundup(end);

	assert(kernel_may_alloc);

	for (m = 0; m < cbi->mmap_size; m++) {
		phys_bytes substart = start, subend = end;
		phys_bytes memaddr = cbi->memmap[m].mm_base_addr;
		phys_bytes memend = memaddr + cbi->memmap[m].mm_length;

		/* Narrow the cut to the part of this chunk it touches. */
		if (substart < memaddr) substart = memaddr;
		if (subend > memend) subend = memend;
		if (substart >= subend) continue;

		/* Drop the chunk and put back what is left of it. */
		cbi->memmap[m].mm_base_addr = cbi->memmap[m].mm_length = 0;
		if (substart > memaddr)
			add_memmap(cbi, memaddr, substart - memaddr);
		if (subend < memend)
			add_memmap(cbi, subend, memend - subend);
	}
}

/*===========================================================================*
 *				print_memmap				     *
 *===========================================================================*/
void
print_memmap(kinfo_t *cbi)
{
	int m;

	assert(cbi->mmap_size < MAXMEMMAP);
	for (m = 0; m < cbi->mmap_size; m++) {
		if (!cbi->memmap[m].mm_length)
			continue;
		printf("%016lx-%016lx ", (phys_bytes)cbi->memmap[m].mm_base_addr,
		    (phys_bytes)(cbi->memmap[m].mm_base_addr +
		    cbi->memmap[m].mm_length));
	}
	printf("\nfree chunks %d, memory ends at %016lx\n", cbi->mmap_size,
	    cbi->mem_high_phys);
}

/*===========================================================================*
 *				pg_alloc_page				     *
 *===========================================================================*/
phys_bytes
pg_alloc_page(kinfo_t *cbi)
{
	int m;

	assert(kernel_may_alloc);

	for (m = 0; m < cbi->mmap_size; m++) {
		multiboot_memory_map_t *mmap = &cbi->memmap[m];
		phys_bytes addr;

		if (!mmap->mm_length)
			continue;

		assert(!(mmap->mm_length % AARCH64_PAGE_SIZE));
		assert(!(mmap->mm_base_addr % AARCH64_PAGE_SIZE));

		addr = mmap->mm_base_addr;
		mmap->mm_base_addr += AARCH64_PAGE_SIZE;
		mmap->mm_length -= AARCH64_PAGE_SIZE;

		cbi->kernel_allocated_bytes_dynamic += AARCH64_PAGE_SIZE;

		return addr;
	}

	panic("can't find free memory");
}

/*===========================================================================*
 *				alloc_table				     *
 *===========================================================================*/
static phys_bytes
alloc_table(kinfo_t *cbi)
{
	phys_bytes pa = pg_alloc_page(cbi);

	memset(pt(pa), 0, AARCH64_PAGE_SIZE);

	if (tables_hi == 0 || pa < tables_lo)
		tables_lo = pa;
	if (pa + AARCH64_PAGE_SIZE > tables_hi)
		tables_hi = pa + AARCH64_PAGE_SIZE;

	return pa;
}

/*===========================================================================*
 *				map_range				     *
 *===========================================================================*/
/*
 * Map size bytes at va to pa with the given attributes, creating tables as
 * needed. Block descriptors are used wherever alignment and length allow,
 * which keeps the linear map of RAM down to a handful of entries and saves a
 * level of walking on every miss.
 */
static void
map_range(kinfo_t *cbi, phys_bytes root, vir_bytes va, phys_bytes pa,
	u64_t size, u64_t attrs)
{
	vir_bytes end = pg_roundup(va + size);

	va = pg_rounddown(va);
	pa = pg_rounddown(pa);

	while (va < end) {
		phys_bytes table_pa = root;
		int level;

		for (level = 0; ; level++) {
			unsigned shift = AARCH64_VM_LEVEL_SHIFT(level);
			u64_t span = 1UL << shift;
			unsigned idx = AARCH64_VM_INDEX(va, level);
			u64_t *table = pt(table_pa);

			if (level == 3) {
				table[idx] = pa | attrs | AARCH64_VM_PAGE |
				    AARCH64_VM_VALID;
				va += AARCH64_PAGE_SIZE;
				pa += AARCH64_PAGE_SIZE;
				break;
			}

			/*
			 * A block fits if it is aligned in both address spaces
			 * and the request still has that much left. Level 0
			 * blocks do not exist in this format.
			 */
			if (level > 0 && (va & (span - 1)) == 0 &&
			    (pa & (span - 1)) == 0 && end - va >= span) {
				/*
				 * Never lay a block over something already
				 * mapped: that would orphan a whole subtree
				 * and silently coarsen its permissions. The
				 * callers are written not to overlap, and
				 * this is how they find out when they do.
				 */
				if (table[idx] & AARCH64_VM_VALID)
					panic("pg: block over a live mapping "
					    "at %lx", va);

				table[idx] = pa | attrs | AARCH64_VM_BLOCK |
				    AARCH64_VM_VALID;
				va += span;
				pa += span;
				break;
			}

			if (!(table[idx] & AARCH64_VM_VALID)) {
				table[idx] = alloc_table(cbi) |
				    AARCH64_VM_TABLE | AARCH64_VM_VALID;
			} else if (!(table[idx] & AARCH64_VM_TABLE)) {
				/*
				 * A block already covers this range. Splitting
				 * it is real work and nothing here needs it,
				 * so say so rather than corrupt the map.
				 */
				panic("pg: would have to split a block at %lx",
				    va);
			}

			table_pa = AARCH64_VM_PFA(table[idx]);
		}
	}
}

/*===========================================================================*
 *				map_kernel_image			     *
 *===========================================================================*/
/*
 * Map the kernel image, one range per protection class. Called twice: with
 * offset 0 for the identity map, and with KERNEL_VA_OFFSET for the map the
 * kernel lives in.
 *
 * The writable range starts at __rodata_end rather than __data_start,
 * because between them sit the usermapped sections. Those are ordinary
 * kernel data as far as the kernel's own map is concerned - what makes them
 * special is that VM maps the same pages into processes as well, at an
 * address of its own choosing, which is arch_phys_map()'s business and not
 * this file's.
 */
static void
map_kernel_image(kinfo_t *cbi, phys_bytes root, vir_bytes offset)
{
	phys_bytes text = sym_phys(__text_start);
	phys_bytes rodata = sym_phys(__rodata_start);
	phys_bytes data = sym_phys(__rodata_end);
	phys_bytes end = sym_phys(__kernel_end);

	map_range(cbi, root, text + offset, text,
	    sym_phys(__text_end) - text, PG_KERNEL_RX);
	map_range(cbi, root, rodata + offset, rodata,
	    data - rodata, PG_KERNEL_RO);
	map_range(cbi, root, data + offset, data, end - data, PG_KERNEL_RW);
}

/*===========================================================================*
 *				map_devices				     *
 *===========================================================================*/
/*
 * Every register range a driver registered before paging existed, mapped at
 * its linear-map address.
 *
 * Device registers live outside RAM, so their linear-map aliases fall in the
 * holes the linear map of RAM leaves and cannot collide with it. They go
 * into the identity map too, so the console keeps working through the switch
 * - which is the difference between debugging that window and being blind
 * in it.
 */
/*
 * One entry of the device list, addressable from here.
 *
 * The list is built in upper-half addresses - see kern_req_phys_map() in
 * memory.c - because it is walked after the switch by code that has no other
 * way to reach it. This is the one walk that happens before the switch, so it
 * is the one place that has to fold those addresses back down. sym_phys() is
 * idempotent, so this is right in both worlds and needs no flag.
 */
static kern_phys_map *
kpm(kern_phys_map *m)
{
	if (m == NULL)
		return NULL;

	return running_high() ? m : (kern_phys_map *)sym_phys(m);
}

static void
map_devices(kinfo_t *cbi, phys_bytes root, vir_bytes offset)
{
	kern_phys_map *m;

	for (m = kpm(kern_phys_map_list()); m != NULL; m = kpm(m->next))
		map_range(cbi, root, (vir_bytes)m->addr + offset, m->addr,
		    m->size, PG_DEVICE);
}

/*===========================================================================*
 *				pg_identity				     *
 *===========================================================================*/
void
pg_identity(kinfo_t *cbi)
{
	/*
	 * The identity map exists for one instruction: the one after
	 * SCTLR_EL1.M is set, which is still fetched from a physical PC. It
	 * is dropped as soon as the kernel is running high, so that a
	 * leftover physical pointer faults instead of quietly working.
	 */
	identity_root = alloc_table(cbi);

	map_kernel_image(cbi, identity_root, 0);
	map_devices(cbi, identity_root, 0);
}

/*===========================================================================*
 *				pg_mapkernel				     *
 *===========================================================================*/
void
pg_mapkernel(kinfo_t *cbi)
{
	phys_bytes kern_start = sym_phys(__kernel_start);
	phys_bytes kern_end = sym_phys(__kernel_end);
	unsigned i;

	kernel_root = alloc_table(cbi);

	/*
	 * All of RAM at a fixed offset, with a hole where the kernel image
	 * is: the image is mapped just below, by protection class, and the
	 * two must not overlap. Because the image's virtual address is its
	 * physical address plus the same offset, the hole and the image line
	 * up exactly.
	 */
	for (i = 0; i < nram; i++) {
		phys_bytes base = ram[i].base;
		phys_bytes end = base + ram[i].size;
		phys_bytes lo, hi;

		/*
		 * [base, end) minus [kern_start, kern_end), written as the
		 * two pieces that survive. Clamping rather than testing for
		 * containment, because the kernel is loaded at the base of
		 * RAM on QEMU's virt machine: a test for "starts after the
		 * base" would leave the image inside the linear map on
		 * exactly the board this is developed on.
		 */
		lo = base;
		hi = kern_start < end ? kern_start : end;
		if (lo < hi)
			map_range(cbi, kernel_root, phys2vir(lo), lo, hi - lo,
			    PG_KERNEL_RW);

		lo = kern_end > base ? kern_end : base;
		hi = end;
		if (lo < hi)
			map_range(cbi, kernel_root, phys2vir(lo), lo, hi - lo,
			    PG_KERNEL_RW);
	}

	map_kernel_image(cbi, kernel_root, KERNEL_VA_OFFSET);
	map_devices(cbi, kernel_root, KERNEL_VA_OFFSET);
}

/*===========================================================================*
 *				pg_clear				     *
 *===========================================================================*/
void
pg_clear(void)
{
	/*
	 * Start a fresh page table for the boot process. Called from
	 * prot_init(), which runs after kmain() has copied the boot
	 * information into the global kinfo, so that is the one to allocate
	 * from.
	 *
	 * Only the lower half is built here. The two 32-bit ports rebuild the
	 * kernel's own mapping at this point as well, because theirs was made
	 * in memory that is about to be freed; the kernel map here is in
	 * ordinary kernel memory and in TTBR1, which nothing switches.
	 */
	if (boot_user_root == 0)
		boot_user_root = alloc_table(&kinfo);
	else
		memset(pt(boot_user_root), 0, AARCH64_PAGE_SIZE);
}

/*===========================================================================*
 *				pg_load					     *
 *===========================================================================*/
phys_bytes
pg_load(void)
{
	u64_t tcr;

	assert(boot_user_root != 0);

	/*
	 * Turn TTBR0 walks back on: pg_drop_identity() switched them off at
	 * the end of boot, and the lower half has a legitimate occupant
	 * again. ASID 0 is the boot process's; handing out the rest is the
	 * scheduler's business, once there is one.
	 */
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr &= ~TCR_EPD0;
	__asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");

	write_ttbr0(boot_user_root | AARCH64_TTBR_ASID(0));
	refresh_tlb();

	return boot_user_root;
}

/*===========================================================================*
 *				pg_map					     *
 *===========================================================================*/
void
pg_map(phys_bytes phys, vir_bytes vaddr, vir_bytes vaddr_end, kinfo_t *cbi)
{
	assert(boot_user_root != 0);
	assert(vaddr < KERNEL_VA_OFFSET);
	assert(vaddr_end <= KERNEL_VA_OFFSET);

	if (phys == PG_ALLOCATEME) {
		assert(!(vaddr % AARCH64_PAGE_SIZE));
	} else {
		assert((vaddr % AARCH64_PAGE_SIZE) ==
		    (phys % AARCH64_PAGE_SIZE));
		vaddr = pg_rounddown(vaddr);
		phys = pg_rounddown(phys);
	}

	while (vaddr < vaddr_end) {
		phys_bytes source = phys;

		if (phys == PG_ALLOCATEME)
			source = pg_alloc_page(cbi);
		else
			phys += AARCH64_PAGE_SIZE;

		assert(!(source % AARCH64_PAGE_SIZE));
		map_range(cbi, boot_user_root, vaddr, source,
		    AARCH64_PAGE_SIZE, PG_USER_RW);
		vaddr += AARCH64_PAGE_SIZE;
	}

	refresh_tlb();
}

/*===========================================================================*
 *				pg_info					     *
 *===========================================================================*/
void
pg_info(reg_t *ttbr_ph, u64_t **ttbr_v)
{
	assert(boot_user_root != 0);

	*ttbr_ph = (reg_t)boot_user_root;
	*ttbr_v = (u64_t *)phys2vir(boot_user_root);
}

/*===========================================================================*
 *				dcache_clean_inval			     *
 *===========================================================================*/
/*
 * Clean and invalidate a physical range from the data caches.
 *
 * With SCTLR_EL1.C clear the writes above went straight to memory, but the
 * table walker is about to read those same locations as cacheable, and
 * anything the boot loader left behind in the caches would shadow them. QEMU
 * does not model this; a real Cortex-A72 does, and the failure mode is a
 * machine that dies without a character of output.
 */
static void
dcache_clean_inval(phys_bytes start, phys_bytes size)
{
	u64_t ctr, line, addr, end;

	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));

	/* CTR_EL0.DminLine is log2 of the line size in words. */
	line = 4UL << ((ctr >> 16) & 0xf);

	end = start + size;
	for (addr = start & ~(line - 1); addr < end; addr += line)
		__asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");

	dsb();
}

/*===========================================================================*
 *				supported_ips				     *
 *===========================================================================*/
/*
 * Physical address size this core supports, in the encoding TCR_EL1.IPS
 * wants. Asking the hardware beats hardcoding it: an IPS larger than the
 * implementation supports is a constrained-unpredictable value, and the
 * answer differs between a Cortex-A72 and whatever QEMU is told to be.
 * 52-bit output needs a different descriptor format, so clamp at 48.
 */
static u64_t
supported_ips(void)
{
	u64_t mmfr0;

	__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
	mmfr0 &= 0xf;

	return mmfr0 > 5 ? 5 : mmfr0;
}

/*===========================================================================*
 *				vm_enable_paging			     *
 *===========================================================================*/
void
vm_enable_paging(void)
{
	u64_t tcr, sctlr;

	assert(identity_root != 0);
	assert(kernel_root != 0);

	/* Publish the tables to a walker that will read them as cacheable. */
	dcache_clean_inval(tables_lo, tables_hi - tables_lo);

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
		:: "r"((u64_t)MAIR_VALUE), "r"(tcr),
		   "r"((u64_t)identity_root), "r"((u64_t)kernel_root)
		: "memory");

	/*
	 * Nothing has been translated on this CPU yet, but the TLB is not
	 * guaranteed to start empty and the boot loader ran with a map of its
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

/*===========================================================================*
 *				pg_enter_high				     *
 *===========================================================================*/
void
pg_enter_high(void (*entry)(void))
{
	vir_bytes target = (vir_bytes)entry + KERNEL_VA_OFFSET;
	vir_bytes stack = (vir_bytes)boot_stack_top + KERNEL_VA_OFFSET;

	/*
	 * The stack moves to its upper-half alias - the same memory under a
	 * different name - but the frames below are abandoned. There is no
	 * return path across this branch, and there is not meant to be: the
	 * return address in lr is physical, and the identity map that would
	 * make it work is about to be taken away.
	 */
	__asm__ volatile(
		"mov	sp, %1\n\t"
		"br	%0"
		:: "r"(target), "r"(stack) : "memory");

	__builtin_unreachable();
}

/*===========================================================================*
 *				pg_drop_identity			     *
 *===========================================================================*/
void
pg_drop_identity(void)
{
	u64_t tcr;

	assert(running_high());

	/*
	 * Clearing TTBR0_EL1 on its own would leave the walker pointed at
	 * physical address zero rather than switched off. EPD0 is what makes
	 * a low address fault, and that is the point: from here on a leftover
	 * physical pointer is a bug that announces itself.
	 */
	__asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
	tcr |= TCR_EPD0;

	__asm__ volatile(
		"msr	ttbr0_el1, xzr\n\t"
		"msr	tcr_el1, %0\n\t"
		"isb"
		:: "r"(tcr) : "memory");

	refresh_tlb();

	identity_root = 0;
}
