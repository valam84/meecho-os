/*
 * How the kernel reaches memory that is not its own.
 *
 * Three things live here. Reading a process's translation tables
 * (vm_lookup); copying and clearing memory in another address space
 * (virtual_copy_f, vm_memset and the calls that wrap them); and the list of
 * physical ranges the kernel needs mapped for itself, which drivers register
 * before paging exists and VM maps once it does.
 *
 *
 * Why there are no windows here
 * -----------------------------
 * i386 and earm cannot see all of physical memory at once, so their version
 * of this file is built around freepdes: a pair of page directory entries
 * kept free so the kernel can point one at a foreign page, use it, and put
 * it back. Everything else there follows from that - createpde(), the
 * reload after a changed mapping, the per-CPU stale TLB bitmap.
 *
 * None of it exists here. All of RAM is mapped in the upper half at a fixed
 * offset, so phys2vir() of a physical address is already a pointer the
 * kernel can use, for any page, at any time. What is left of "copy between
 * two address spaces" is: walk the source table, walk the destination table,
 * memcpy through the linear map.
 *
 *
 * What that costs, and what has to be checked by hand
 * --------------------------------------------------
 * The linear map is the kernel's own mapping, with the kernel's own
 * permissions. It does not carry the permissions VM gave the page in the
 * process. So a write the process itself would not be allowed to make - to a
 * copy-on-write page, above all - goes through silently unless somebody
 * looks.
 *
 * On the 32-bit ports nobody has to look: they reach the page through a
 * window carrying the process's own descriptor, the write faults, and the
 * fault is turned into a request for VM to sort the page out. Here the fault
 * would never happen, so the permission is read out of the descriptor
 * instead and the same request is made deliberately. That is why vm_lookup
 * has a private form that returns the descriptor, and why the copy paths ask
 * for read or for write rather than just for an address.
 *
 * This is also why there is no fault-catching primitive in this file. See
 * PORTING-LOG.md, stage 4 group 2.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"

#include <machine/vm.h>

#include <minix/type.h>
#include <minix/syslib.h>
#include <minix/cachectl.h>
#include <string.h>
#include <assert.h>

#include "arch_proto.h"
#include "kernel/proto.h"
#include "kernel/debug.h"
#include "kernel/ktrace.h"

/*
 * Whether a process has an address space of its own. The kernel tasks do
 * not: they run in the upper half, which every address space shares. A user
 * process does not either until VM has given it one, which is what
 * VMCTL_SETADDRSPACE does.
 */
#define HASPT(procptr)	((procptr)->p_seg.p_ttbr != 0)

/*
 * Ranges drivers have asked for, newest first.
 *
 * The pointers in it - the head, each next, each id and each callback - are
 * kernel-virtual, even for the entries registered before the MMU was on. See
 * kern_req_phys_map() for why, and pg_utils.c's map_devices() for the one
 * walk that has to undo it.
 */
static kern_phys_map *kern_phys_map_head;

/*===========================================================================*
 *				kern_virt				     *
 *===========================================================================*/
/*
 * The upper-half form of an address that names something in the kernel
 * image, whichever form it arrives in.
 *
 * Idempotent on purpose: early boot reaches a kernel symbol at its physical
 * address and later code reaches the same symbol at its virtual one, and this
 * has to give the same answer to both without being told which is which. It
 * is the inverse of sym_phys() in pg_utils.c, which folds the other way for
 * the same reason.
 */
static vir_bytes
kern_virt(vir_bytes a)
{
	return a >= KERNEL_VA_OFFSET ? a : a + KERNEL_VA_OFFSET;
}

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
 *				memory_init				     *
 *===========================================================================*/
void
memory_init(void)
{
	/*
	 * Nothing to set up. Both 32-bit ports reserve a pair of page
	 * directory entries here - freepdes - which the kernel then borrows
	 * whenever it has to look at a page that is not in the current
	 * address space: it writes the page's physical address into a spare
	 * directory slot, uses the window, and puts the slot back.
	 *
	 * That whole mechanism exists because a 32-bit kernel cannot see all
	 * of physical memory at once. This one can: RAM is mapped in the
	 * upper half at a fixed offset, so phys2vir() of any physical address
	 * is already a usable pointer and there is no window to open. See
	 * pg_utils.c and port/PORTING-LOG.md, stage 4 group 1.
	 *
	 * kinfo.freepde_start is left at zero for the same reason. VM still
	 * has a freepde() of its own in servers/vm/pt.c, and it will go the
	 * same way when that file learns about four levels of tables.
	 */
}

