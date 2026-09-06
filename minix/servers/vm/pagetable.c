
#define _SYSTEM 1

#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ds.h>
#include <minix/endpoint.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/safecopies.h>
#include <minix/cpufeature.h>
#include <minix/bitmap.h>
#include <minix/debug.h>

#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#include "proto.h"
#include "glo.h"
#include "util.h"
#include "vm.h"
#include "sanitycheck.h"

static int vm_self_pages;

#if ARCH_VM_KERNEL_IN_PROC_PT
/* PDE used to map in kernel, kernel physical address.
 *
 * The kernel reads every process's page directory through these: VM keeps a
 * page table of page-directory pointers and tells the kernel where it is.
 * Where the kernel has an address space of its own, it reaches any table by
 * its physical address instead and none of this exists.
 */
#define MAX_PAGEDIR_PDES 5
static struct pdm {
	int		pdeno;
	pt_entry_t	val;
	phys_bytes	phys;
	pt_entry_t	*page_directories;
} pagedir_mappings[MAX_PAGEDIR_PDES];

static multiboot_module_t *kern_mb_mod = NULL;
static size_t kern_size = 0;
#endif /* ARCH_VM_KERNEL_IN_PROC_PT */

/* First directory entry that is not the process's, set by pt_init().
 *
 * pt_copy() and pt_ptmap() walk the process's own mappings and must stop
 * here: past it are entries VM makes for every address space alike, and a
 * copy would find them already made in the destination.
 *
 * Where the kernel shares the page table that is where the kernel image
 * starts. Where it does not, it is where VM puts the kernel's user-visible
 * mappings - which is the same thing seen from this file, and the whole of
 * what is above the process.
 */
static int kern_start_pde = ARCH_VM_DIR_ENTRIES;

#if ARCH_VM_KERNEL_IN_PROC_PT
/* big page size available in hardware? */
static int bigpage_ok = 1;
#endif

/* Our process table entry. */
struct vmproc *vmprocess = &vmproc[VM_PROC_NR];

/* Spare memory, ready to go after initialization, to avoid a
 * circular dependency on allocating memory and writing it into VM's
 * page table.
 */
#if SANITYCHECKS
#define SPAREPAGES 200
#define STATIC_SPAREPAGES 190
#else
#ifdef __arm__
# define SPAREPAGES 150
# define STATIC_SPAREPAGES 140
#elif defined(__aarch64__)
/*
 * Between the two. ARM needs so many because one of its page tables spans a
 * megabyte, so an address space is made of hundreds of them; a level 3 table
 * here spans two megabytes, which is nearer i386's four. These have to carry
 * pt_init() through building VM's own address space before any allocator
 * works, and running out is a panic, so there is margin.
 */
# define SPAREPAGES 50
# define STATIC_SPAREPAGES 45
#else
# define SPAREPAGES 20
# define STATIC_SPAREPAGES 15
#endif /* __arm__ */
#endif

#ifdef __i386__
static u32_t global_bit = 0;
#endif

#define SPAREPAGEDIRS 1
#define STATIC_SPAREPAGEDIRS 1

int missing_sparedirs = SPAREPAGEDIRS;
static struct {
	void *pagedir;
	phys_bytes phys;
} sparepagedirs[SPAREPAGEDIRS];

#define is_staticaddr(v) ((vir_bytes) (v) < VM_OWN_HEAPSTART)

#define MAX_KERNMAPPINGS 10
static struct {
	phys_bytes	phys_addr;	/* Physical addr. */
	phys_bytes	len;		/* Length in bytes. */
	vir_bytes	vir_addr;	/* Offset in page table. */
	pt_entry_t	flags;
} kern_mappings[MAX_KERNMAPPINGS];
int kernmappings = 0;

/* Clicks must be pages, as
 *  - they must be page aligned to map them
 *  - they must be a multiple of the page size
 *  - it's inconvenient to have them bigger than pages, because we often want
 *    just one page
 * May as well require them to be equal then.
 */
#if CLICK_SIZE != VM_PAGE_SIZE
#error CLICK_SIZE must be page size.
#endif

static void *spare_pagequeue;
static char static_sparepages[VM_PAGE_SIZE*STATIC_SPAREPAGES] 
	__aligned(VM_PAGE_SIZE);

/*
 * A pool of spare page directories, for the same reason as the spare pages
 * above: pt_new() runs before anything can allocate. It is needed wherever a
 * directory is more than one page, which is ARM's 16 KiB one and AArch64's
 * six-page block, and nowhere else.
 */
#define HAVE_SPAREPAGEDIRS (ARCH_PAGEDIR_SIZE > VM_PAGE_SIZE)

#if HAVE_SPAREPAGEDIRS
static char static_sparepagedirs[ARCH_PAGEDIR_SIZE*STATIC_SPAREPAGEDIRS + ARCH_PAGEDIR_SIZE] __aligned(ARCH_PAGEDIR_ALIGN);
#endif

