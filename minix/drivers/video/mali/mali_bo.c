/*
 * Buffer objects: memory the GPU and the processor share.
 *
 * Where the memory comes from, and why so (PORTING-LOG, "Этап 11.6").
 * The service allocates it - anonymous memory of its own, one mmap(2)
 * region per buffer, every page present (MAP_PREALLOC) - and learns the
 * physical address of each page with sys_umap(), so that no buffer, a
 * 128 MiB tiler heap included, needs contiguous RAM: the GPU's MMU puts
 * the pages together.  The client gets the same pages through
 * vm_remap(), the way the compositor gets fb's frames: VM links the
 * client's region to this one page by page, so the client sees exactly
 * this buffer and nothing next to it, and the pages stay the client's
 * for as long as it maps them, even if the buffer is freed here first
 * (and VM no longer panics when such a client forks: mem_shared.c).
 * What a client could reach is what its own handles name, through its
 * own address space on the GPU and its own mapping on the processor.
 *
 * Memory from VM is zeroed - but by the processor, through the kernel's
 * cacheable map of all RAM, and the GPU reads RAM, not the processor's
 * caches.  So every new buffer is cleaned to memory before its first
 * entry goes into a page table: otherwise a job could read what the
 * page's previous owner left in RAM.
 *
 * A heap grows on demand, as under Linux (PORTING-LOG, "Этап 11.6, куча"):
 * Mesa asks for 128 MiB of tiler heap per GL context and a frame uses a
 * few.  Creating one takes only its place in the address space; a
 * translation fault inside it brings in the 2 MiB piece that holds the
 * address - memory, cleaned, mapped - and the access that faulted goes on
 * (mali_mmu.c).  Memory counts against the limits as it comes, so the
 * limits are of memory, not of promises.
 *
 * The place in the GPU address space follows panfrost (drm_mm, best fit,
 * from 32 MiB to 4 GiB): a buffer that is not code is aligned to 2 MiB if
 * it is that big; code is aligned to its own size, so that it never
 * crosses a 16 MiB boundary (the program counter is 24 bits), and never
 * starts or ends on a 4 GiB one.  Checked against what panfrost gave on
 * the board for the same sequence (port/cb2-gpu/ref-api.txt).
 *
 * The processor's caches.  The GPU is not coherent, and the kernel maps
 * all RAM cacheable, so a client's mapping is cacheable too (two mappings
 * of a page with different attributes are undefined behaviour, which is
 * why Linux's write-combining mapping is not an option here).  So a
 * buffer a processor has touched is cleaned before each job that uses it
 * and cleaned and invalidated after: what the client wrote reaches the
 * GPU, what the GPU wrote reaches the client, no line the processor
 * fetched speculatively meanwhile hides it - and what the client wrote
 * while the job ran stays.  An invalidate alone would drop a dirty line,
 * and with it a write the client made to another part of the buffer
 * during the job (a ring it fills, a stream it maps unsynchronized: under
 * Linux the mapping is write-combining, and such writes are simply in
 * memory).  Only a line both wrote during the same job loses one side's
 * bytes - a race in the client wherever it runs.  A buffer no processor maps - a heap,
 * a render target never read back - costs nothing.
 */

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/sef.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mali.h"
#include "malireg.h"
#include "panfrost_drm.h"

#define SZ_2M		0x200000ull
#define SZ_16M		0x1000000ull
#define SZ_4G		0x100000000ull

/* The tokens MMAP_BO gives: above anything a GPU address can be. */
#define MMAP_OFFSET_BASE	(1ull << 40)

static size_t limit_total = 768u << 20;
static size_t limit_vm = 512u << 20;
static size_t total;

void
mali_bo_limits(size_t t, size_t v)
{
	limit_total = t;
	limit_vm = v;
}

size_t
mali_bo_total(void)
{
	return total;
}

/*
 * In pieces of 128 KiB, as fb does: the kernel does one request whole
 * under its big lock, and a 128 MiB heap in one piece would hold every
 * other core out of the kernel for tens of milliseconds.
 */
#define CACHE_PIECE	(128 * 1024)