/*===========================================================================*
 *				mem_clear_mapcache			     *
 *===========================================================================*/
void
mem_clear_mapcache(void)
{
	/*
	 * VM calls this through VMCTL_CLEARMAPCACHE to say that it has
	 * changed a mapping the kernel may have cached. The kernel caches
	 * none: every access to another address space walks that space's
	 * tables at the moment of the access, so there is nothing to forget.
	 *
	 * The 32-bit ports clear their freepde slots here, which is the
	 * cache in question.
	 */
}

/*===========================================================================*
 *				mem_kern_ptr				     *
 *===========================================================================*/
/*
 * A pointer the kernel may use for the physical page at pa, or NULL.
 *
 * Only RAM is in the linear map. A physical address that came out of a page
 * table can name device registers, or nothing at all if the table is
 * damaged, and phys2vir() would produce an address for either. Dereferencing
 * it would be a data abort in the kernel with no handler, where the honest
 * answer to the caller is EFAULT.
 */
static void *
mem_kern_ptr(phys_bytes pa)
{
	if (!pg_is_ram(pa))
		return NULL;

	return (void *)phys2vir(pa);
}

/*===========================================================================*
 *				vm_lookup_desc				     *
 *===========================================================================*/
/*
 * Translate a virtual address in a process the way the hardware would, and
 * hand back the leaf descriptor as well as the physical address.
 *
 * The descriptor is what the callers in this file are really after: it
 * carries the permission bits, and on this architecture the kernel has to
 * consult them itself. It is a 64-bit value, which is why this is a private
 * function and not vm_lookup()'s ptent parameter.
 */
int
vm_lookup_desc(const struct proc *proc, vir_bytes virtual,
	phys_bytes *physical, u64_t *desc)
{
	phys_bytes table_pa;
	int level;

	assert(proc);
	assert(physical);
	assert(!isemptyp(proc));
	assert(HASPT(proc));

	/*
	 * A process's table translates the lower half only. An upper-half
	 * address does not merely have no entry in it: bits 48 and above are
	 * not part of any index, so walking one would quietly answer for a
	 * different address altogether. 0xffff_0000_4000_0000 would be looked
	 * up as 0x4000_0000. Refuse it here rather than return a plausible
	 * wrong page.
	 */
	if (virtual >= (1UL << AARCH64_VA_BITS))
		return EFAULT;

	table_pa = proc->p_seg.p_ttbr & AARCH64_VM_ADDR_MASK;

	for (level = 0; ; level++) {
		unsigned shift = AARCH64_VM_LEVEL_SHIFT(level);
		u64_t *table = mem_kern_ptr(table_pa);
		u64_t e;

		if (table == NULL)
			return EFAULT;

		e = table[AARCH64_VM_INDEX(virtual, level)];

		if (!(e & AARCH64_VM_VALID))
			return EFAULT;

		if (level == 3) {
			/*
			 * At the last level bit 1 distinguishes a page from a
			 * reserved encoding, not a table from a block.
			 */
			if (!(e & AARCH64_VM_PAGE))
				return EFAULT;
		} else if (e & AARCH64_VM_TABLE) {
			table_pa = AARCH64_VM_PFA(e);
			continue;
		} else if (level == 0) {
			/* The 4 KiB granule has no level 0 block. */
			return EFAULT;
		}

		/* A leaf: a page at level 3, a block at level 1 or 2. */
		if (desc != NULL)
			*desc = e;
		*physical = AARCH64_VM_PFA(e) | (virtual & ((1UL << shift) - 1));

		return OK;
	}
}

/*===========================================================================*
 *				vm_lookup				     *
 *===========================================================================*/