void pt_assert(pt_t *pt)
{
	char dir[VM_PAGE_SIZE];
	pt_clearmapcache();
	if((sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed");
	}
	sys_physcopy(NONE, pt->pt_root_phys, SELF, (vir_bytes) dir, sizeof(dir), 0);
	assert(!memcmp(dir, pt->pt_root, sizeof(dir)));
}

#if SANITYCHECKS
/*===========================================================================*
 *				pt_sanitycheck		     		     *
 *===========================================================================*/
void pt_sanitycheck(pt_t *pt, const char *file, int line)
{
/* Basic pt sanity check. */
	int slot;

	MYASSERT(pt);
	MYASSERT(pt->pt_dir);
	MYASSERT(pt->pt_root_phys);

	for(slot = 0; slot < ELEMENTS(vmproc); slot++) {
		if(pt == &vmproc[slot].vm_pt)
			break;
	}

	if(slot >= ELEMENTS(vmproc)) {
		panic("pt_sanitycheck: passed pt not in any proc");
	}

	MYASSERT(usedpages_add(pt->pt_root_phys, VM_PAGE_SIZE) == OK);
}
#endif

/*===========================================================================*
 *				findhole		     		     *
 *===========================================================================*/
static vir_bytes findhole(int pages)
{
/* Find a space in the virtual address space of VM. */
	vir_bytes curv;
	int pde = 0, try_restart;
	static void *lastv = 0;
	pt_t *pt = &vmprocess->vm_pt;
	vir_bytes vmin, vmax;
	vir_bytes holev = NO_MEM;
	int holesize = -1;

	vmin = VM_OWN_MMAPBASE;
	vmax = VM_OWN_MMAPTOP;

	/* Input sanity check. */
	assert(vmin + VM_PAGE_SIZE >= vmin);
	assert(vmax >= vmin + VM_PAGE_SIZE);
	assert((vmin % VM_PAGE_SIZE) == 0);
	assert((vmax % VM_PAGE_SIZE) == 0);
	assert(pages > 0);

	curv = (vir_bytes) lastv;
	if(curv < vmin || curv >= vmax)
		curv = vmin;

	try_restart = 1;

	/* Start looking for a free page starting at vmin. */
	while(curv < vmax) {
		int pte;

		assert(curv >= vmin);
		assert(curv < vmax);

		pde = ARCH_VM_PDE(curv);
		pte = ARCH_VM_PTE(curv);

		if((pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT) &&
		   (pt->pt_pt[pde][pte] & ARCH_VM_PTE_PRESENT)) {
			/* there is a page here - so keep looking for holes */
			holev = NO_MEM;
			holesize = 0;
		} else {
			/* there is no page here - so we have a hole, a bigger
			 * one if we already had one
			 */
			if(holev == NO_MEM) {
				holev = curv;
				holesize = 1;
			} else holesize++;

			assert(holesize > 0);
			assert(holesize <= pages);

			/* if it's big enough, return it */
			if(holesize == pages) {
				lastv = (void*) (curv + VM_PAGE_SIZE);
				return holev;
			}
		}

		curv+=VM_PAGE_SIZE;

		/* if we reached the limit, start scanning from the beginning if
		 * we haven't looked there yet
		 */
		if(curv >= vmax && try_restart) {
			try_restart = 0;
			curv = vmin;
		}
	}

	printf("VM: out of virtual address space in vm\n");

	return NO_MEM;
}

/*===========================================================================*
 *				vm_freepages		     		     *
 *===========================================================================*/
void vm_freepages(vir_bytes vir, int pages)
{
	assert(!(vir % VM_PAGE_SIZE)); 

	if(is_staticaddr(vir)) {
		printf("VM: not freeing static page\n");
		return;
	}

	if(pt_writemap(vmprocess, &vmprocess->vm_pt, vir,
		MAP_NONE, pages*VM_PAGE_SIZE, 0,
		WMF_OVERWRITE | WMF_FREE) != OK)
		panic("vm_freepages: pt_writemap failed");

	vm_self_pages--;

#if SANITYCHECKS
	/* If SANITYCHECKS are on, flush tlb so accessing freed pages is
	 * always trapped, also if not in tlb.
	 */
	if((sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed");
	}
#endif
}

/*===========================================================================*
 *				vm_getsparepage		     		     *
 *===========================================================================*/
static void *vm_getsparepage(phys_bytes *phys)
{
	void *ptr;
	if(reservedqueue_alloc(spare_pagequeue, phys, &ptr) != OK) {
		return NULL;
	}
	assert(ptr);
	return ptr;
}

#if HAVE_SPAREPAGEDIRS
/*===========================================================================*
 *				vm_getsparepagedir	      		     *
 *===========================================================================*/
static void *vm_getsparepagedir(phys_bytes *phys)
{
	int s;
	assert(missing_sparedirs >= 0 && missing_sparedirs <= SPAREPAGEDIRS);
	for(s = 0; s < SPAREPAGEDIRS; s++) {
		if(sparepagedirs[s].pagedir) {
			void *sp;
			sp = sparepagedirs[s].pagedir;
			*phys = sparepagedirs[s].phys;
			sparepagedirs[s].pagedir = NULL;
			missing_sparedirs++;
			assert(missing_sparedirs >= 0 && missing_sparedirs <= SPAREPAGEDIRS);
			return sp;
		}
	}
	return NULL;
}
#endif /* HAVE_SPAREPAGEDIRS */

void *vm_mappages(phys_bytes p, int pages)
{
	vir_bytes loc;
	int r;
	pt_t *pt = &vmprocess->vm_pt;

	/* Where in our virtual address space can we put it? */
	loc = findhole(pages);
	if(loc == NO_MEM) {
		printf("vm_mappages: findhole failed\n");
		return NULL;
	}

	/* Map this page into our address space. */
	if((r=pt_writemap(vmprocess, pt, loc, p, VM_PAGE_SIZE*pages,
		ARCH_VM_PTE_PRESENT | ARCH_VM_PTE_USER | ARCH_VM_PTE_RW |
		ARCH_VM_PTE_CACHED, 0)) != OK) {
		printf("vm_mappages writemap failed\n");
		return NULL;
	}

	if((r=sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed: %d", r);
	}

	assert(loc);

	return (void *) loc;
}

static int pt_init_done;

/*===========================================================================*
 *				vm_allocpage		     		     *
 *===========================================================================*/
void *vm_allocpages(phys_bytes *phys, int reason, int pages)
{
/* Allocate a page for use by VM itself. */
	phys_bytes newpage;
	static int level = 0;
	void *ret;
	u32_t mem_flags = 0;

	assert(reason >= 0 && reason < VMP_CATEGORIES);

	assert(pages > 0);

	level++;

	assert(level >= 1);
	assert(level <= 2);

	if((level > 1) || !pt_init_done) {
		void *s;

		if(pages == 1) s=vm_getsparepage(phys);
#if HAVE_SPAREPAGEDIRS
		else if(pages == ARCH_PAGEDIR_SIZE/VM_PAGE_SIZE)
			s=vm_getsparepagedir(phys);
#endif
		else panic("%d pages", pages);

		level--;
		if(!s) {
			util_stacktrace();
			printf("VM: warning: out of spare pages\n");
		}
		if(!is_staticaddr(s)) vm_self_pages++;
		return s;
	}

#if defined(__arm__)
	if (reason == VMP_PAGEDIR) {
		mem_flags |= PAF_ALIGN16K;
	}
#endif

	/* Allocate page of memory for use by VM. As VM
	 * is trusted, we don't have to pre-clear it.
	 */
	if((newpage = alloc_mem(pages, mem_flags)) == NO_MEM) {
		level--;
		printf("VM: vm_allocpage: alloc_mem failed\n");
		return NULL;
	}

	*phys = CLICK2ABS(newpage);

	if(!(ret = vm_mappages(*phys, pages))) {
		level--;
		printf("VM: vm_allocpage: vm_mappages failed\n");
		return NULL;
	}

	level--;
	vm_self_pages++;

	return ret;
}

void *vm_allocpage(phys_bytes *phys, int reason)
{
	return vm_allocpages(phys, reason, 1);
}

/*===========================================================================*
 *				vm_pagelock		     		     *
 *===========================================================================*/
void vm_pagelock(void *vir, int lockflag)
{
/* Mark a page allocated by vm_allocpage() unwritable, i.e. only for VM. */
	vir_bytes m = (vir_bytes) vir;
	int r;
	pt_entry_t flags = ARCH_VM_PTE_PRESENT | ARCH_VM_PTE_USER;
	pt_t *pt;

	pt = &vmprocess->vm_pt;

	assert(!(m % VM_PAGE_SIZE));

	if(!lockflag)
		flags |= ARCH_VM_PTE_RW;
	else
		flags |= ARCH_VM_PTE_RO;

	flags |= ARCH_VM_PTE_CACHED;

	/* Update flags. */
	if((r=pt_writemap(vmprocess, pt, m, 0, VM_PAGE_SIZE,
		flags, WMF_OVERWRITE | WMF_WRITEFLAGSONLY)) != OK) {
		panic("vm_lockpage: pt_writemap failed");
	}

	if((r=sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed: %d", r);
	}

	return;
}

/*===========================================================================*
 *				vm_addrok		     		     *
 *===========================================================================*/
int vm_addrok(void *vir, int writeflag)
{
	pt_t *pt = &vmprocess->vm_pt;
	int pde, pte;
	vir_bytes v = (vir_bytes) vir;

	pde = ARCH_VM_PDE(v);
	pte = ARCH_VM_PTE(v);

	if(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT)) {
		printf("addr not ok: missing pde %d\n", pde);
		return 0;
	}

	if(writeflag && !ARCH_VM_PTE_ISWRITABLE(pt->pt_dir[pde])) {
		printf("addr not ok: pde %d present but pde unwritable\n", pde);
		return 0;
	}

	if(!(pt->pt_pt[pde][pte] & ARCH_VM_PTE_PRESENT)) {
		printf("addr not ok: missing pde %d / pte %d\n",
			pde, pte);
		return 0;
	}

	if(writeflag && !ARCH_VM_PTE_ISWRITABLE(pt->pt_pt[pde][pte])) {
		printf("addr not ok: pde %d / pte %d present but unwritable\n",
			pde, pte);
		return 0;
	}

	return 1;
}

/*===========================================================================*
 *				pt_ptalloc		     		     *
 *===========================================================================*/
static int pt_ptalloc(pt_t *pt, int pde, pt_entry_t flags)
{
/* Allocate a page table and write its address into the page directory. */
	int i;
	phys_bytes pt_phys;
	pt_entry_t *p;

	/* Argument must make sense. */
	assert(pde >= 0 && pde < ARCH_VM_DIR_ENTRIES);
	assert(!(flags & ~(PTF_ALLFLAGS)));

	/* We don't expect to overwrite page directory entry, nor
	 * storage for the page table.
	 */
	assert(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT));
	assert(!pt->pt_pt[pde]);

	/* Get storage for the page table. The allocation call may in fact
	 * recursively create the directory entry as a side effect. In that
	 * case, we free the newly allocated page and do nothing else.
	 */
	if (!(p = vm_allocpage(&pt_phys, VMP_PAGETABLE)))
		return ENOMEM;
	if (pt->pt_pt[pde]) {
		vm_freepages((vir_bytes) p, 1);
		assert(pt->pt_pt[pde]);
		return OK;
	}
	pt->pt_pt[pde] = p;

	for(i = 0; i < ARCH_VM_PT_ENTRIES; i++)
		pt->pt_pt[pde][i] = 0;	/* Empty entry. */

	/* Make page directory entry.
	 * The PDE is always 'present,' 'writable,' and 'user accessible,'
	 * relying on the PTE for protection.
	 */
	pt->pt_dir[pde] = ARCH_VM_PDE_MAKE(pt_phys, flags);

	return OK;
}

/*===========================================================================*
 *			    pt_ptalloc_in_range		     		     *
 *===========================================================================*/
int pt_ptalloc_in_range(pt_t *pt, vir_bytes start, vir_bytes end,
	pt_entry_t flags, int verify)
{
/* Allocate all the page tables in the range specified. */
	int pde, first_pde, last_pde;

	first_pde = ARCH_VM_PDE(start);
	last_pde = ARCH_VM_PDE(end-1);

	assert(first_pde >= 0);
	assert(last_pde < ARCH_VM_DIR_ENTRIES);

	/* Scan all page-directory entries in the range. */
	for(pde = first_pde; pde <= last_pde; pde++) {
		assert(!ARCH_VM_IS_BIGPAGE(pt->pt_dir[pde]));
		if(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT)) {
			int r;
			if(verify) {
				printf("pt_ptalloc_in_range: no pde %d\n", pde);
				return EFAULT;
			}
			assert(!pt->pt_dir[pde]);
			if((r=pt_ptalloc(pt, pde, flags)) != OK) {
				/* Couldn't do (complete) mapping.
				 * Don't bother freeing any previously
				 * allocated page tables, they're
				 * still writable, don't point to nonsense,
				 * and pt_ptalloc leaves the directory
				 * and other data in a consistent state.
				 */
				return r;
			}
			assert(pt->pt_pt[pde]);
		}
		assert(pt->pt_pt[pde]);
		assert(pt->pt_dir[pde]);
		assert(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT);
	}

	return OK;
}

static const char *ptestr(pt_entry_t pte)
{
#define FLAG(constant, name) {						\
	if(pte & (constant)) { strcat(str, name); strcat(str, " "); }	\
}

	static char str[64];
	if(!(pte & ARCH_VM_PTE_PRESENT)) {
		return "not present";
	}
	str[0] = '\0';
	strcat(str, ARCH_VM_PTE_ISWRITABLE(pte) ? "W " : "R ");
	FLAG(ARCH_VM_PTE_USER, "U");
#if defined(__aarch64__)
	FLAG(AARCH64_VM_AF, "AF");
	FLAG(AARCH64_VM_NG, "NG");
	FLAG(AARCH64_VM_PXN, "PXN");
	FLAG(AARCH64_VM_UXN, "UXN");
	FLAG(AARCH64_VM_SH_INNER, "SH");
#elif defined(__i386__)
	FLAG(I386_VM_PWT, "PWT");
	FLAG(I386_VM_PCD, "PCD");
	FLAG(I386_VM_ACC, "ACC");
	FLAG(I386_VM_DIRTY, "DIRTY");
	FLAG(I386_VM_PS, "PS");
	FLAG(I386_VM_GLOBAL, "G");
	FLAG(I386_VM_PTAVAIL1, "AV1");
	FLAG(I386_VM_PTAVAIL2, "AV2");
	FLAG(I386_VM_PTAVAIL3, "AV3");
#elif defined(__arm__)
	FLAG(ARM_VM_PTE_SUPER, "S");
	FLAG(ARM_VM_PTE_S, "SH");
	FLAG(ARM_VM_PTE_WB, "WB");
	FLAG(ARM_VM_PTE_WT, "WT");
#endif

	return str;
}

