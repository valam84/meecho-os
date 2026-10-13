
/* This file implements the methods of shared memory.  */

#include <assert.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"

/* These functions are static so as to not pollute the
 * global namespace, and are accessed through their function
 * pointers.
 */

static int shared_unreference(struct phys_region *pr);
static int shared_pagefault(struct vmproc *vmp, struct vir_region *region, 
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io);
static int shared_sanitycheck(struct phys_region *pr, const char *file, int line);
static int shared_writable(struct phys_region *pr);
static void shared_delete(struct vir_region *region);
static u32_t shared_regionid(struct vir_region *region);
static int shared_copy(struct vir_region *vr, struct vir_region *newvr);
static int shared_refcount(struct vir_region *vr);
static int shared_pt_flags(struct vir_region *vr);

struct mem_type mem_type_shared = {
	.name = "shared memory",
	.ev_copy = shared_copy,
	.ev_unreference = shared_unreference,
	.ev_pagefault = shared_pagefault,
	.ev_sanitycheck = shared_sanitycheck,
	.ev_delete = shared_delete,
	.regionid = shared_regionid,
	.refcount = shared_refcount,
	.writable = shared_writable,
	.pt_flags = shared_pt_flags,
};

static int shared_pt_flags(struct vir_region *vr){
#if defined(__arm__)
	return ARM_VM_PTE_CACHED;
#elif defined(__aarch64__)
	return AARCH64_VM_PTE_CACHED;
#else
	return 0;
#endif
}

static int shared_unreference(struct phys_region *pr)
{
	return mem_type_anon.ev_unreference(pr);
}

static int getsrc(struct vir_region *region,
	struct vmproc **vmp, struct vir_region **r)
{
	int srcproc;

	if(region->def_memtype != &mem_type_shared) {
		printf("shared region hasn't shared type but %s.\n",
			region->def_memtype->name);
		return EINVAL;
	}

	if(!region->param.shared.ep || !region->param.shared.vaddr) {
		printf("shared region has not defined source region.\n");
		util_stacktrace();
		return EINVAL;
	}

	/*
	 * The source may be gone, and that is no fault of VM's: its process
	 * exited or was restarted (a driver), or it unmapped the region (a
	 * GPU buffer freed while a client still maps it, stage 11.6).  The
	 * pages this region has linked already stay its own; the others can
	 * not be found any more, which a page fault turns into SIGSEGV.  No
	 * console line for it: any process can ask for one as often as it
	 * likes.  A region at the same address that is not the source is the
	 * same thing (the id below), and so is quiet too.
	 */
        if(vm_isokendpt((endpoint_t) region->param.shared.ep, &srcproc) != OK)
                return EINVAL;

	*vmp = &vmproc[srcproc];

	if(!(*r=map_lookup(*vmp, region->param.shared.vaddr, NULL)))
                return EINVAL;

	if(region->param.shared.id != (*r)->id)
		return EINVAL;

	/*
	 * Anonymous memory, or physically contiguous anonymous memory: the
	 * frame buffers of the display driver are the second kind (the
	 * display controller fetches them by physical address), and the
	 * compositor maps them through here (stage 11.4).  Both keep every
	 * page present for as long as the region lives, which is all a
	 * shared mapping needs of its source; contiguous memory is never
	 * paged in, so physblock_get() always finds it.
	 */
	if((*r)->def_memtype != &mem_type_anon &&
	    (*r)->def_memtype != &mem_type_anon_contig) {
		printf("source region hasn't anon type but %s.\n",
			(*r)->def_memtype->name);
		return EINVAL;
	}

	return OK;
}

static u32_t shared_regionid(struct vir_region *vr)
{
	struct vir_region *src_region;
	struct vmproc *src_vmp;

	if(getsrc(vr, &src_vmp, &src_region) != OK)
		return 0;

	return src_region->id;
}

static void shared_delete(struct vir_region *region)
{
	struct vir_region *src_region;
	struct vmproc *src_vmp;

	if(getsrc(region, &src_vmp, &src_region) != OK)
		return;

	assert(src_region->remaps > 0);
	src_region->remaps--;
}

static int shared_pagefault(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb,
	void *state, int statelen, int *io)
{
	struct vir_region *src_region;
	struct vmproc *src_vmp;
	struct phys_region *pr;

	if(getsrc(region, &src_vmp, &src_region) != OK) {
		return EINVAL;
	}

	if(ph->ph->phys != MAP_NONE) {
		/* memory is there - pagefault does not need handling */
		return OK;
	}

	pb_free(ph->ph);

	if(!(pr = physblock_get(src_region, ph->offset))) {
		int r;
		if((r=map_pf(src_vmp, src_region, ph->offset, write,
			NULL, NULL, 0, io)) != OK)
			return r;
		if(!(pr = physblock_get(src_region, ph->offset))) {
			panic("missing region after pagefault handling");
		}
	}

	pb_link(ph, pr->ph, ph->offset, region);

	return OK;
}

static int shared_sanitycheck(struct phys_region *pr, const char *file, int line)
{
	return OK;
}

static int shared_writable(struct phys_region *pr)
{
	assert(pr->ph->refcount > 0);
	return pr->ph->phys != MAP_NONE;
}

void shared_setsource(struct vir_region *vr, endpoint_t ep,
	struct vir_region *src_vr)
{
	struct vmproc *vmp;
	struct vir_region *srcvr;
	int id = src_vr->id;
	vir_bytes vaddr = src_vr->vaddr;

	assert(vr->def_memtype == &mem_type_shared);

	if(!ep || !vaddr || !id) {
		printf("VM: shared_setsource: zero ep/vaddr/id - ignoring\n");
		return;
	}

	vr->param.shared.ep = ep;
	vr->param.shared.vaddr = vaddr;
	vr->param.shared.id = id;

	if(getsrc(vr, &vmp, &srcvr) != OK)
		panic("initial getsrc failed");

	assert(srcvr == src_vr);

	srcvr->remaps++;
}

static int shared_copy(struct vir_region *vr, struct vir_region *newvr)
{
	struct vmproc *vmp;
	struct vir_region *srcvr;

	/*
	 * The source gone (see getsrc()): the copy is as blind as the
	 * original.  The pages the original has, the copy has too -
	 * map_copy_region() links them after this - and the others fault in
	 * both.  This was a panic of VM, and any process could ask for it:
	 * map a buffer a driver lends (fb's frames, a GPU buffer), let the
	 * driver free it or restart, fork.
	 */
	if(getsrc(vr, &vmp, &srcvr) != OK) {
		newvr->param.shared = vr->param.shared;
		return OK;
	}

	shared_setsource(newvr, vr->param.shared.ep, srcvr);

	return OK;
}

static int shared_refcount(struct vir_region *vr)
{
	return 1 + vr->remaps;
}