int
vm_lookup(const struct proc *proc, const vir_bytes virtual,
	phys_bytes *physical, u32_t *ptent)
{
	/*
	 * ptent is not supported and cannot be. The generic prototype types
	 * it as a pointer to a 32-bit word, which was a page table entry when
	 * every port had 32-bit ones; a VMSAv8-64 descriptor is 64 bits wide
	 * and would arrive here with its upper attributes - PXN, UXN, the
	 * software bits - cut off. Handing back a truncated descriptor is
	 * worse than not answering.
	 *
	 * Nothing in the tree asks: every caller of vm_lookup(), in the
	 * generic kernel and in the two 32-bit ports alike, passes NULL. If
	 * one ever wants the descriptor, the prototype in kernel/proto.h is
	 * what has to widen, and vm_lookup_desc() above is already the
	 * function it wants.
	 */
	if (ptent != NULL)
		panic("vm_lookup: ptent cannot hold an aarch64 descriptor");

	return vm_lookup_desc(proc, virtual, physical, NULL);
}

/*===========================================================================*
 *				vm_lookup_range				     *
 *===========================================================================*/
size_t
vm_lookup_range(const struct proc *proc, vir_bytes vir_addr,
	phys_bytes *phys_addr, size_t bytes)
{
	/*
	 * How much of the range starting at vir_addr is contiguous in
	 * physical memory: between 0 and bytes. If nonzero and phys_addr is
	 * not NULL, the physical address of the start is stored there. The
	 * caller has already established that the range is valid for the
	 * process; this only answers about contiguity.
	 */
	phys_bytes phys, next_phys;
	size_t len;

	assert(proc);
	assert(bytes > 0);
	assert(HASPT(proc));

	if (vm_lookup_desc(proc, vir_addr, &phys, NULL) != OK)
		return 0;

	if (phys_addr != NULL)
		*phys_addr = phys;

	len = AARCH64_PAGE_SIZE - (vir_addr % AARCH64_PAGE_SIZE);
	vir_addr += len;
	next_phys = phys + len;

	while (len < bytes) {
		if (vm_lookup_desc(proc, vir_addr, &phys, NULL) != OK)
			break;

		if (next_phys != phys)
			break;

		len += AARCH64_PAGE_SIZE;
		vir_addr += AARCH64_PAGE_SIZE;
		next_phys += AARCH64_PAGE_SIZE;
	}

	/* The last step may have gone past what was asked for. */
	return MIN(bytes, len);
}

/*===========================================================================*
 *				vm_check_range				     *
 *===========================================================================*/
int
vm_check_range(struct proc *caller, struct proc *target, vir_bytes vir_addr,
	size_t bytes, int writeflag)
{
	/*
	 * Ask VM to make a range of the target's memory usable, on behalf of
	 * caller. The call suspends caller and is expected to be made a
	 * second time once VM has answered, which is when the result of the
	 * first attempt is returned.
	 */
	int r;

	if ((caller->p_misc_flags & MF_KCALL_RESUME) &&
	    (r = caller->p_vmrequest.vmresult) != OK)
		return r;

	vm_suspend(caller, target, vir_addr, bytes, VMSTYPE_KERNELCALL,
	    writeflag);

	return VMSUSPEND;
}

/*===========================================================================*
 *			      check_resumed_caller			     *
 *===========================================================================*/
/* What VM decided, if this call is the second half of a suspended one. */
static int
check_resumed_caller(struct proc *caller)
{
	if (caller && (caller->p_misc_flags & MF_KCALL_RESUME)) {
		assert(caller->p_vmrequest.vmresult != VMSUSPEND);
		return caller->p_vmrequest.vmresult;
	}

	return OK;
}

/*===========================================================================*
 *				resolve					     *
 *===========================================================================*/
/*
 * Turn an address in some address space into one the kernel can use, and say
 * how far it stays usable.
 *
 * pr == NULL means the address is physical; a kernel task means it is
 * already a kernel address; anything else means a walk of that process's
 * tables. *bytes is truncated to what this one answer covers, which for a
 * user process is the rest of the page.
 *
 * Failure is EFAULT and covers both "not mapped" and "not writable by the
 * process", because both of them are things VM can fix and the caller treats
 * them the same way.
 */
