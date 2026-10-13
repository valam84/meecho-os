/* Memory objects: the pages behind memfd_create(2).
 *
 * A memory object is a set of pages that belongs to no process: a client
 * draws into it, the compositor reads it, each through a mapping of its
 * own, and both see the same bytes because both map the same pages.  That
 * is the one thing no other mapping on this system does - anonymous
 * memory is private, a file mapping is read-only or copy-on-write, and
 * mem_shared.c borrows the pages of a region of one particular process
 * and dies with it.
 *
 * VFS gives the object its descriptor (vfs/memfd.c) and PFS keeps its size;
 * VM keeps the pages, here.  The object is named by the inode number PFS
 * gave it, which VFS sends in place of a file's in the reply to an mmap
 * lookup, marked by VMV_DEV_MEMFD in place of a device.
 *
 * Lifetime.  A mapping holds VM's own descriptor of the object (fdref.c,
 * as for any mapped file), so while anything maps it, VFS's vnode lives;
 * and VFS tells this file (VM_MEMFD, VMMF_FREE) when the vnode goes, which
 * is when no descriptor and no mapping is left.  The object holds a
 * reference of its own on each page - as the cache does on the pages it
 * keeps - so that a page outlives the mappings that come and go, and the
 * page is freed when that reference and the last mapping's are gone.
 *
 * Pages are made on first touch, zeroed, and from then on every mapping
 * of that offset is linked to the same page.  A shared mapping writes it
 * in place; a private one (MAP_PRIVATE) copies on its first write, as a
 * private file mapping does.
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"

struct memobj {
	ino_t ino;			/* PFS's inode number: the name */
	size_t npages;			/* entries in pages[] */
	struct phys_block **pages;	/* NULL where nothing touched yet */
	struct memobj *next;
};

static struct memobj *memobjs;

static int memobj_unreference(struct phys_region *pr);
static int memobj_pagefault(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io);
static int memobj_sanitycheck(struct phys_region *pr, const char *file,
	int line);
static int memobj_writable(struct phys_region *pr);
static int memobj_copy(struct vir_region *vr, struct vir_region *newvr);
static void memobj_split(struct vmproc *vmp, struct vir_region *vr,
	struct vir_region *r1, struct vir_region *r2);
static int memobj_lowshrink(struct vir_region *vr, vir_bytes len);
static void memobj_delete(struct vir_region *region);
static int memobj_pt_flags(struct vir_region *vr);

struct mem_type mem_type_memobj = {
	.name = "memory object",
	.ev_unreference = memobj_unreference,
	.ev_pagefault = memobj_pagefault,
	.ev_sanitycheck = memobj_sanitycheck,
	.ev_copy = memobj_copy,
	.writable = memobj_writable,
	.ev_split = memobj_split,
	.ev_lowshrink = memobj_lowshrink,
	.ev_delete = memobj_delete,
	.pt_flags = memobj_pt_flags,
};

/*===========================================================================*
 *				the objects				     *
 *===========================================================================*/
static struct memobj *memobj_find(ino_t ino, int create)
{
	struct memobj *obj;

	for(obj = memobjs; obj; obj = obj->next)
		if(obj->ino == ino)
			return obj;

	if(!create || !(obj = calloc(1, sizeof(*obj))))
		return NULL;
	obj->ino = ino;
	obj->next = memobjs;
	memobjs = obj;
	return obj;
}

/* The object lets go of a page: freed if no mapping has it either. */
static void memobj_drop_page(struct phys_block *pb)
{
	assert(pb->refcount >= 1);
	pb->refcount--;
	if(pb->refcount == 0) {
		assert(!pb->firstregion);
		assert(pb->phys != MAP_NONE);
		free_mem(ABS2CLICK(pb->phys), 1);
		SLABFREE(pb);
	}
}

/* The page at an index, made (zeroed) if nothing has touched it yet. */
static int memobj_page(struct memobj *obj, size_t idx, u32_t allocflags,
	struct phys_block **pbp)
{
	struct phys_block *pb, **np;
	phys_bytes cl;
	size_t n;

	if(idx >= obj->npages) {
		n = obj->npages ? obj->npages : 64;
		while(n <= idx)
			n *= 2;
		if(!(np = realloc(obj->pages, n * sizeof(*np))))
			return ENOMEM;
		memset(np + obj->npages, 0, (n - obj->npages) * sizeof(*np));
		obj->pages = np;
		obj->npages = n;
	}

	if((pb = obj->pages[idx]) == NULL) {
		if((cl = alloc_mem(1, allocflags | PAF_CLEAR)) == NO_MEM)
			return ENOMEM;
		if(!(pb = pb_new(CLICK2ABS(cl)))) {
			free_mem(cl, 1);
			return ENOMEM;
		}
		pb->refcount = 1;		/* the object's own */
		obj->pages[idx] = pb;
	}

	*pbp = pb;
	return OK;
}

static void memobj_free(struct memobj *obj)
{
	struct memobj **pp;
	size_t i;

	for(i = 0; i < obj->npages; i++)
		if(obj->pages[i])
			memobj_drop_page(obj->pages[i]);
	free(obj->pages);

	for(pp = &memobjs; *pp != obj; pp = &(*pp)->next)
		assert(*pp);
	*pp = obj->next;
	free(obj);
}

/*
 * The object's size went down to "size": what lies past it reads as zero
 * from now on.  A page nobody maps is freed; one still mapped somewhere is
 * zeroed and kept, so that every mapping goes on seeing the same page.
 */
