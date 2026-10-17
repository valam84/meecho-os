/*
 * Buffers' memory: what the GPU and the processor share (minix/drmdriver.h).
 *
 * Where the memory comes from, and why so (PORTING-LOG, "Этап 11.6").
 * The service allocates it - anonymous memory of its own, one mmap(2)
 * region per buffer, every page present (MAP_PREALLOC) - and learns the
 * physical address of each page with sys_umap(), so that no buffer, a
 * 128 MiB tiler heap included, needs contiguous RAM: the GPU's MMU (or the
 * SMMU in front of it) puts the pages together.  The client gets the same
 * pages through vm_remap(), the way the compositor gets fb's frames: VM
 * links the client's region to this one page by page, so the client sees
 * exactly this buffer and nothing next to it, and the pages stay the
 * client's for as long as it maps them, even if the buffer is freed here
 * first (and VM no longer panics when such a client forks: mem_shared.c).
 *
 * Memory from VM is zeroed - but by the processor, through the kernel's
 * cacheable map of all RAM, and the GPU reads RAM, not the processor's
 * caches.  So every new buffer is cleaned to memory before a driver can
 * put its pages in a GPU's tables: otherwise a job could read what the
 * page's previous owner left in RAM.
 *
 * The processor's caches.  Neither GPU here is coherent, and the kernel
 * maps all RAM cacheable, so a client's mapping is cacheable too (two
 * mappings of a page with different attributes are undefined behaviour,
 * which is why Linux's write-combining mapping is not an option here).  So
 * a buffer a processor has touched is cleaned before each job that uses it
 * and cleaned and invalidated after: what the client wrote reaches the
 * GPU, what the GPU wrote reaches the client, no line the processor
 * fetched speculatively meanwhile hides it - and what the client wrote
 * while the job ran stays.  An invalidate alone would drop a dirty line,
 * and with it a write the client made to another part of the buffer
 * during the job (a ring it fills, a stream it maps unsynchronized: under
 * Linux the mapping is write-combining, and such writes are simply in
 * memory).  Only a line both wrote during the same job loses one side's
 * bytes - a race in the client wherever it runs.  A buffer no processor
 * maps - a heap, a render target never read back - costs nothing.
 *
 * Moved here from the mali service (mali_bo.c, stage 11.6).
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

#include "drm_priv.h"

#define PAGE		4096u

static size_t limit_total = 768u << 20;
static size_t total;

void
drmdrv_mem_limit(size_t t)
{
	limit_total = t;
}

size_t
drmdrv_mem_total(void)
{
	return total;
}

/* May bytes more be given out? */
int
drmdrv_mem_room(size_t bytes)
{
	return total + bytes <= limit_total;
}

/* Memory of the driver's own making that counts (a heap's piece). */
void
drmdrv_mem_charge(size_t bytes)
{
	total += bytes;
}

void
drmdrv_mem_discharge(size_t bytes)
{
	total -= bytes;
}

/*
 * In pieces of 128 KiB, as fb does: the kernel does one request whole
 * under its big lock, and a 128 MiB heap in one piece would hold every
 * other core out of the kernel for tens of milliseconds.
 */
#define CACHE_PIECE	(128 * 1024)

void
drmdrv_cache(int op, void *addr, size_t len)
{
	size_t n;
	int r;

	while (len > 0) {
		n = len < CACHE_PIECE ? len : CACHE_PIECE;
		if ((r = sys_cachectl(op, addr, n)) != OK) {
			drm_warn("cache maintenance of a buffer failed: %d\n",
			    r);
			return;
		}
		addr = (char *)addr + n;
		len -= n;
	}
}

/*
 * size bytes of the service's own memory, every page present: where it is
 * mapped here in *cpu, each page's physical address in pages[], and VM's
 * zeros cleaned to RAM, before a GPU can read them.
 */