static int
resolve(const struct proc *pr, vir_bytes addr, vir_bytes *bytes, int write,
	void **kaddr)
{
	phys_bytes pa;
	u64_t desc;

	if (pr == NULL) {
		/*
		 * A physical address. The linear map is a fixed offset, so a
		 * physically contiguous range is contiguous there too and
		 * needs no chunking - but a range running off the end of RAM
		 * would leave the map, so both ends are checked.
		 */
		if ((*kaddr = mem_kern_ptr(addr)) == NULL)
			return EFAULT;
		if (!pg_is_ram(addr + *bytes - 1))
			return EFAULT;

		return OK;
	}

	if (iskernelp(pr)) {
		/*
		 * A kernel address, and the kernel half is mapped the same way
		 * in every address space because it is in TTBR1 and TTBR1 is
		 * never switched. Nothing to translate.
		 */
		*kaddr = (void *)addr;
		return OK;
	}

	if (!HASPT(pr))
		return EFAULT;

	if (vm_lookup_desc(pr, addr, &pa, &desc) != OK)
		return EFAULT;

	/*
	 * The write permission is the process's, not the kernel's. Through
	 * the linear map the kernel could write here regardless; that is
	 * exactly what must not happen to a page VM is holding read-only for
	 * copy-on-write. Refusing sends the caller to vm_suspend(), which is
	 * where the 32-bit ports arrive by way of a page fault.
	 */
	if (write && (desc & AARCH64_VM_AP_RO))
		return EFAULT;

	if ((*kaddr = mem_kern_ptr(pa)) == NULL)
		return EFAULT;

	/*
	 * One page at a time. A block descriptor would let this go further,
	 * but VM builds user address spaces out of 4 KiB pages, so the case
	 * does not arise and guessing at it would be untested code.
	 */
	*bytes = MIN(*bytes,
	    AARCH64_PAGE_SIZE - (addr % AARCH64_PAGE_SIZE));

	return OK;
}

/*===========================================================================*
 *				arch_cache_range			     *
 *===========================================================================*/
/*
 * One data cache maintenance operation over a range of the caller's memory.
 *
 * The work itself is dcache_range() in cache.c; what happens here is the
 * translation, and the translation is also the permission check.  resolve()
 * walks the caller's own page table, refuses an address it has not got, and
 * refuses to hand back a writable pointer to a page the process may only
 * read - which is exactly the question to ask of an operation that discards
 * cache lines.  Cleaning discards nothing, so it is allowed on a read-only
 * mapping.
 *
 * resolve() answers one page at a time for a user process, so a range
 * crossing a page boundary comes in pieces, and each piece is treated as a
 * range of its own with two partial ends.  With a cache line no larger than
 * a page - which is every implementation there is - a page holds a whole
 * number of lines, no line straddles a seam, and the pieces come out exactly
 * as the whole range would have.  Were a line ever larger, the lines at the
 * seams would be written back before being discarded rather than after,
 * which is slower and still correct.
 */
int
arch_cache_range(struct proc *caller, vir_bytes addr, vir_bytes len, int op)
{
	int write = (op != CACHE_CLEAN);

	while (len > 0) {
		vir_bytes chunk = len;
		void *kaddr;
		phys_bytes pa;
		u64_t desc;
		int r;

		/*
		 * The caller's mapping has to be Normal cacheable, like the
		 * kernel's.  Maintenance on memory the caller mapped as
		 * Device or Non-cacheable is a contradiction: its own
		 * accesses bypass the cache, so whatever the kernel finds in
		 * the cache for that page is somebody else's - stale zeroes
		 * from the allocator, as it turned out - and cleaning it
		 * writes that somebody else's data over the caller's.  Saying
		 * EINVAL here is what would have pointed at the page
		 * attributes on the first run instead of the sixth.
		 */
		if (!iskernelp(caller)) {
			if (vm_lookup_desc(caller, addr, &pa, &desc) != OK)
				return EFAULT;
			if ((desc & AARCH64_VM_ATTRINDX(7)) !=
			    AARCH64_VM_ATTRINDX(AARCH64_MAIR_NORMAL))
				return EINVAL;
		}

		if ((r = resolve(caller, addr, &chunk, write, &kaddr)) != OK)
			return r;

		dcache_range(op, (unsigned long)kaddr, chunk);

		addr += chunk;
		len -= chunk;
	}

	return OK;
}

/*===========================================================================*
 *				lin_lin_copy				     *
 *===========================================================================*/
/*
 * Copy between two address spaces, either of which may be physical memory,
 * the kernel, or a process.
 *
 * The name is the one i386 and earm use for the same job, so that the three
 * can be compared; the body is not. There is no window to open, no page
 * directory to reload and no fault to catch - just two lookups and a copy,
 * bounded by whichever side runs out of contiguous memory first.
 */