static void
cache(int op, void *addr, size_t len)
{
	size_t n;
	int r;

	while (len > 0) {
		n = len < CACHE_PIECE ? len : CACHE_PIECE;
		if ((r = sys_cachectl(op, addr, n)) != OK) {
			log_warn(&mali_log, "cache maintenance of a buffer "
			    "failed: %d\n", r);
			return;
		}
		addr = (char *)addr + n;
		len -= n;
	}
}

/* ---- a place in the GPU address space -------------------------------- */

struct hole {
	uint64_t start, end;
	struct mali_bo *before;		/* the buffer after the hole, or NULL */
};

/* x before y: smaller first, and of equal ones the lower. */
static int
hole_before(const struct hole *x, const struct hole *y)
{
	uint64_t sx = x->end - x->start, sy = y->end - y->start;

	return sx != sy ? sx < sy : x->start < y->start;
}

/* By size (a service has no qsort; the holes are few). */
static void
holes_sort(struct hole *h, unsigned n)
{
	struct hole t;
	unsigned i, j;

	for (i = 1; i < n; i++) {
		t = h[i];
		for (j = i; j > 0 && hole_before(&t, &h[j - 1]); j--)
			h[j] = h[j - 1];
		h[j] = t;
	}
}

/*
 * Code may not start or end on a 4 GiB boundary, and not within 16 MiB
 * below one (panfrost's colour adjustment of drm_mm); end is exclusive.
 */
static void
exec_adjust(uint64_t *start, uint64_t *end)
{
	uint64_t next;

	if ((*start & (SZ_4G - 1)) == 0)
		*start += MALI_PAGE;
	if ((*end & (SZ_4G - 1)) == 0)
		*end -= MALI_PAGE;
	next = (*start + SZ_4G - 1) & ~(SZ_4G - 1);
	if (next - *start <= SZ_16M)
		*start = next + MALI_PAGE;
	if (*end > ((*start + SZ_4G - 1) & ~(SZ_4G - 1)) - MALI_PAGE)
		*end = ((*start + SZ_4G - 1) & ~(SZ_4G - 1)) - MALI_PAGE;
}

/*
 * The smallest hole that takes size bytes, aligned as panfrost aligns;
 * equal holes lowest first.  The address goes to *va and the buffer it
 * is to go before to *before (NULL: at the end).
 */
static int
va_alloc(struct mali_vm *vm, size_t size, uint32_t flags, uint64_t *va,
	struct mali_bo **before)
{
	struct hole *holes;
	struct mali_bo *bo;
	uint64_t prev, s, e, align, rem;
	unsigned n = 0, max = 1, i;

	TAILQ_FOREACH(bo, &vm->bos, link)
		max++;
	if ((holes = malloc(max * sizeof(*holes))) == NULL)
		return ENOMEM;
	prev = MALI_VA_START;
	TAILQ_FOREACH(bo, &vm->bos, link) {
		if (bo->va > prev) {
			holes[n].start = prev;
			holes[n].end = bo->va;
			holes[n++].before = bo;
		}
		prev = bo->va + bo->size;
	}
	if (MALI_VA_END > prev) {
		holes[n].start = prev;
		holes[n].end = MALI_VA_END;
		holes[n++].before = NULL;
	}
	holes_sort(holes, n);

	if (!(flags & PANFROST_BO_NOEXEC))
		align = size;
	else
		align = size >= SZ_2M ? SZ_2M : 0;

	for (i = 0; i < n; i++) {
		s = holes[i].start;
		e = holes[i].end;
		if (e - s < size)
			continue;
		if (!(flags & PANFROST_BO_NOEXEC))
			exec_adjust(&s, &e);
		if (e <= s || e - s < size)
			continue;
		if (align != 0 && (rem = s % align) != 0) {
			s += align - rem;
			if (e <= s || e - s < size)
				continue;
		}
		*va = s;
		*before = holes[i].before;
		free(holes);
		return OK;
	}
	free(holes);
	return ENOSPC;
}

/* ---- memory ----------------------------------------------------------- */

/* The device's word when a buffer's memory goes (mali_drm.c: its fence). */
void (*mali_mem_gone)(struct mali_mem *mem);

