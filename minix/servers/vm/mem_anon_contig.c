
/* This file implements the methods of physically contiguous anonymous memory. */

#include <assert.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"

static int anon_contig_reference(struct phys_region *, struct phys_region *);
static int anon_contig_unreference(struct phys_region *pr);
static int anon_contig_pagefault(struct vmproc *vmp, struct vir_region *region, 
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io);
static int anon_contig_sanitycheck(struct phys_region *pr, const char *file, int line);
static int anon_contig_writable(struct phys_region *pr);
static void anon_contig_split(struct vmproc *vmp, struct vir_region *vr,
                        struct vir_region *r1, struct vir_region *r2);
static int anon_contig_resize(struct vmproc *vmp, struct vir_region *vr, vir_bytes l);
static int anon_contig_new(struct vir_region *vr);
static int anon_contig_pt_flags(struct vir_region *vr);

struct mem_type mem_type_anon_contig = {
	.name = "anonymous memory (physically contiguous)",
	.ev_new = anon_contig_new,
	.ev_reference = anon_contig_reference,
	.ev_unreference = anon_contig_unreference,
	.ev_pagefault = anon_contig_pagefault,
	.ev_resize = anon_contig_resize,
	.ev_split = anon_contig_split,
	.ev_sanitycheck = anon_contig_sanitycheck,
	.writable = anon_contig_writable,
	.pt_flags = anon_contig_pt_flags,
};

/*
 * Contiguous memory is what drivers ask for when a device is going to read
 * or write it, and the two ARM ports answer differently.
 *
 * earm maps it as Device memory - uncached - so that a driver written
 * without any cache maintenance sees what the device wrote and the device
 * sees what the driver wrote.  That works only as long as nothing else maps
 * the same physical page cacheably, and something always does: the kernel
 * has all of RAM in its linear map, and this server writes zeroes into every
 * page it hands out through its own mapping of it.  Two mappings of one
 * page with different memory types is behaviour the architecture leaves
 * undefined, and on the CB2 it was not undefined for long.  Dirty lines
 * left by the zeroing sat in the cache, the driver's Device-mapped writes
 * went straight past them to memory, and the first cache clean the driver
 * asked for - meant to publish its descriptors - wrote the stale zeroes
 * back on top of them.  Descriptor rings the device saw as empty, at
 * random, depending on which lines had been evicted in between.
 *
 * So on aarch64 this memory is Normal cacheable like everything else, and
 * keeping it coherent with a device is the driver's job, done explicitly
 * with sys_cachectl(2).  One memory type for one page; the cache does not
 * have two opinions about what the memory holds.
 */
static int anon_contig_pt_flags(struct vir_region *vr){
#if defined(__arm__)
	return  ARM_VM_PTE_DEVICE;
#elif defined(__aarch64__)
	return  AARCH64_VM_PTE_CACHED;
#else
	return  0;
#endif
}

static int anon_contig_pagefault(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io)
{
	panic("anon_contig_pagefault: pagefault cannot happen");
}

static int anon_contig_new(struct vir_region *region)
{
        u32_t allocflags;
	phys_bytes new_pages, new_page_cl, cur_ph;
	phys_bytes p, pages;

        allocflags = vrallocflags(region->flags);

	pages = region->length/VM_PAGE_SIZE;

	assert(physregions(region) == 0);

	for(p = 0; p < pages; p++) {
		struct phys_block *pb = pb_new(MAP_NONE);
		struct phys_region *pr = NULL;
		if(pb)
			pr = pb_reference(pb, p * VM_PAGE_SIZE, region, &mem_type_anon_contig);
		if(!pr) {
			if(pb) pb_free(pb);
			map_free(region);
			return ENOMEM;
		}
	}

	assert(physregions(region) == pages);

	if((new_page_cl = alloc_mem(pages, allocflags)) == NO_MEM) {
		map_free(region);
		return ENOMEM;
	}

	cur_ph = new_pages = CLICK2ABS(new_page_cl);

	for(p = 0; p < pages; p++) {
		struct phys_region *pr = physblock_get(region, p * VM_PAGE_SIZE);
		assert(pr);
		assert(pr->ph);
		assert(pr->ph->phys == MAP_NONE);
		assert(pr->offset == p * VM_PAGE_SIZE);
		pr->ph->phys = cur_ph + pr->offset;
	}

	return OK;
}

static int anon_contig_resize(struct vmproc *vmp, struct vir_region *vr, vir_bytes l)
{
	printf("VM: cannot resize physically contiguous memory.\n");
	return ENOMEM;
}

static int anon_contig_reference(struct phys_region *pr,
	struct phys_region *newpr)
{
	printf("VM: cannot fork with physically contig memory.\n");
	return ENOMEM;
}

/* Methods inherited from the anonymous memory methods. */

static int anon_contig_unreference(struct phys_region *pr)
{
	return mem_type_anon.ev_unreference(pr);
}

static int anon_contig_sanitycheck(struct phys_region *pr, const char *file, int line)
{
	return mem_type_anon.ev_sanitycheck(pr, file, line);
}

static int anon_contig_writable(struct phys_region *pr)
{
	return mem_type_anon.writable(pr);
}

static void anon_contig_split(struct vmproc *vmp, struct vir_region *vr,
                        struct vir_region *r1, struct vir_region *r2)
{
	return;
}