static int
lin_lin_copy(struct proc *srcproc, vir_bytes srclinaddr,
	struct proc *dstproc, vir_bytes dstlinaddr, vir_bytes bytes)
{
	if (srcproc)
		assert(!RTS_ISSET(srcproc, RTS_SLOT_FREE));
	if (dstproc)
		assert(!RTS_ISSET(dstproc, RTS_SLOT_FREE));
	if (srcproc)
		assert(!RTS_ISSET(srcproc, RTS_VMINHIBIT));
	if (dstproc)
		assert(!RTS_ISSET(dstproc, RTS_VMINHIBIT));

	while (bytes > 0) {
		vir_bytes chunk = bytes;
		void *src, *dst;

		if (resolve(srcproc, srclinaddr, &chunk, 0, &src) != OK)
			return EFAULT_SRC;
		if (resolve(dstproc, dstlinaddr, &chunk, 1, &dst) != OK)
			return EFAULT_DST;

		/*
		 * The source lookup ran before the destination shortened the
		 * chunk. That is fine - it answered about the first byte, and
		 * a shorter run starting there is still inside what it
		 * covered.
		 */
		memcpy(dst, src, chunk);

		bytes -= chunk;
		srclinaddr += chunk;
		dstlinaddr += chunk;
	}

	return OK;
}

/*===========================================================================*
 *				virtual_copy_f				     *
 *===========================================================================*/
int
virtual_copy_f(struct proc *caller, struct vir_addr *src_addr,
	struct vir_addr *dst_addr, vir_bytes bytes, int vmcheck)
{
	struct vir_addr *vir_addr[2];
	struct proc *procs[2];
	int i, r;

	assert((vmcheck && caller) || (!vmcheck && !caller));

	if (bytes <= 0)
		return EDOM;

	vir_addr[_SRC_] = src_addr;
	vir_addr[_DST_] = dst_addr;

	for (i = _SRC_; i <= _DST_; i++) {
		endpoint_t proc_e = vir_addr[i]->proc_nr_e;
		int proc_nr;

		if (proc_e == NONE) {
			procs[i] = NULL;	/* a physical address */
		} else {
			if (!isokendpt(proc_e, &proc_nr)) {
				printf("virtual_copy: no reasonable "
				    "endpoint\n");
				return ESRCH;
			}
			procs[i] = proc_addr(proc_nr);
		}
	}

	if ((r = check_resumed_caller(caller)) != OK)
		return r;

	r = lin_lin_copy(procs[_SRC_], vir_addr[_SRC_]->offset,
	    procs[_DST_], vir_addr[_DST_]->offset, bytes);
	if (r == OK)
		return OK;

	if (r != EFAULT_SRC && r != EFAULT_DST)
		panic("lin_lin_copy failed: %d", r);

	if (!vmcheck || !caller)
		return r;

	/*
	 * One side was unmapped, or - and this is the case the 32-bit ports
	 * reach through a page fault instead - mapped in a way that forbids
	 * the access. Either way VM is the one who can do something about it,
	 * and the call is suspended until it has.
	 */
	if (r == EFAULT_SRC) {
		assert(procs[_SRC_]);
		vm_suspend(caller, procs[_SRC_], vir_addr[_SRC_]->offset,
		    bytes, VMSTYPE_KERNELCALL, 0);
	} else {
		assert(procs[_DST_]);
		vm_suspend(caller, procs[_DST_], vir_addr[_DST_]->offset,
		    bytes, VMSTYPE_KERNELCALL, 1);
	}

	return VMSUSPEND;
}

/*===========================================================================*
 *				data_copy				     *
 *===========================================================================*/
int
data_copy(const endpoint_t from_proc, const vir_bytes from_addr,
	const endpoint_t to_proc, const vir_bytes to_addr, size_t bytes)
{
	struct vir_addr src, dst;

	src.offset = from_addr;
	dst.offset = to_addr;
	src.proc_nr_e = from_proc;
	dst.proc_nr_e = to_proc;
	assert(src.proc_nr_e != NONE);
	assert(dst.proc_nr_e != NONE);

	return virtual_copy(&src, &dst, bytes);
}

/*===========================================================================*
 *				data_copy_vmcheck			     *
 *===========================================================================*/