/*===========================================================================*
 *			     pt_map_in_range		     		     *
 *===========================================================================*/
int pt_map_in_range(struct vmproc *src_vmp, struct vmproc *dst_vmp,
	vir_bytes start, vir_bytes end)
{
/* Transfer all the mappings from the pt of the source process to the pt of
 * the destination process in the range specified.
 */
	int pde, pte;
	vir_bytes viraddr;
	pt_t *pt, *dst_pt;

	pt = &src_vmp->vm_pt;
	dst_pt = &dst_vmp->vm_pt;

	end = end ? end : VM_DATATOP;
	assert(start % VM_PAGE_SIZE == 0);
	assert(end % VM_PAGE_SIZE == 0);

	assert( /* ARCH_VM_PDE(start) >= 0 && */ start <= end);
	assert(ARCH_VM_PDE(end) < ARCH_VM_DIR_ENTRIES);

#if LU_DEBUG
	printf("VM: pt_map_in_range: src = %d, dst = %d\n",
		src_vmp->vm_endpoint, dst_vmp->vm_endpoint);
	printf("VM: pt_map_in_range: transferring from 0x%08x (pde %d pte %d) to 0x%08x (pde %d pte %d)\n",
		start, ARCH_VM_PDE(start), ARCH_VM_PTE(start),
		end, ARCH_VM_PDE(end), ARCH_VM_PTE(end));
#endif

	/* Scan all page-table entries in the range. */
	for(viraddr = start; viraddr <= end; viraddr += VM_PAGE_SIZE) {
		pde = ARCH_VM_PDE(viraddr);
		if(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT)) {
			if(viraddr == VM_DATATOP) break;
			continue;
		}
		pte = ARCH_VM_PTE(viraddr);
		if(!(pt->pt_pt[pde][pte] & ARCH_VM_PTE_PRESENT)) {
			if(viraddr == VM_DATATOP) break;
			continue;
		}

		/* Transfer the mapping. */
		dst_pt->pt_pt[pde][pte] = pt->pt_pt[pde][pte];
		assert(dst_pt->pt_pt[pde]);

                if(viraddr == VM_DATATOP) break;
	}

	return OK;
}

/*===========================================================================*
 *				pt_ptmap		     		     *
 *===========================================================================*/
