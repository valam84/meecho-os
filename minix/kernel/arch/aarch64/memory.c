/*
 * Physical memory as the kernel hands it to VM.
 *
 * Two things live here so far. One is the list of physical ranges the kernel
 * needs mapped for itself - device registers, almost always - which drivers
 * register before paging exists and VM maps once it does. The other is the
 * pair of calls VM walks that list with.
 *
 * The kernel's own page table work is not here yet; it is the next piece of
 * the memory group and comes over from arch/aarch64/bringup/mmu.c.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"

#include <machine/vm.h>

#include <minix/type.h>
#include <minix/syslib.h>
#include <string.h>
#include <assert.h>

#include "arch_proto.h"
#include "kernel/proto.h"
#include "kernel/debug.h"

/* Ranges drivers have asked for, newest first. */
static kern_phys_map *kern_phys_map_head;

/* Defined in kernel.lds. */
extern char usermapped_start, usermapped_end, usermapped_nonglo_start;

static int usermapped_glo_index = -1, usermapped_index = -1,
	first_um_idx = -1;

/*===========================================================================*
 *				vir2phys				     *
 *===========================================================================*/
phys_bytes
vir2phys(void *ptr)
{
	/*
	 * Only for addresses in the kernel's own image and in the linear
	 * mapping of physical memory, which are the same thing here: the
	 * upper half is physical memory at a fixed offset, so the translation
	 * is a subtraction. A user address does not belong to this function -
	 * it belongs to a page table walk - and there is nothing sensible
	 * this could return for one.
	 */
	return (phys_bytes)ptr - KERNEL_VA_OFFSET;
}

/*===========================================================================*
 *			      kern_req_phys_map				     *
 *===========================================================================*/
int
kern_req_phys_map(phys_bytes base_address, vir_bytes io_size, int vm_flags,
	kern_phys_map *priv, kern_phys_map_mapped cb, vir_bytes id)
{
	/*
	 * The caller owns the list entry - it is a static in the driver - so
	 * this only fills it in and links it. Nothing here allocates, because
	 * this runs before there is anything to allocate from.
	 */
	assert(base_address != 0);
	assert(io_size % AARCH64_PAGE_SIZE == 0);
	assert(cb != NULL);

	priv->addr = base_address;
	priv->size = io_size;
	priv->vm_flags = vm_flags;
	priv->cb = cb;
	priv->id = id;
	priv->index = -1;

	priv->next = kern_phys_map_head;
	kern_phys_map_head = priv;

	return 0;
}

/*===========================================================================*
 *			    kern_phys_map_mapped_ptr			     *
 *===========================================================================*/
int
kern_phys_map_mapped_ptr(vir_bytes id, phys_bytes address)
{
	/* The id is the address of the driver's base variable. */
	*((vir_bytes *)id) = address;
	return 0;
}

/*===========================================================================*
 *				kern_phys_map_ptr			     *
 *===========================================================================*/
int
kern_phys_map_ptr(phys_bytes base_address, vir_bytes io_size, int vm_flags,
	kern_phys_map *priv, vir_bytes ptr)
{
	return kern_req_phys_map(base_address, io_size, vm_flags, priv,
	    kern_phys_map_mapped_ptr, ptr);
}

/*===========================================================================*
 *				arch_phys_map				     *
 *===========================================================================*/
int
arch_phys_map(const int index, phys_bytes *addr, phys_bytes *len, int *flags)
{
	static int first = 1;
	kern_phys_map *phys_maps;
	int freeidx = 0;
	vir_bytes glo_len = (vir_bytes)&usermapped_nonglo_start -
	    (vir_bytes)&usermapped_start;

	if (first) {
		memset(&minix_kerninfo, 0, sizeof(minix_kerninfo));
		if (glo_len > 0)
			usermapped_glo_index = freeidx++;

		usermapped_index = freeidx++;
		first_um_idx = usermapped_index;
		if (usermapped_glo_index != -1)
			first_um_idx = usermapped_glo_index;
		first = 0;

		/* Number the ranges drivers asked for, after those two. */
		for (phys_maps = kern_phys_map_head; phys_maps != NULL;
		    phys_maps = phys_maps->next)
			phys_maps->index = freeidx++;
	}

	if (index == usermapped_glo_index) {
		*addr = vir2phys(&usermapped_start);
		*len = glo_len;
		*flags = VMMF_USER | VMMF_GLO;
		return OK;
	} else if (index == usermapped_index) {
		*addr = vir2phys(&usermapped_nonglo_start);
		*len = (vir_bytes)&usermapped_end -
		    (vir_bytes)&usermapped_nonglo_start;
		*flags = VMMF_USER;
		return OK;
	}

	for (phys_maps = kern_phys_map_head; phys_maps != NULL;
	    phys_maps = phys_maps->next) {
		if (phys_maps->index == index) {
			*addr = phys_maps->addr;
			*len = phys_maps->size;
			*flags = phys_maps->vm_flags;
			return OK;
		}
	}

	return EINVAL;
}