int
data_copy_vmcheck(struct proc *caller, const endpoint_t from_proc,
	const vir_bytes from_addr, const endpoint_t to_proc,
	const vir_bytes to_addr, size_t bytes)
{
	struct vir_addr src, dst;

	src.offset = from_addr;
	dst.offset = to_addr;
	src.proc_nr_e = from_proc;
	dst.proc_nr_e = to_proc;
	assert(src.proc_nr_e != NONE);
	assert(dst.proc_nr_e != NONE);

	return virtual_copy_vmcheck(caller, &src, &dst, bytes);
}

/*===========================================================================*
 *				vm_memset				     *
 *===========================================================================*/
int
vm_memset(struct proc *caller, endpoint_t who, phys_bytes ph, int c,
	phys_bytes count)
{
	struct proc *whoptr = NULL;
	phys_bytes left = count;
	vir_bytes cur = ph;
	int r;

	if ((r = check_resumed_caller(caller)) != OK)
		return r;

	/* NONE means ph is physical; anything else means it is virtual. */
	if (who != NONE && !(whoptr = endpoint_lookup(who)))
		return ESRCH;

	c &= 0xFF;

	while (left > 0) {
		vir_bytes chunk = left;
		void *dst;

		if (resolve(whoptr, cur, &chunk, 1, &dst) != OK) {
			/*
			 * A process can be helped; a bad physical address
			 * cannot, and the caller had no business handing one
			 * over.
			 */
			if (whoptr == NULL)
				panic("vm_memset: bad physical address "
				    "0x%lx", (unsigned long)cur);

			vm_suspend(caller, whoptr, ph, count,
			    VMSTYPE_KERNELCALL, 1);
			return VMSUSPEND;
		}

		memset(dst, c, chunk);

		cur += chunk;
		left -= chunk;
	}

	return OK;
}

/*===========================================================================*
 *			     __switch_address_space			     *
 *===========================================================================*/