int pt_ptmap(struct vmproc *src_vmp, struct vmproc *dst_vmp)
{
/* Transfer mappings to page dir and page tables from source process and
 * destination process.
 */
	int pde, r;
	phys_bytes physaddr;
	vir_bytes viraddr;
	pt_t *pt;

	pt = &src_vmp->vm_pt;

#if LU_DEBUG
	printf("VM: pt_ptmap: src = %d, dst = %d\n",
		src_vmp->vm_endpoint, dst_vmp->vm_endpoint);
#endif

	/* Transfer mapping to the page directory - the whole block of it,
	 * which on the 32-bit ports is the directory and here also holds the
	 * levels above it.
	 */
	viraddr = (vir_bytes) pt->pt_root;
	physaddr = pt->pt_root_phys & ARCH_VM_ADDR_MASK;
	if((r=pt_writemap(dst_vmp, &dst_vmp->vm_pt, viraddr, physaddr,
		ARCH_PAGEDIR_SIZE,
		ARCH_VM_PTE_PRESENT | ARCH_VM_PTE_USER | ARCH_VM_PTE_RW |
		ARCH_VM_PTE_CACHED,
		WMF_OVERWRITE)) != OK) {
		return r;
	}
#if LU_DEBUG
	printf("VM: pt_ptmap: transferred mapping to page dir: 0x%08x (0x%08x)\n",
		viraddr, physaddr);
#endif

	/* Scan all non-reserved page-directory entries. */
	for(pde=0; pde < kern_start_pde; pde++) {
		if(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT)) {
			continue;
		}

		if(!pt->pt_pt[pde]) { panic("pde %d empty\n", pde); }

		/* Transfer mapping to the page table. */
		viraddr = (vir_bytes) pt->pt_pt[pde];
		physaddr = pt->pt_dir[pde] & ARCH_VM_PDE_MASK;
		assert(viraddr);
		if((r=pt_writemap(dst_vmp, &dst_vmp->vm_pt, viraddr, physaddr,
			VM_PAGE_SIZE,
			ARCH_VM_PTE_PRESENT | ARCH_VM_PTE_USER |
			ARCH_VM_PTE_RW | ARCH_VM_PTE_CACHED,
			WMF_OVERWRITE)) != OK) {
			return r;
		}
	}

	return OK;
}

void pt_clearmapcache(void)
{
	/* Make sure kernel will invalidate tlb when using current
	 * pagetable (i.e. vm's) to make new mappings before new cr3
	 * is loaded.
	 */
	if(sys_vmctl(SELF, VMCTL_CLEARMAPCACHE, 0) != OK)
		panic("VMCTL_CLEARMAPCACHE failed");
}

int pt_writable(struct vmproc *vmp, vir_bytes v)
{
	pt_entry_t entry;
	pt_t *pt = &vmp->vm_pt;
	assert(!(v % VM_PAGE_SIZE));
	int pde = ARCH_VM_PDE(v);
	int pte = ARCH_VM_PTE(v);

	assert(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT);
	assert(pt->pt_pt[pde]);

	entry = pt->pt_pt[pde][pte];

	return(ARCH_VM_PTE_ISWRITABLE(entry) ? 1 : 0);
}

/*===========================================================================*
 *				pt_writemap		     		     *
 *===========================================================================*/
int pt_writemap(struct vmproc * vmp,
			pt_t *pt,
			vir_bytes v,
			phys_bytes physaddr,
			size_t bytes,
			pt_entry_t flags,
			u32_t writemapflags)
{
/* Write mapping into page table. Allocate a new page table if necessary. */
/* Page directory and table entries for this virtual address. */
	int p, pages;
	int verify = 0;
	int ret = OK;

#ifdef CONFIG_SMP
	int vminhibit_clear = 0;
	/* FIXME
	 * don't do it everytime, stop the process only on the first change and
	 * resume the execution on the last change. Do in a wrapper of this
	 * function
	 */
	if (vmp && vmp->vm_endpoint != NONE && vmp->vm_endpoint != VM_PROC_NR &&
			!(vmp->vm_flags & VMF_EXITING)) {
		sys_vmctl(vmp->vm_endpoint, VMCTL_VMINHIBIT_SET, 0);
		vminhibit_clear = 1;
	}
#endif

	if(writemapflags & WMF_VERIFY)
		verify = 1;

	assert(!(bytes % VM_PAGE_SIZE));
	assert(!(flags & ~(PTF_ALLFLAGS)));

	pages = bytes / VM_PAGE_SIZE;

	/* MAP_NONE means to clear the mapping. It doesn't matter
	 * what's actually written into the PTE if PRESENT
	 * isn't on, so we can just write MAP_NONE into it.
	 */
	assert(physaddr == MAP_NONE || (flags & ARCH_VM_PTE_PRESENT));
	assert(physaddr != MAP_NONE || !flags);

	/* First make sure all the necessary page tables are allocated,
	 * before we start writing in any of them, because it's a pain
	 * to undo our work properly.
	 */
	ret = pt_ptalloc_in_range(pt, v, v + VM_PAGE_SIZE*pages, flags, verify);
	if(ret != OK) {
		printf("VM: writemap: pt_ptalloc_in_range failed\n");
		goto resume_exit;
	}

	/* Now write in them. */
	for(p = 0; p < pages; p++) {
		pt_entry_t entry;
		int pde = ARCH_VM_PDE(v);
		int pte = ARCH_VM_PTE(v);

		assert(!(v % VM_PAGE_SIZE));
		assert(pte >= 0 && pte < ARCH_VM_PT_ENTRIES);
		assert(pde >= 0 && pde < ARCH_VM_DIR_ENTRIES);

		/* Page table has to be there. */
		assert(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT);

		/* We do not expect it to be a bigpage. */
		assert(!ARCH_VM_IS_BIGPAGE(pt->pt_dir[pde]));

		/* Make sure page directory entry for this page table
		 * is marked present and page table entry is available.
		 */
		assert(pt->pt_pt[pde]);

		if(writemapflags & (WMF_WRITEFLAGSONLY|WMF_FREE)) {
			physaddr = pt->pt_pt[pde][pte] & ARCH_VM_ADDR_MASK;
		}

		if(writemapflags & WMF_FREE) {
			free_mem(ABS2CLICK(physaddr), 1);
		}

		/* Entry we will write. */
		entry = (physaddr & ARCH_VM_ADDR_MASK) | flags;

		if(verify) {
			pt_entry_t maskedentry;
			maskedentry = pt->pt_pt[pde][pte];
#if defined(__i386__)
			maskedentry &= ~(I386_VM_ACC|I386_VM_DIRTY);
#endif
			/* Verify pagetable entry. */
			if(ARCH_VM_PTE_ISWRITABLE(entry)) {
				/* If we expect a writable page, allow a readonly page. */
#if defined(__i386__)
				maskedentry |= ARCH_VM_PTE_RW;
#else
				maskedentry &= ~ARCH_VM_PTE_RO;
#endif
			}
#if defined(__arm__)
			maskedentry &= ~(ARM_VM_PTE_WB|ARM_VM_PTE_WT);
#endif
			if(maskedentry != entry) {
				printf("pt_writemap: mismatch: ");
				if((entry & ARCH_VM_ADDR_MASK) !=
					(maskedentry & ARCH_VM_ADDR_MASK)) {
					printf("pt_writemap: physaddr mismatch (0x%lx, 0x%lx); ",
						(long)entry, (long)maskedentry);
				} else printf("phys ok; ");
				printf(" flags: found %s; ",
					ptestr(pt->pt_pt[pde][pte]));
				printf(" masked %s; ",
					ptestr(maskedentry));
				printf(" expected %s\n", ptestr(entry));
				printf("found 0x%lx, wanted 0x%lx\n",
					(unsigned long)pt->pt_pt[pde][pte],
					(unsigned long)entry);
				ret = EFAULT;
				goto resume_exit;
			}
		} else {
			/* Write pagetable entry. */
			pt->pt_pt[pde][pte] = entry;
		}

		physaddr += VM_PAGE_SIZE;
		v += VM_PAGE_SIZE;
	}

resume_exit:

#ifdef CONFIG_SMP
	if (vminhibit_clear) {
		assert(vmp && vmp->vm_endpoint != NONE && vmp->vm_endpoint != VM_PROC_NR &&
			!(vmp->vm_flags & VMF_EXITING));
		sys_vmctl(vmp->vm_endpoint, VMCTL_VMINHIBIT_CLEAR, 0);
	}
#endif

	return ret;
}

