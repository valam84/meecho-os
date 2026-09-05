#ifndef _AARCH64_PROTO_H
#define _AARCH64_PROTO_H

#include <machine/vm.h>

/*
 * One page of kernel stack per CPU. The exception frame is 34 registers,
 * 272 bytes, and nothing in the kernel recurses deeply, so a page is ample;
 * what matters more is that the stacks are page-aligned and page-apart, so
 * that an overrun lands in a guard page instead of in the neighbouring CPU's
 * stack.
 */
#define K_STACK_SIZE	AARCH64_PAGE_SIZE

#ifndef __ASSEMBLY__

#include "cpufunc.h"

/* klib.S */
__dead void reset(void);
phys_bytes vir2phys(void *);
vir_bytes phys_memset(phys_bytes ph, u32_t c, phys_bytes bytes);

/*
 * Switching address spaces is a write to TTBR0 alone: the kernel half lives
 * in TTBR1 and never changes. The caller's current ptproc is passed in so
 * that the switch can be skipped when the space is already loaded, which is
 * the common case on the way back out of a system call.
 */
void __switch_address_space(struct proc *p, struct proc **__ptproc);
#define switch_address_space(proc)	\
	__switch_address_space(proc, get_cpulocal_var_ptr(ptproc))

/*
 * The labels the data abort handler compares a faulting return address
 * against, to tell a fault inside a deliberate copy from user space from a
 * fault in the kernel proper. The bring-up kernel calls the same mechanism
 * trap_expect_fault(); see port/PORTING-LOG.md, stage 2.4.
 */
void __copy_msg_from_user_end(void);
void __copy_msg_to_user_end(void);
void __user_copy_msg_pointer_failure(void);

/*
 * The memory map the kernel was handed at boot, and the one it hands on to
 * VM. Generic main() returns the bootstrap memory to it once the bootstrap
 * phase is over, which is the one entry point into the architecture memory
 * code that the generic kernel uses by name.
 */
void add_memmap(kinfo_t *cbi, u64_t addr, u64_t len);

/*
 * A range of physical memory the kernel needs mapped for itself: device
 * registers, in practice.
 *
 * A driver has to touch its registers before paging exists, when the address
 * it needs is the physical one, and after, when the address it needs is the
 * one VM chose. So it registers the range early, naming a variable that holds
 * its base, and VM calls back with the new address once the mapping is in
 * force. The list entry is the driver's own static, because this all runs
 * before there is anything to allocate from.
 *
 * The bring-up kernel has the same mechanism under the name
 * mmu_map_device(), with itself in the place of VM. This is the tree's
 * version, and it is the one ARM already had.
 */
typedef int (*kern_phys_map_mapped)(vir_bytes id, vir_bytes new_addr);

typedef struct kern_phys_map {
	phys_bytes addr;		/* physical address to map */
	vir_bytes size;			/* size of the mapping */
	vir_bytes id;			/* passed back to the callback */
	int vm_flags;			/* flags for VM */
	kern_phys_map_mapped cb;	/* called once the mapping is live */
	phys_bytes vir;			/* the address VM chose */
	int index;			/* index VM asks about it by */
	struct kern_phys_map *next;
} kern_phys_map;

int kern_req_phys_map(phys_bytes base_address, vir_bytes io_size,
	int vm_flags, kern_phys_map *priv, kern_phys_map_mapped cb,
	vir_bytes id);

/* The common case: the callback just writes the new base into *ptr. */
int kern_phys_map_ptr(phys_bytes base_address, vir_bytes io_size,
	int vm_flags, kern_phys_map *priv, vir_bytes ptr);

int kern_phys_map_mapped_ptr(vir_bytes id, phys_bytes address);

/*
 * Kernel stacks, one pair of pages per CPU: the top of the upper page is the
 * stack pointer, which leaves the lower one as the guard. k_stacks_start
 * labels the reservation, which the architecture layer makes in its own
 * assembly the way the two 32-bit ports do - not in the link script, so that
 * the size can be written in terms of K_STACK_SIZE and CONFIG_MAX_CPUS.
 * k_stacks is that address rounded up to a page.
 */
EXTERN void *k_stacks_start;
extern void *k_stacks;

#define get_k_stack_top(cpu)	((void *)(((char *)(k_stacks)) \
					+ 2 * ((cpu) + 1) * K_STACK_SIZE))

/* functions defined in architecture-independent kernel source. */
#include "kernel/proto.h"

#endif /* __ASSEMBLY__ */

#endif /* _AARCH64_PROTO_H */