static void
mem_destroy(struct mali_mem *mem)
{
	if (mali_mem_gone != NULL)
		mali_mem_gone(mem);
	if (mem->cpu != NULL && mem->cpu != MAP_FAILED)
		munmap(mem->cpu, mem->size);
	if (!(mem->flags & PANFROST_BO_HEAP))
		total -= mem->size;
	free(mem->pages);
	free(mem);
}

void
mali_mem_ref(struct mali_mem *mem)
{
	mem->refs++;
}

void
mali_mem_unref(struct mali_mem *mem)
{
	if (--mem->refs == 0)
		mem_destroy(mem);
}

/* ---- buffers ---------------------------------------------------------- */

static void
bo_destroy(struct mali_bo *bo)
{
	size_t npages = bo->size / MALI_PAGE, i;

	/*
	 * Out of the GPU's tables first, and out of the MMU's caches: the
	 * pages go back to VM next (if this was the memory's last
	 * placement), and the GPU must not reach them after.
	 */
	if (bo->va != 0) {
		mali_vm_unmap(bo->vm, bo->va, npages);
		TAILQ_REMOVE(&bo->vm->bos, bo, link);
	}
	if (bo->pieces != NULL)
		for (i = 0; i < bo->size / MALI_HEAP_PIECE; i++)
			if (bo->pieces[i] != NULL)
				munmap(bo->pieces[i], MALI_HEAP_PIECE);
	bo->vm->mem -= bo->backed;
	if (bo->flags & PANFROST_BO_HEAP)
		total -= bo->backed;
	free(bo->pieces);
	if (bo->mem != NULL)
		mali_mem_unref(bo->mem);
	free(bo);
}

/*
 * Place bo in its address space: an address as panfrost picks one, and
 * the memory's pages in the tables - a heap's come later, by its faults.
 */
static int
place(struct mali_bo *bo)
{
	struct mali_bo *before;
	uint64_t va, prot;
	int r;

	if ((r = va_alloc(bo->vm, bo->size, bo->flags, &va, &before)) != OK)
		return r;
	if (!(bo->flags & PANFROST_BO_HEAP)) {
		prot = MALI_PTE_READ | MALI_PTE_WRITE;
		if (bo->flags & PANFROST_BO_NOEXEC)
			prot |= MALI_PTE_XN;
		if ((r = mali_vm_map(bo->vm, va, bo->mem->pages,
		    bo->size / MALI_PAGE, prot)) != OK)
			return r;
	}
	bo->va = va;
	if (before != NULL)
		TAILQ_INSERT_BEFORE(before, bo, link);
	else
		TAILQ_INSERT_TAIL(&bo->vm->bos, bo, link);
	bo->mmap_offset = MMAP_OFFSET_BASE + va;
	return OK;
}

/* A placement of mem in vm, not yet placed; mem gains a reference. */
static struct mali_bo *
bo_alloc(struct mali_vm *vm, struct mali_mem *mem)
{
	struct mali_bo *bo;

	if ((bo = calloc(1, sizeof(*bo))) == NULL)
		return NULL;
	bo->vm = vm;
	bo->mem = mem;
	mem->refs++;
	bo->flags = mem->flags;
	bo->size = mem->size;
	bo->refs = 1;
	return bo;
}

/*
 * A new buffer of size bytes in vm, with PANFROST_BO_* flags: its memory,
 * and its placement on the GPU.  The caller checks the flags.  A heap is
 * placed and left empty: its memory comes as the GPU faults on it.
 */