/*===========================================================================*
 *				pt_checkrange		     		     *
 *===========================================================================*/
int pt_checkrange(pt_t *pt, vir_bytes v,  size_t bytes,
	int write)
{
	int p, pages;

	assert(!(bytes % VM_PAGE_SIZE));

	pages = bytes / VM_PAGE_SIZE;

	for(p = 0; p < pages; p++) {
		int pde = ARCH_VM_PDE(v);
		int pte = ARCH_VM_PTE(v);

		assert(!(v % VM_PAGE_SIZE));
		assert(pte >= 0 && pte < ARCH_VM_PT_ENTRIES);
		assert(pde >= 0 && pde < ARCH_VM_DIR_ENTRIES);

		/* Page table has to be there. */
		if(!(pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT))
			return EFAULT;

		/* Make sure page directory entry for this page table
		 * is marked present and page table entry is available.
		 */
		assert((pt->pt_dir[pde] & ARCH_VM_PDE_PRESENT) && pt->pt_pt[pde]);

		if(!(pt->pt_pt[pde][pte] & ARCH_VM_PTE_PRESENT)) {
			return EFAULT;
		}

		if(write && !ARCH_VM_PTE_ISWRITABLE(pt->pt_pt[pde][pte])) {
			return EFAULT;
		}

		v += VM_PAGE_SIZE;
	}

	return OK;
}

/*===========================================================================*
 *				pt_root_init		     		     *
 *===========================================================================*/
/* Lay out the block of tables an address space is made of, and say where the
 * directory is inside it.
 *
 * Where the architecture translates through the two levels this file works
 * in, the block is the directory and there is nothing to do. Where there are
 * more, the levels above are a spine at the front of the block: one table
 * per level, each naming the next, and the last naming the pages of the
 * directory in order. Only entry zero of each upper table is ever used,
 * because the whole address space fits underneath it - which is exactly what
 * lets the directory be one flat array.
 *
 * Written once here and never touched again: everything that follows changes
 * the directory and the tables below it.
 */
static void pt_root_init(pt_t *pt)
{
#if ARCH_PT_SPINE_PAGES == 0
	pt->pt_dir = pt->pt_root;
#else
	pt_entry_t *last;
	int i;

	for(i = 0; i < ARCH_PT_SPINE_PAGES; i++)
		memset(pt->pt_root + i * ARCH_VM_PT_ENTRIES, 0, VM_PAGE_SIZE);

	for(i = 0; i < ARCH_PT_SPINE_PAGES - 1; i++)
		pt->pt_root[i * ARCH_VM_PT_ENTRIES] = ARCH_VM_PDE_MAKE(
			pt->pt_root_phys + (i + 1) * VM_PAGE_SIZE, 0);

	last = pt->pt_root + (ARCH_PT_SPINE_PAGES - 1) * ARCH_VM_PT_ENTRIES;
	for(i = 0; i < ARCH_PT_DIR_PAGES; i++)
		last[i] = ARCH_VM_PDE_MAKE(pt->pt_root_phys +
			ARCH_PT_DIR_OFFSET + i * VM_PAGE_SIZE, 0);

	pt->pt_dir = pt->pt_root + ARCH_PT_SPINE_PAGES * ARCH_VM_PT_ENTRIES;
#endif
}

/*===========================================================================*
 *				pt_new			     		     *
 *===========================================================================*/
int pt_new(pt_t *pt)
{
/* Allocate a pagetable root. Allocate a page-aligned page directory
 * and set them to 0 (indicating no page tables are allocated). Lookup
 * its physical address as we'll need that in the future. Verify it's
 * page-aligned.
 */
	int i, r;

	/* Don't ever re-allocate/re-move a certain process slot's
	 * page directory once it's been created. This is a fraction
	 * faster, but also avoids having to invalidate the page
	 * mappings from in-kernel page tables pointing to
	 * the page directories (the page_directories data).
	 */
        if(!pt->pt_root &&
          !(pt->pt_root = vm_allocpages(&pt->pt_root_phys,
	  	VMP_PAGEDIR, ARCH_PAGEDIR_SIZE/VM_PAGE_SIZE))) {
		return ENOMEM;
	}

	assert(!(pt->pt_root_phys % ARCH_PAGEDIR_ALIGN));

	pt_root_init(pt);

	for(i = 0; i < ARCH_VM_DIR_ENTRIES; i++) {
		pt->pt_dir[i] = 0; /* invalid entry (PRESENT bit = 0) */
		pt->pt_pt[i] = NULL;
	}

	/* Where to start looking for free virtual address space? */
	pt->pt_virtop = 0;

        /* Map in kernel. */
        if((r=pt_mapkernel(pt)) != OK)
		return r;

	return OK;
}

#if ARCH_VM_KERNEL_IN_PROC_PT
static int freepde(void)
{
	int p = kernel_boot_info.freepde_start++;
	assert(kernel_boot_info.freepde_start < ARCH_VM_DIR_ENTRIES);
	return p;
}
#endif

void pt_allocate_kernel_mapped_pagetables(void)
{
#if ARCH_VM_KERNEL_IN_PROC_PT
	/* Reserve PDEs available for mapping in the page directories. */
	int pd;
	for(pd = 0; pd < MAX_PAGEDIR_PDES; pd++) {
		struct pdm *pdm = &pagedir_mappings[pd];
		if(!pdm->pdeno)  {
			pdm->pdeno = freepde();
			assert(pdm->pdeno);
		}
		phys_bytes ph;

		/* Allocate us a page table in which to
		 * remember page directory pointers.
		 */
		if(!(pdm->page_directories =
			vm_allocpage(&ph, VMP_PAGETABLE))) {
			panic("no virt addr for vm mappings");
		}
		memset(pdm->page_directories, 0, VM_PAGE_SIZE);
		pdm->phys = ph;

#if defined(__i386__)
		pdm->val = (ph & ARCH_VM_ADDR_MASK) |
			ARCH_VM_PDE_PRESENT | ARCH_VM_PTE_RW;
#elif defined(__arm__)
		pdm->val = (ph & ARCH_VM_PDE_MASK)
			| ARCH_VM_PDE_PRESENT
			| ARM_VM_PTE_CACHED
			| ARM_VM_PDE_DOMAIN; //LSC FIXME
#endif
	}
#endif /* ARCH_VM_KERNEL_IN_PROC_PT */
}