/*===========================================================================*
 *			     arch_phys_map_reply			     *
 *===========================================================================*/
int
arch_phys_map_reply(const int index, const vir_bytes addr)
{
	kern_phys_map *phys_maps;

	if (index == first_um_idx) {
		vir_bytes usermapped_offset;

		/*
		 * VM has mapped the shared pages somewhere in the calling
		 * process and says where. Everything the kernel published
		 * there is pointed at through minix_kerninfo, so each of
		 * those pointers is moved by the same distance.
		 *
		 * On this architecture that distance is large and negative:
		 * the kernel's copy is in the upper half, which EL0 cannot
		 * reach at all, and the process sees the same pages in its
		 * own half. The two 32-bit ports, where one page table holds
		 * both halves, get a small offset here - but nothing in the
		 * interface ever promised a small one, so the arithmetic is
		 * the same and only the assertion below had to go.
		 */
		usermapped_offset = addr - (vir_bytes)&usermapped_start;
#define FIXEDPTR(ptr) (void *) ((vir_bytes)(ptr) + usermapped_offset)
#define ASSIGN(minixstruct) \
	minix_kerninfo.minixstruct = FIXEDPTR(&minixstruct)
		ASSIGN(kinfo);
		ASSIGN(machine);
		ASSIGN(kmessages);
		ASSIGN(loadinfo);
		ASSIGN(kuserinfo);
		ASSIGN(arm_frclock);
		ASSIGN(kclockinfo);
#undef ASSIGN
#undef FIXEDPTR

		minix_kerninfo.kerninfo_magic = KERNINFO_MAGIC;
		minix_kerninfo.minix_feature_flags = minix_feature_flags;
		minix_kerninfo_user = (vir_bytes)((vir_bytes)&minix_kerninfo +
		    usermapped_offset);

		minix_kerninfo.ki_flags |= MINIX_KIF_USERINFO;

		return OK;
	}

	if (index == usermapped_index)
		return OK;

	for (phys_maps = kern_phys_map_head; phys_maps != NULL;
	    phys_maps = phys_maps->next) {
		if (phys_maps->index == index) {
			assert(phys_maps->cb != NULL);
			/*
			 * Only record the address. The driver's base variable
			 * is not rewritten until the mapping is actually in
			 * force, which is what arch_enable_paging() does.
			 */
			phys_maps->vir = addr;
			return OK;
		}
	}

	return EINVAL;
}

/*===========================================================================*
 *			     arch_enable_paging				     *
 *===========================================================================*/
int
arch_enable_paging(struct proc *caller)
{
	kern_phys_map *phys_maps;

	assert(caller->p_seg.p_ttbr);

	/* Load the caller's page table: the mappings VM made are now live. */
	switch_address_space(caller);

	/*
	 * Move every driver onto its new base. There is no printing in here
	 * on purpose: the console is one of the drivers being moved, and
	 * until its callback has run its old address has gone and its new one
	 * has not arrived.
	 */
	for (phys_maps = kern_phys_map_head; phys_maps != NULL;
	    phys_maps = phys_maps->next) {
		assert(phys_maps->cb != NULL);
		phys_maps->cb(phys_maps->id, phys_maps->vir);
	}

	return OK;
}

/*===========================================================================*
 *			    release_address_space			     *
 *===========================================================================*/
void
release_address_space(struct proc *pr)
{
	pr->p_seg.p_ttbr_v = NULL;
	barrier();
}