int
drmdrv_pages_new(size_t size, void **cpu, phys_bytes *pages)
{
	size_t k;
	void *p;
	int r;

	p = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC, -1, 0);
	if (p == MAP_FAILED)
		return ENOMEM;
	for (k = 0; k < size / PAGE; k++) {
		if ((r = sys_umap(SELF, VM_D, (vir_bytes)p + k * PAGE,
		    PAGE, &pages[k])) != OK) {
			drm_warn("no physical address for page %zu of a "
			    "buffer: %d\n", k, r);
			munmap(p, size);
			return ENOMEM;
		}
	}
	/* VM's zeros to RAM, before the GPU can read the pages. */
	drmdrv_cache(CACHE_CLEAN_INVALIDATE, p, size);
	*cpu = p;
	return OK;
}

void
drmdrv_pages_free(void *cpu, size_t size)
{
	munmap(cpu, size);
}

static void
mem_destroy(struct drmdrv_mem *mem)
{
	drm_mem_unhook(mem);
	if (mem->cpu != NULL && mem->cpu != MAP_FAILED)
		munmap(mem->cpu, mem->size);
	total -= mem->counted;
	free(mem->pages);
	free(mem);
}

void
drmdrv_mem_ref(struct drmdrv_mem *mem)
{
	mem->refs++;
}

void
drmdrv_mem_unref(struct drmdrv_mem *mem)
{
	if (--mem->refs == 0)
		mem_destroy(mem);
}

static struct drmdrv_mem *
mem_alloc(size_t size, uint32_t flags)
{
	struct drmdrv_mem *mem;

	if ((mem = calloc(1, sizeof(*mem))) == NULL)
		return NULL;
	mem->flags = flags;
	mem->size = size;
	mem->minor = -1;
	mem->refs = 1;
	return mem;
}

/*
 * size bytes (a whole number of pages) of new memory, its pages present
 * and cleaned, counted in the total; ENOMEM past the limit.  The caller
 * holds the one reference.
 */