static void pt_copy(pt_t *dst, pt_t *src)
{
	int pde;
	for(pde=0; pde < kern_start_pde; pde++) {
		if(!(src->pt_dir[pde] & ARCH_VM_PDE_PRESENT)) {
			continue;
		}
		assert(!ARCH_VM_IS_BIGPAGE(src->pt_dir[pde]));
		if(!src->pt_pt[pde]) { panic("pde %d empty\n", pde); }
		if(pt_ptalloc(dst, pde, 0) != OK)
			panic("pt_ptalloc failed");
		memcpy(dst->pt_pt[pde], src->pt_pt[pde],
			ARCH_VM_PT_ENTRIES * sizeof(*dst->pt_pt[pde]));
	}
}

#if !ARCH_VM_KERNEL_IN_PROC_PT
/*===========================================================================*
 *			     pt_copy_bootstrap				     *
 *===========================================================================*/
/* Rebuild, in VM's own page table, the mappings the kernel made for VM before
 * VM could make any of its own.
 *
 * Where the kernel and the process share a page table this is a walk over one
 * array, and pt_init() does it inline. Here the kernel's tables are a tree
 * that pg_utils.c laid out to suit itself - nothing says its level 2 tables
 * adjoin, the way the ones VM allocates do - so the tree is walked a level at
 * a time, each table read into a buffer by a physical copy, and the leaf
 * tables copied into the ones pt_ptalloc() has just made.
 *
 * Only what is under the first entry of the top table is looked at, which is
 * the whole of what this page table can describe. The kernel maps nothing for
 * VM above it: it is in TTBR1, and it loaded VM from an ELF whose addresses
 * are all low.
 */
static void pt_copy_bootstrap(pt_t *newpt, phys_bytes root)
{
	static pt_entry_t table[ARCH_PT_SPINE_PAGES + 1][ARCH_VM_PT_ENTRIES];
	int spine, l1, l2;

	/* Down the spine: entry zero at every level above the directory. */
	for(spine = 0; spine < ARCH_PT_SPINE_PAGES; spine++) {
		if(sys_vircopy(NONE, root, SELF, (vir_bytes) table[spine],
			VM_PAGE_SIZE, 0) != OK)
			panic("VM: sys_vircopy of the boot page table failed");
		if(!(table[spine][0] & ARCH_VM_PDE_PRESENT))
			panic("VM: the kernel left no mappings for VM");
		assert(!ARCH_VM_IS_BIGPAGE(table[spine][0]));
		root = table[spine][0] & ARCH_VM_PDE_MASK;
	}

	/* root now names the first page of the kernel's directory. Its pages
	 * need not adjoin, so they are read one at a time.
	 */
	for(l1 = 0; l1 < ARCH_PT_DIR_PAGES; l1++) {
		pt_entry_t *dir = table[ARCH_PT_SPINE_PAGES];

		if(l1 > 0) {
			/* Which page the next part of the directory is in is
			 * the business of the level above it, so ask again. */
			if(!(table[ARCH_PT_SPINE_PAGES-1][l1] &
				ARCH_VM_PDE_PRESENT))
				continue;
			root = table[ARCH_PT_SPINE_PAGES-1][l1] &
				ARCH_VM_PDE_MASK;
		}

		if(sys_vircopy(NONE, root, SELF, (vir_bytes) dir,
			VM_PAGE_SIZE, 0) != OK)
			panic("VM: sys_vircopy of a directory page failed");

		for(l2 = 0; l2 < ARCH_VM_PT_ENTRIES; l2++) {
			pt_entry_t entry = dir[l2];
			int p = l1 * ARCH_VM_PT_ENTRIES + l2;
			phys_bytes ptaddr_kern, ptaddr_us;

			if(!(entry & ARCH_VM_PDE_PRESENT)) continue;
			if(ARCH_VM_IS_BIGPAGE(entry)) continue;

			if(pt_ptalloc(newpt, p, 0) != OK)
				panic("pt_ptalloc failed");
			assert(newpt->pt_dir[p] & ARCH_VM_PDE_PRESENT);

			ptaddr_kern = entry & ARCH_VM_PDE_MASK;
			ptaddr_us = newpt->pt_dir[p] & ARCH_VM_PDE_MASK;

			if(sys_abscopy(ptaddr_kern, ptaddr_us,
				VM_PAGE_SIZE) != OK)
				panic("pt_init: abscopy failed");
		}
	}
}
#endif /* !ARCH_VM_KERNEL_IN_PROC_PT */

/*===========================================================================*
 *                              pt_init                                      *
 *===========================================================================*/