void
__switch_address_space(struct proc *p, struct proc **__ptproc)
{
	reg_t new_ttbr = p->p_seg.p_ttbr;

	if (new_ttbr == 0)
		return;

	/*
	 * VM has rewritten this process's mappings since it last ran, and on
	 * this architecture that has to be acted on here.
	 *
	 * proc.c raises MF_FLUSH_TLB when VM inhibits a process in order to
	 * rewrite its page tables, and retires the TLB itself only when that
	 * process's address space is the one already loaded. Every other case
	 * is left to this function - because on i386 reaching it means
	 * reloading CR3, and reloading CR3 empties the TLB. Here it does not:
	 * that a switch leaves the other space's entries in place is the
	 * entire point of tagging address spaces. The stale entries would
	 * survive exactly the rewrite that was meant to invalidate them.
	 *
	 * The CB2 found this, and nothing else could have. init's fork took a
	 * copy-on-write fault, VM copied the page and marked it writable, and
	 * the process came back to a TLB entry that still said read-only: one
	 * address faulting twenty thousand times over a descriptor that
	 * plainly permitted the write. QEMU cannot show it - its TCG TLB is
	 * untagged and a switch empties it, which is the assumption this code
	 * deliberately broke.
	 *
	 * By tag, because the process is known here; before the switch,
	 * because the entries belong to the space being entered. The flag
	 * itself is cleared by proc.c after this returns.
	 */
	if ((p->p_misc_flags & MF_FLUSH_TLB) && pg_asids_usable()) {
		KTRACE_EV(KTV_TLBFLUSH);
		refresh_tlb_asid(AARCH64_PROC_ASID(p));
	}

	/*
	 * Only TTBR0 changes. The kernel is in TTBR1 and stays there through
	 * every switch, which is the whole reason the upper half was chosen
	 * at stage 2.3: there is no kernel mapping to rebuild and no window
	 * to invalidate, and a process cannot even name a kernel address.
	 */
	if (new_ttbr == read_ttbr0())
		return;

	/*
	 * Counted here rather than at the call: switch_to_user() asks for a
	 * switch on every entry, and most of them find the space already
	 * loaded.  What costs something is the write below.
	 */
	KTRACE_EV(KTV_ASIDSW);

	write_ttbr0(new_ttbr);

	/*
	 * And nothing else, on any machine this runs on. The value written
	 * above carries the address space's ASID in its top bits, user
	 * mappings are non-global, so the entries of the space being left
	 * stay in the TLB, tagged, and are still valid when it runs again.
	 * That is the whole reason the mappings were made non-global.
	 *
	 * The flush is still here for a machine whose ASID field is too
	 * narrow to give every process slot a distinct tag - see
	 * pg_asids_usable(). There the tags would alias, which is not a slow
	 * kernel but a wrong one, so that machine pays what this port paid
	 * before ASIDs: the whole TLB, on every switch.
	 */
	if (!pg_asids_usable()) {
		KTRACE_EV(KTV_TLBFLUSH);
		refresh_tlb();
	}

	*__ptproc = p;
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

	/*
	 * Every address that names something in the kernel image is stored as
	 * the upper-half one, whichever world the caller is in.
	 *
	 * This list is the only structure the kernel builds on one side of the
	 * MMU switch and walks on the other. Drivers register their ranges
	 * from pre_init(), where taking the address of a static yields a
	 * physical address, and pre_init_high() walks the list afterwards to
	 * hand each driver its new base - by which time those addresses are
	 * not mapped at all, because the identity map covers only the image
	 * and the device registers and is about to go entirely.
	 *
	 * Storing the upper-half form settles it once, at the one point every
	 * entry passes through, rather than at each of the places that walk
	 * the list. The one walk that happens before the switch -
	 * map_devices() in pg_utils.c - converts back, which it is already
	 * equipped to do: that file deals in both worlds by construction.
	 *
	 * kern_virt() is idempotent, so it does not matter whether this is
	 * called before or after the move. That is deliberate: a helper that
	 * had to know would be one more thing to get wrong on a path that
	 * cannot be single-stepped.
	 */
	priv->addr = base_address;		/* a device address: physical */
	priv->size = io_size;
	priv->vm_flags = vm_flags;
	priv->cb = (kern_phys_map_mapped)kern_virt((vir_bytes)cb);
	priv->id = kern_virt(id);
	priv->index = -1;

	priv->next = kern_phys_map_head;
	kern_phys_map_head = (kern_phys_map *)kern_virt((vir_bytes)priv);

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
 *			      kern_phys_map_list			     *
 *===========================================================================*/
kern_phys_map *
kern_phys_map_list(void)
{
	return kern_phys_map_head;
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
/*
 * What VM has to map into every process's address space, and nothing else.
 *
 * Only the pages the kernel publishes to user space: minix_kerninfo and what
 * it points at. The device ranges drivers registered through
 * kern_phys_map_ptr() are deliberately not offered, and that is where this
 * differs from earm.
 *
 * There they have to be, because the kernel and the process share one page
 * table, so the kernel's own mapping of the console or the interrupt
 * controller is a mapping VM makes. Here the kernel is in TTBR1: pre_init()
 * mapped those ranges for itself before the MMU came on, and pre_init_high()
 * ran the drivers' callbacks over them afterwards. Offering them again would
 * have VM answer with an address in the process's half of the address space,
 * and arch_enable_paging() would then point the console driver at an address
 * that exists only while VM's page table is loaded - so the kernel would
 * print into whatever the running process has there.
 */
int
arch_phys_map(const int index, phys_bytes *addr, phys_bytes *len, int *flags)
{
	static int first = 1;
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

	/* There is nothing else to reply about; see arch_phys_map(). */
	return EINVAL;
}

/*===========================================================================*
 *			     arch_enable_paging				     *
 *===========================================================================*/
int
arch_enable_paging(struct proc *caller)
{
	assert(caller->p_seg.p_ttbr);

	/* Load the caller's page table: the mappings VM made are now live. */
	switch_address_space(caller);

#ifdef CONFIG_SMP
	/*
	 * VM is running, which is where i386 also waits for the secondaries,
	 * and for the same reason: it is the last moment at which the system
	 * is still orderly enough for a count of cores that failed to come up
	 * to mean anything, and the first at which letting them run costs
	 * nothing.
	 */
	barrier();
	wait_for_APs_to_finish_booting();
#endif

	/*
	 * And that is all there is to do. On earm this is where every driver
	 * is moved onto the base VM chose for it, because until VM ran there
	 * was no page table with those ranges in it. Here pre_init() built
	 * the kernel's own and pre_init_high() moved the drivers then; VM is
	 * not asked about those ranges at all, and there is nothing left for
	 * it to answer. See arch_phys_map().
	 */
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