static void memobj_trunc(struct memobj *obj, u64_t size)
{
	size_t i, first;
	vir_bytes off;
	struct phys_block *pb;

	first = (size_t)((size + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);

	if((off = size % VM_PAGE_SIZE) != 0 && first - 1 < obj->npages &&
	    (pb = obj->pages[first - 1]) != NULL)
		sys_memset(NONE, 0, pb->phys + off, VM_PAGE_SIZE - off);

	for(i = first; i < obj->npages; i++) {
		if(!(pb = obj->pages[i]))
			continue;
		if(pb->refcount == 1) {
			obj->pages[i] = NULL;
			memobj_drop_page(pb);
		} else
			sys_memset(NONE, 0, pb->phys, VM_PAGE_SIZE);
	}
}

/*===========================================================================*
 *				do_memfd				     *
 *===========================================================================*/
int do_memfd(message *m)
{
	struct memobj *obj;

	/* VFS speaks for the object; nobody else knows it. */
	if(m->m_source != VFS_PROC_NR)
		return EPERM;

	obj = memobj_find((ino_t)m->VMMF_INO, 0);

	switch(m->VMMF_OP) {
	case VMMF_FREE:
		if(obj)
			memobj_free(obj);
		return OK;
	case VMMF_TRUNC:
		if(obj)
			memobj_trunc(obj, m->VMMF_SIZE);
		return OK;
	default:
		return EINVAL;
	}
}

/*===========================================================================*
 *				mmap_memobj				     *
 *===========================================================================*/
/*
 * VFS has answered a mmap lookup with a memory object: map it.  "fd" is
 * VM's own descriptor of it, which the mapping keeps (or which is closed
 * here, if VM had one already).
 */
int mmap_memobj(struct vmproc *vmp, int fd, ino_t ino, off_t offset,
	int flags, int prot, vir_bytes addr, vir_bytes len, vir_bytes *retaddr)
{
	struct vir_region *vr;
	struct fdref *ref;
	u32_t vrflags = 0;

	if(offset < 0 || (offset % VM_PAGE_SIZE)) {
		(void)vfs_request(VMVFSREQ_FDCLOSE, fd, vmp, 0, 0, NULL, NULL,
		    NULL, 0);
		return EINVAL;
	}

	if(prot & PROT_WRITE)
		vrflags |= VR_WRITABLE;
	if(flags & MAP_SHARED)
		vrflags |= VR_SHARED;

	if(!(vr = mmap_region(vmp, addr, flags, roundup(len, VM_PAGE_SIZE),
	    vrflags, &mem_type_memobj, 0))) {
		(void)vfs_request(VMVFSREQ_FDCLOSE, fd, vmp, 0, 0, NULL, NULL,
		    NULL, 0);
		return ENOMEM;
	}

	ref = fdref_dedup_or_new(vmp, ino, VMV_DEV_MEMFD, fd, 1);
	assert(ref);
	fdref_ref(ref, vr);
	vr->param.file.offset = (u64_t)offset;
	vr->param.file.clearend = 0;
	vr->param.file.inited = 1;

	*retaddr = vr->vaddr;
	return OK;
}

/*===========================================================================*
 *				the memory type				     *
 *===========================================================================*/
static int memobj_pt_flags(struct vir_region *vr)
{
#if defined(__arm__)
	return ARM_VM_PTE_CACHED;
#elif defined(__aarch64__)
	return AARCH64_VM_PTE_CACHED;
#else
	return 0;
#endif
}

/* The last reference went - the object's among them, or it would not. */
static int memobj_unreference(struct phys_region *pr)
{
	assert(pr->ph->refcount == 0);
	if(pr->ph->phys != MAP_NONE)
		free_mem(ABS2CLICK(pr->ph->phys), 1);
	return OK;
}

static int memobj_pagefault(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io)
{
	struct memobj *obj;
	struct phys_block *pb;
	u64_t off;
	int r;

	assert(region->param.file.inited);
	assert(ph->ph->refcount > 0);

	if(ph->ph->phys == MAP_NONE) {
		off = region->param.file.offset + ph->offset;
		if(!(obj = memobj_find(region->param.file.fdref->ino, 1)))
			return ENOMEM;
		if((r = memobj_page(obj, (size_t)(off / VM_PAGE_SIZE),
		    vrallocflags(region), &pb)) != OK)
			return r;

		/* The placeholder goes, the object's page comes in. */
		pb_unreferenced(region, ph, 0);
		pb_link(ph, pb, ph->offset, region);
	}

	/* A private mapping keeps its writes to itself. */
	if(write && !(region->flags & VR_SHARED))
		return mem_cow(region, ph, MAP_NONE, MAP_NONE);

	return OK;
}

static int memobj_sanitycheck(struct phys_region *pr, const char *file,
	int line)
{
	return OK;
}

static int memobj_writable(struct phys_region *pr)
{
	return pr->ph->phys != MAP_NONE && (pr->parent->flags & VR_SHARED);
}

static int memobj_copy(struct vir_region *vr, struct vir_region *newvr)
{
	assert(vr->param.file.inited);
	newvr->param.file = vr->param.file;
	newvr->param.file.fdref = NULL;
	fdref_ref(vr->param.file.fdref, newvr);
	return OK;
}

static void memobj_split(struct vmproc *vmp, struct vir_region *vr,
	struct vir_region *r1, struct vir_region *r2)
{
	assert(vr->param.file.inited);

	r1->param.file = vr->param.file;
	r2->param.file = vr->param.file;
	fdref_ref(vr->param.file.fdref, r1);
	fdref_ref(vr->param.file.fdref, r2);
	r2->param.file.offset += r1->length;
}

static int memobj_lowshrink(struct vir_region *vr, vir_bytes len)
{
	assert(vr->param.file.inited);
	vr->param.file.offset += len;
	return OK;
}

static void memobj_delete(struct vir_region *region)
{
	assert(region->param.file.inited);
	fdref_deref(region);
	region->param.file.inited = 0;
}