void pt_init(void)
{
        pt_t *newpt, newpt_dyn;
        int s, r;
#if ARCH_VM_KERNEL_IN_PROC_PT
	int p;
#endif
	phys_bytes phys;
	vir_bytes sparepages_mem;
#if HAVE_SPAREPAGEDIRS
	vir_bytes sparepagedirs_mem;
#endif
#if ARCH_VM_KERNEL_IN_PROC_PT
	static pt_entry_t currentpagedir[ARCH_VM_DIR_ENTRIES];
	int m = kernel_boot_info.kern_mod;
#endif
#if defined(__i386__)
	int global_bit_ok = 0;
	u32_t mypdbr; /* Page Directory Base Register (cr3) value */
#else
	u32_t myttbr;
#endif

#if ARCH_VM_KERNEL_IN_PROC_PT
	/* Find what the physical location of the kernel is. */
	assert(m >= 0);
	assert(m < kernel_boot_info.mods_with_kernel);
	assert(kernel_boot_info.mods_with_kernel < MULTIBOOT_MAX_MODS);
	kern_mb_mod = &kernel_boot_info.module_list[m];
	kern_size = kern_mb_mod->mod_end - kern_mb_mod->mod_start;
	assert(!(kern_mb_mod->mod_start % ARCH_BIG_PAGE_SIZE));
	assert(!(kernel_boot_info.vir_kern_start % ARCH_BIG_PAGE_SIZE));
	kern_start_pde = kernel_boot_info.vir_kern_start / ARCH_BIG_PAGE_SIZE;
#else
	kern_start_pde = ARCH_VM_PDE(ARCH_KERNMAP_BASE);
#endif

        /* Get ourselves spare pages. */
        sparepages_mem = (vir_bytes) static_sparepages;
	assert(!(sparepages_mem % VM_PAGE_SIZE));

#if HAVE_SPAREPAGEDIRS
        /* Get ourselves spare pagedirs. */
	sparepagedirs_mem = (vir_bytes) static_sparepagedirs;
	assert(!(sparepagedirs_mem % ARCH_PAGEDIR_ALIGN));
#endif

	/* Spare pages are used to allocate memory before VM has its own page
	 * table that things (i.e. arbitrary physical memory) can be mapped into.
	 * We get it by pre-allocating it in our bss (allocated and mapped in by
	 * the kernel) in static_sparepages. We also need the physical addresses
	 * though; we look them up now so they are ready for use.
	 */
#if HAVE_SPAREPAGEDIRS
        missing_sparedirs = 0;
        assert(STATIC_SPAREPAGEDIRS <= SPAREPAGEDIRS);
        for(s = 0; s < SPAREPAGEDIRS; s++) {
		vir_bytes v = (sparepagedirs_mem + s*ARCH_PAGEDIR_SIZE);;
		phys_bytes ph;
        	if((r=sys_umap(SELF, VM_D, (vir_bytes) v,
	                ARCH_PAGEDIR_SIZE, &ph)) != OK)
				panic("pt_init: sys_umap failed: %d", r);
        	if(s >= STATIC_SPAREPAGEDIRS) {
        		sparepagedirs[s].pagedir = NULL;
        		missing_sparedirs++;
        		continue;
        	}
        	sparepagedirs[s].pagedir = (void *) v;
        	sparepagedirs[s].phys = ph;
        }
#endif

	if(!(spare_pagequeue = reservedqueue_new(SPAREPAGES, 1, 1, 0)))
		panic("reservedqueue_new for single pages failed");

        assert(STATIC_SPAREPAGES < SPAREPAGES);
        for(s = 0; s < STATIC_SPAREPAGES; s++) {
		void *v = (void *) (sparepages_mem + s*VM_PAGE_SIZE);
		phys_bytes ph;
		if((r=sys_umap(SELF, VM_D, (vir_bytes) v,
	                VM_PAGE_SIZE*SPAREPAGES, &ph)) != OK)
				panic("pt_init: sys_umap failed: %d", r);
		reservedqueue_add(spare_pagequeue, v, ph);
        }

#if defined(__i386__)
	/* global bit and 4MB pages available? */
	global_bit_ok = _cpufeature(_CPUF_I386_PGE);
	bigpage_ok = _cpufeature(_CPUF_I386_PSE);

	/* Set bit for PTE's and PDE's if available. */
	if(global_bit_ok)
		global_bit = I386_VM_GLOBAL;
#endif

	/* Now reserve another pde for kernel's own mappings. */
	{
		phys_bytes addr, len;
		int flags, pindex = 0;
		vir_bytes offset;
#if ARCH_VM_KERNEL_IN_PROC_PT
		int kernmap_pde;

		kernmap_pde = freepde();
		offset = kernmap_pde * ARCH_BIG_PAGE_SIZE;
#else
		/*
		 * Above the process's own address space, in the part of the
		 * directory regions never reach. There are no directory
		 * entries to reserve here: the kernel has an address space of
		 * its own and takes none of this one.
		 */
		offset = ARCH_KERNMAP_BASE;
#endif

		while(sys_vmctl_get_mapping(pindex, &addr, &len,
			&flags) == OK)  {
			vir_bytes vir;
			if(pindex >= MAX_KERNMAPPINGS)
                		panic("VM: too many kernel mappings: %d", pindex);
			kern_mappings[pindex].phys_addr = addr;
			kern_mappings[pindex].len = len;
			kern_mappings[pindex].vir_addr = offset;
			kern_mappings[pindex].flags =
				ARCH_VM_PTE_PRESENT;
			if(flags & VMMF_UNCACHED)
				kern_mappings[pindex].flags |= PTF_NOCACHE;
			else
				kern_mappings[pindex].flags |= ARCH_VM_PTE_CACHED;
			if(flags & VMMF_USER)
				kern_mappings[pindex].flags |= ARCH_VM_PTE_USER;
			else
				kern_mappings[pindex].flags |= ARCH_VM_PTE_SUPER;
			if(flags & VMMF_WRITE)
				kern_mappings[pindex].flags |= ARCH_VM_PTE_RW;
			else
				kern_mappings[pindex].flags |= ARCH_VM_PTE_RO;

#if defined(__i386__)
			if(flags & VMMF_GLO)
				kern_mappings[pindex].flags |= I386_VM_GLOBAL;
#endif

			if(addr % VM_PAGE_SIZE)
                		panic("VM: addr unaligned: %lu", addr);
			if(len % VM_PAGE_SIZE)
                		panic("VM: len unaligned: %lu", len);
			vir = offset;
			if(sys_vmctl_reply_mapping(pindex, vir) != OK)
                		panic("VM: reply failed");
			offset += len;
			pindex++;
			kernmappings++;

#if ARCH_VM_KERNEL_IN_PROC_PT
			{
			int usedpde = ARCH_VM_PDE(offset);
			while(usedpde > kernmap_pde) {
				int newpde = freepde();
				assert(newpde == kernmap_pde+1);
				kernmap_pde = newpde;
			}
			}
#endif
		}
	}

	pt_allocate_kernel_mapped_pagetables();

	/* Allright. Now. We have to make our own page directory and page tables,
	 * that the kernel has already set up, accessible to us. It's easier to
	 * understand if we just copy all the required pages (i.e. page directory
	 * and page tables), and set up the pointers as if VM had done it itself.
	 *
	 * This allocation will happen without using any page table, and just
	 * uses spare pages.
	 */
        newpt = &vmprocess->vm_pt;
	if(pt_new(newpt) != OK)
		panic("vm pt_new failed");

	/* Get our current pagedir so we can see it. */
#if defined(__i386__)
	if(sys_vmctl_get_pdbr(SELF, &mypdbr) != OK)
#else
	if(sys_vmctl_get_pdbr(SELF, &myttbr) != OK)
#endif

		panic("VM: sys_vmctl_get_pdbr failed");

	/* We have mapped in kernel ourselves; now copy mappings for VM
	 * that kernel made, including allocations for BSS. Skip identity
	 * mapping bits; just map in VM.
	 */
#if ARCH_VM_KERNEL_IN_PROC_PT
#if defined(__i386__)
	if(sys_vircopy(NONE, mypdbr, SELF,
		(vir_bytes) currentpagedir, VM_PAGE_SIZE, 0) != OK)
#elif defined(__arm__)
	if(sys_vircopy(NONE, myttbr, SELF,
		(vir_bytes) currentpagedir, ARCH_PAGEDIR_SIZE, 0) != OK)
#endif
		panic("VM: sys_vircopy failed");

	for(p = 0; p < ARCH_VM_DIR_ENTRIES; p++) {
		pt_entry_t entry = currentpagedir[p];
		phys_bytes ptaddr_kern, ptaddr_us;

		/* BIGPAGEs are kernel mapping (do ourselves) or boot
		 * identity mapping (don't want).
		 */
		if(!(entry & ARCH_VM_PDE_PRESENT)) continue;
		if(ARCH_VM_IS_BIGPAGE(entry)) continue;

		if(pt_ptalloc(newpt, p, 0) != OK)
			panic("pt_ptalloc failed");
		assert(newpt->pt_dir[p] & ARCH_VM_PDE_PRESENT);

		ptaddr_kern = entry & ARCH_VM_PDE_MASK;
		ptaddr_us = newpt->pt_dir[p] & ARCH_VM_PDE_MASK;

		/* Copy kernel-initialized pagetable contents into our
		 * normally accessible pagetable.
		 */
                if(sys_abscopy(ptaddr_kern, ptaddr_us, VM_PAGE_SIZE) != OK)
			panic("pt_init: abscopy failed");
	}
#else
	pt_copy_bootstrap(newpt, myttbr);
#endif

	/* Inform kernel vm has a newly built page table. */
	assert(vmproc[VM_PROC_NR].vm_endpoint == VM_PROC_NR);
	pt_bind(newpt, &vmproc[VM_PROC_NR]);

	pt_init_done = 1;

	/* VM is now fully functional in that it can dynamically allocate memory
	 * for itself.
	 *
	 * We don't want to keep using the bootstrap statically allocated spare
	 * pages though, as the physical addresses will change on liveupdate. So we
	 * re-do part of the initialization now with purely dynamically allocated
	 * memory. First throw out the static pool.
	 *
	 * Then allocate the kernel-shared-pagetables and VM pagetables with dynamic
	 * memory.
	 */

	alloc_cycle();                          /* Make sure allocating works */
	while(vm_getsparepage(&phys)) ;		/* Use up all static pages */
	alloc_cycle();                          /* Refill spares with dynamic */
	pt_allocate_kernel_mapped_pagetables(); /* Reallocate in-kernel pages */
	pt_bind(newpt, &vmproc[VM_PROC_NR]);    /* Recalculate */
	pt_mapkernel(newpt);                    /* Rewrite pagetable info */

	/* Flush TLB just in case any of those mappings have been touched */
	if((sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed");
	}

	/* Recreate VM page table with dynamic-only allocations */
	memset(&newpt_dyn, 0, sizeof(newpt_dyn));
	pt_new(&newpt_dyn);
	pt_copy(&newpt_dyn, newpt);
	memcpy(newpt, &newpt_dyn, sizeof(*newpt));

	pt_bind(newpt, &vmproc[VM_PROC_NR]);    /* Recalculate */
	pt_mapkernel(newpt);                    /* Rewrite pagetable info */

	/* Flush TLB just in case any of those mappings have been touched */
	if((sys_vmctl(SELF, VMCTL_FLUSHTLB, 0)) != OK) {
		panic("VMCTL_FLUSHTLB failed");
	}

        /* All OK. */
        return;
}

/*===========================================================================*
 *				pt_bind			     		     *
 *===========================================================================*/
int pt_bind(pt_t *pt, struct vmproc *who)
{
#if !ARCH_VM_KERNEL_IN_PROC_PT
	/* Basic sanity checks. */
	assert(who);
	assert(who->vm_flags & VMF_INUSE);
	assert(pt);

	/*
	 * Nothing to publish. The kernel reaches any page table by its
	 * physical address, through the linear map of RAM in its own half of
	 * the address space, so it does not need this table mapped anywhere
	 * and there is no table of page directory pointers to maintain. What
	 * it is told is the root's physical address, which is what TTBR0
	 * gets, and where VM sees that root - which the kernel only records,
	 * to hand back through VMCTL_GET_PDBR.
	 */
	return sys_vmctl_set_addrspace(who->vm_endpoint, pt->pt_root_phys,
		pt->pt_root);
#else
	int procslot, pdeslot;
	u32_t phys;
	void *pdes;
	int pagedir_pde;
	int slots_per_pde;
	int pages_per_pagedir = ARCH_PAGEDIR_SIZE/VM_PAGE_SIZE;
	struct pdm *pdm;

	slots_per_pde = ARCH_VM_PT_ENTRIES / pages_per_pagedir;

	/* Basic sanity checks. */
	assert(who);
	assert(who->vm_flags & VMF_INUSE);
	assert(pt);

	procslot = who->vm_slot;
	pdm = &pagedir_mappings[procslot/slots_per_pde];
	pdeslot = procslot%slots_per_pde;
	pagedir_pde = pdm->pdeno;
	assert(pdeslot >= 0);
	assert(procslot < ELEMENTS(vmproc));
	assert(pdeslot < ARCH_VM_PT_ENTRIES / pages_per_pagedir);
	assert(pagedir_pde >= 0);

	phys = pt->pt_root_phys & ARCH_VM_ADDR_MASK;
	assert(pt->pt_root_phys == phys);
	assert(!(pt->pt_root_phys % ARCH_PAGEDIR_ALIGN));

	/* Update "page directory pagetable." */
#if defined(__i386__)
	pdm->page_directories[pdeslot] =
		phys | ARCH_VM_PDE_PRESENT|ARCH_VM_PTE_RW;
#elif defined(__arm__)
{
	int i;
	for (i = 0; i < pages_per_pagedir; i++) {
		pdm->page_directories[pdeslot*pages_per_pagedir+i] =
			(phys+i*VM_PAGE_SIZE)
			| ARCH_VM_PTE_PRESENT
			| ARCH_VM_PTE_RW
			| ARM_VM_PTE_CACHED
			| ARCH_VM_PTE_USER; //LSC FIXME
	}
}
#endif

	/* This is where the PDE's will be visible to the kernel
	 * in its address space.
	 */
	pdes = (void *) (pagedir_pde*ARCH_BIG_PAGE_SIZE + 
#if defined(__i386__)
			pdeslot * VM_PAGE_SIZE);
#elif defined(__arm__)
			pdeslot * ARCH_PAGEDIR_SIZE);
#endif

	/* Tell kernel about new page table root. */
	return sys_vmctl_set_addrspace(who->vm_endpoint, pt->pt_root_phys,
		pdes);
#endif /* ARCH_VM_KERNEL_IN_PROC_PT */
}