struct mali_bo *
mali_bo_new(struct mali_vm *vm, size_t size, uint32_t flags, int *err)
{
	struct mali_mem *mem;
	struct mali_bo *bo;
	size_t npages, k;
	int r;

	if (flags & PANFROST_BO_HEAP)
		size = (size + SZ_2M - 1) & ~(SZ_2M - 1);
	else
		size = (size + MALI_PAGE - 1) & ~((size_t)MALI_PAGE - 1);
	if (size == 0 || size > MALI_VA_END - MALI_VA_START) {
		*err = EINVAL;
		return NULL;
	}
	if (!(flags & PANFROST_BO_HEAP) && (total + size > limit_total ||
	    vm->mem + size > limit_vm)) {
		*err = ENOMEM;
		return NULL;
	}
	if ((mem = calloc(1, sizeof(*mem))) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	mem->flags = flags;
	mem->size = size;
	mem->minor = -1;
	if (!(flags & PANFROST_BO_HEAP))
		total += size;
	if ((bo = bo_alloc(vm, mem)) == NULL) {
		mem_destroy(mem);
		*err = ENOMEM;
		return NULL;
	}

	if (flags & PANFROST_BO_HEAP) {
		if ((bo->pieces = calloc(size / MALI_HEAP_PIECE,
		    sizeof(*bo->pieces))) == NULL) {
			bo_destroy(bo);
			*err = ENOMEM;
			return NULL;
		}
	} else {
		bo->backed = size;
		vm->mem += size;
		npages = size / MALI_PAGE;
		if ((mem->pages = calloc(npages, sizeof(*mem->pages))) ==
		    NULL) {
			bo_destroy(bo);
			*err = ENOMEM;
			return NULL;
		}
		mem->cpu = mmap(NULL, size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC, -1, 0);
		if (mem->cpu == MAP_FAILED) {
			mem->cpu = NULL;
			bo_destroy(bo);
			*err = ENOMEM;
			return NULL;
		}
		for (k = 0; k < npages; k++) {
			if ((r = sys_umap(SELF, VM_D, (vir_bytes)mem->cpu +
			    k * MALI_PAGE, MALI_PAGE, &mem->pages[k])) != OK) {
				log_warn(&mali_log, "no physical address for "
				    "page %zu of a buffer: %d\n", k, r);
				bo_destroy(bo);
				*err = ENOMEM;
				return NULL;
			}
		}
		/* VM's zeros to RAM, before the GPU can read the pages. */
		cache(CACHE_CLEAN_INVALIDATE, mem->cpu, size);
	}

	if ((r = place(bo)) != OK) {
		bo_destroy(bo);
		*err = r;
		return NULL;
	}
	return bo;
}

/*
 * The memory of another context's buffer placed in vm too (PRIME): the
 * same pages at an address of vm's own, chosen as for a buffer made here,
 * with the flags it was made with - as panfrost maps a GEM object into
 * each file that has a handle to it.  It counts against vm's limit: what
 * a context holds placed is what it can keep alive.  A heap never comes
 * here (it is never exported).
 */
struct mali_bo *
mali_bo_import(struct mali_vm *vm, struct mali_mem *mem, int *err)
{
	struct mali_bo *bo;
	int r;

	if (mem->flags & PANFROST_BO_HEAP) {
		*err = EINVAL;
		return NULL;
	}
	if (vm->mem + mem->size > limit_vm) {
		*err = ENOMEM;
		return NULL;
	}
	if ((bo = bo_alloc(vm, mem)) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	bo->backed = mem->size;
	vm->mem += mem->size;
	if ((r = place(bo)) != OK) {
		bo_destroy(bo);
		*err = r;
		return NULL;
	}
	return bo;
}

void
mali_bo_ref(struct mali_bo *bo)
{
	bo->refs++;
}

void
mali_bo_unref(struct mali_bo *bo)
{
	if (--bo->refs == 0)
		bo_destroy(bo);
}

/* The buffer containing GPU address va in vm, or NULL. */
struct mali_bo *
mali_bo_at(struct mali_vm *vm, uint64_t va)
{
	struct mali_bo *bo;

	TAILQ_FOREACH(bo, &vm->bos, link)
		if (va >= bo->va && va < bo->va + bo->size)
			return bo;
	return NULL;
}

/*
 * The heap bo grows by the 2 MiB piece that holds GPU address va: memory,
 * cleaned to RAM, into the tables - panfrost's panfrost_mmu_map_fault_addr().
 * Called from the MMU line, the access that faulted waiting; mapping the
 * piece flushes the MMU's caches of it (mali_vm_map()), and that command is
 * what lets the access go on.  *piece is where the piece starts.  OK if the
 * piece is there now - EEXIST if it was before, which the caller answers
 * with a flush of its own; ENOMEM past the limits or without memory.
 */
int
mali_bo_grow(struct mali_bo *bo, uint64_t va, uint64_t *piece)
{
	static phys_bytes pages[MALI_HEAP_PIECE / MALI_PAGE];
	size_t i = (size_t)((va - bo->va) / MALI_HEAP_PIECE), k;
	void *p;
	int r;

	if (!(bo->flags & PANFROST_BO_HEAP) || va < bo->va ||
	    va >= bo->va + bo->size)
		return EINVAL;
	*piece = bo->va + (uint64_t)i * MALI_HEAP_PIECE;
	if (bo->pieces[i] != NULL)
		return EEXIST;
	if (total + MALI_HEAP_PIECE > limit_total ||
	    bo->vm->mem + MALI_HEAP_PIECE > limit_vm)
		return ENOMEM;

	p = mmap(NULL, MALI_HEAP_PIECE, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC, -1, 0);
	if (p == MAP_FAILED)
		return ENOMEM;
	for (k = 0; k < MALI_HEAP_PIECE / MALI_PAGE; k++) {
		if ((r = sys_umap(SELF, VM_D, (vir_bytes)p + k * MALI_PAGE,
		    MALI_PAGE, &pages[k])) != OK) {
			log_warn(&mali_log, "no physical address for page %zu "
			    "of a heap piece: %d\n", k, r);
			munmap(p, MALI_HEAP_PIECE);
			return ENOMEM;
		}
	}
	/* VM's zeros to RAM, before the GPU can read the pages. */
	cache(CACHE_CLEAN_INVALIDATE, p, MALI_HEAP_PIECE);

	/* Never code, as panfrost maps it: read, write, no execute. */
	if ((r = mali_vm_map(bo->vm, *piece, pages, MALI_HEAP_PIECE / MALI_PAGE,
	    MALI_PTE_READ | MALI_PTE_WRITE | MALI_PTE_XN)) != OK) {
		munmap(p, MALI_HEAP_PIECE);
		return r;
	}
	bo->pieces[i] = p;
	bo->backed += MALI_HEAP_PIECE;
	bo->vm->mem += MALI_HEAP_PIECE;
	total += MALI_HEAP_PIECE;
	return OK;
}

/* Before a job: what a processor wrote, to memory. */
void
mali_bo_for_gpu(struct mali_bo *bo)
{
	struct mali_mem *mem = bo->mem;

	if (mem->cpu_access)
		cache(CACHE_CLEAN_INVALIDATE, mem->cpu, mem->size);
}

/*
 * After a job: what the GPU wrote, to a processor that looks - and what
 * the processor wrote meanwhile, to memory first (see above).
 */
void
mali_bo_for_cpu(struct mali_bo *bo)
{
	struct mali_mem *mem = bo->mem;

	if (mem->cpu_access)
		cache(CACHE_CLEAN_INVALIDATE, mem->cpu, mem->size); /* not INVALIDATE */
}

/*
 * Map the memory into process who, at addr if not NULL: VM links who's
 * new region to this service's pages (vm_remap), so who sees the very
 * pages the GPU does.  The whole buffer: VM lends a region whole or not
 * at all.  From now on a processor may touch it.
 */
int
mali_mem_map_into(struct mali_mem *mem, endpoint_t who, void *addr,
	int writable, void **where)
{
	void *v;

	if (mem->cpu == NULL)
		return EINVAL;		/* a heap's is nobody's to map */
	/*
	 * The source by its own endpoint, not SELF: VM checks both ends with
	 * vm_isokendpt(), and SELF is no endpoint there - the call fails
	 * without a word (the board said ENOMEM to the first mmap).
	 */
	if (writable)
		v = vm_remap(who, sef_self(), addr, mem->cpu, mem->size);
	else
		v = vm_remap_ro(who, sef_self(), addr, mem->cpu, mem->size);
	if (v == MAP_FAILED)
		return ENOMEM;
	/*
	 * The GPU may have written the buffer already, and the processor may
	 * hold lines of it it fetched before; unless a job is about to end
	 * (which invalidates), drop them now.
	 */
	if (!mem->cpu_access && mem->busy == 0)
		cache(CACHE_INVALIDATE, mem->cpu, mem->size);
	mem->cpu_access = 1;
	*where = v;
	return OK;
}

int
mali_bo_map_into(struct mali_bo *bo, endpoint_t who, void *addr,
	int writable, void **where)
{
	return mali_mem_map_into(bo->mem, who, addr, writable, where);
}