struct drmdrv_mem *
drmdrv_mem_new(size_t size, uint32_t flags, int *err)
{
	struct drmdrv_mem *mem;
	int r;

	if (size == 0 || (size & (PAGE - 1))) {
		*err = EINVAL;
		return NULL;
	}
	if (!drmdrv_mem_room(size) || (mem = mem_alloc(size, flags)) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	mem->counted = size;
	total += size;
	if ((mem->pages = calloc(size / PAGE, sizeof(*mem->pages))) == NULL ||
	    (r = drmdrv_pages_new(size, &mem->cpu, mem->pages)) != OK) {
		mem_destroy(mem);
		*err = ENOMEM;
		return NULL;
	}
	return mem;
}

/*
 * Memory with no pages of its own: a driver brings them in pieces (a
 * heap that grows by the GPU's faults) and counts them itself.  Nobody's
 * to map.
 */
struct drmdrv_mem *
drmdrv_mem_new_unbacked(size_t size, uint32_t flags, int *err)
{
	struct drmdrv_mem *mem;

	if ((mem = mem_alloc(size, flags)) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	return mem;
}

/*
 * Memory that is not this service's: size bytes of a device's physically
 * contiguous memory from phys (the frame buffer driver's scanout buffers -
 * the driver checks which).  It is not counted in the limits, which are
 * about what the service gives out, and never freed here - its owner keeps
 * it for as long as it runs.
 *
 * The owner's region of it, src_vir in src_ep, is what a client that maps
 * the buffer (mmap(2) of its dma-buf: Mesa reading a frame back, a
 * screenshot) is mapped from - not this service's view of it, since VM
 * shares a region of the process that has it, not a share of a share.
 * This service maps it too, for the processor's caches around jobs, as
 * for any buffer a processor reads (cpu_access): the region is shared, so
 * its pages are touched once to be in the table that sys_cachectl reads.
 * Without that view the buffer is still the GPU's, only not a client's to
 * map.
 */
struct drmdrv_mem *
drmdrv_mem_new_phys(phys_bytes phys, endpoint_t src_ep, vir_bytes src_vir,
	size_t size, uint32_t flags, int *err)
{
	volatile const char *p;
	struct drmdrv_mem *mem;
	size_t npages, k;
	void *v;

	if (size == 0 || (size & (PAGE - 1)) || (phys & (PAGE - 1))) {
		*err = EINVAL;
		return NULL;
	}
	if ((mem = mem_alloc(size, flags)) == NULL) {
		*err = ENOMEM;
		return NULL;
	}
	mem->foreign = 1;
	mem->src_ep = src_ep;
	mem->src_vir = src_vir;
	npages = size / PAGE;
	if ((mem->pages = calloc(npages, sizeof(*mem->pages))) == NULL) {
		mem_destroy(mem);
		*err = ENOMEM;
		return NULL;
	}
	for (k = 0; k < npages; k++)
		mem->pages[k] = phys + k * PAGE;
	/* Both ends by endpoint: SELF is none to VM (drmdrv_mem_map_into()). */
	v = vm_remap(sef_self(), src_ep, NULL, (void *)src_vir, size);
	if (v != MAP_FAILED) {
		for (p = v, k = 0; k < npages; k++)
			(void)p[k * PAGE];
		mem->cpu = v;
	} else
		drm_warn("no view of a scanout buffer (%d, 0x%lx): the GPU's, "
		    "not a client's to map\n", src_ep, (unsigned long)src_vir);
	return mem;
}

/* Before a job: what a processor wrote, to memory. */
void
drmdrv_mem_for_gpu(struct drmdrv_mem *mem)
{
	if (mem->cpu_access)
		drmdrv_cache(CACHE_CLEAN_INVALIDATE, mem->cpu, mem->size);
}

/*
 * After a job: what the GPU wrote, to a processor that looks - and what
 * the processor wrote meanwhile, to memory first (see above).
 */
void
drmdrv_mem_for_cpu(struct drmdrv_mem *mem)
{
	if (mem->cpu_access)
		drmdrv_cache(CACHE_CLEAN_INVALIDATE, mem->cpu, mem->size); /* not INVALIDATE */
}

/*
 * Map the memory into process who, at addr if not NULL: VM links who's
 * new region to this service's pages (vm_remap), so who sees the very
 * pages the GPU does.  The whole buffer: VM lends a region whole or not
 * at all.  From now on a processor may touch it.
 */
int
drmdrv_mem_map_into(struct drmdrv_mem *mem, endpoint_t who, void *addr,
	int writable, void **where)
{
	endpoint_t src_ep;
	void *v, *src;

	if (mem->cpu == NULL)
		return EINVAL;		/* a heap's is nobody's to map */
	/*
	 * The source by its own endpoint, not SELF: VM checks both ends with
	 * vm_isokendpt(), and SELF is no endpoint there - the call fails
	 * without a word (the board said ENOMEM to the first mmap).  A
	 * device's memory comes from its owner's region
	 * (drmdrv_mem_new_phys()).
	 */
	src_ep = mem->foreign ? mem->src_ep : sef_self();
	src = mem->foreign ? (void *)mem->src_vir : mem->cpu;
	if (writable)
		v = vm_remap(who, src_ep, addr, src, mem->size);
	else
		v = vm_remap_ro(who, src_ep, addr, src, mem->size);
	if (v == MAP_FAILED)
		return ENOMEM;
	/*
	 * The GPU may have written the buffer already, and the processor may
	 * hold lines of it it fetched before; unless a job is about to end
	 * (which invalidates), drop them now.
	 */
	if (!mem->cpu_access && mem->busy == 0)
		drmdrv_cache(CACHE_INVALIDATE, mem->cpu, mem->size);
	mem->cpu_access = 1;
	*where = v;
	return OK;
}