/*===========================================================================*
 *				pt_free			     		     *
 *===========================================================================*/
void pt_free(pt_t *pt)
{
/* Free memory associated with this pagetable. */
	int i;

	for(i = 0; i < ARCH_VM_DIR_ENTRIES; i++)
		if(pt->pt_pt[i])
			vm_freepages((vir_bytes) pt->pt_pt[i], 1);

	return;
}

/*===========================================================================*
 *				pt_mapkernel		     		     *
 *===========================================================================*/
int pt_mapkernel(pt_t *pt)
{
	int i;
#if ARCH_VM_KERNEL_IN_PROC_PT
	int kern_pde = kern_start_pde;
	phys_bytes addr, mapped = 0;

        /* Any page table needs to map in the kernel address space. */
	assert(bigpage_ok);
	assert(kern_pde >= 0);

	/* pt_init() has made sure this is ok. */
	addr = kern_mb_mod->mod_start;

	/* Actually mapping in kernel */
	while(mapped < kern_size) {
#if defined(__i386__)
		pt->pt_dir[kern_pde] = addr | ARCH_VM_PDE_PRESENT |
			ARCH_VM_BIGPAGE | ARCH_VM_PTE_RW | global_bit;
#elif defined(__arm__)
		pt->pt_dir[kern_pde] = (addr & ARM_VM_SECTION_MASK)
			| ARM_VM_SECTION
			| ARM_VM_SECTION_DOMAIN
			| ARM_VM_SECTION_CACHED
			| ARM_VM_SECTION_SUPER;
#endif
		kern_pde++;
		mapped += ARCH_BIG_PAGE_SIZE;
		addr += ARCH_BIG_PAGE_SIZE;
	}

	/* Kernel also wants to know about all page directories. */
	{
		int pd;
		for(pd = 0; pd < MAX_PAGEDIR_PDES; pd++) {
			struct pdm *pdm = &pagedir_mappings[pd];

			assert(pdm->pdeno > 0);
			assert(pdm->pdeno > kern_pde);
			pt->pt_dir[pdm->pdeno] = pdm->val;
		}
	}
#endif /* ARCH_VM_KERNEL_IN_PROC_PT */

	/* Kernel also wants various mappings of its own.
	 *
	 * Where the kernel has an address space of its own these are all
	 * that is left of this function: not the kernel, which is in it and
	 * no business of any process page table, but the pages it publishes
	 * to user space - minix_kerninfo and what it points at.
	 */
	for(i = 0; i < kernmappings; i++) {
		int r;
		if((r=pt_writemap(NULL, pt,
			kern_mappings[i].vir_addr,
			kern_mappings[i].phys_addr,
			kern_mappings[i].len,
			kern_mappings[i].flags, 0)) != OK) {
			return r;
		}

	}

	return OK;
}

int get_vm_self_pages(void) { return vm_self_pages; }
