
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
#include <minix/debug.h>
#include <minix/bitmap.h>

#include <string.h>
#include <errno.h>
#include <assert.h>

#include <machine/archtypes.h>
#include <minix/timers.h>
#include "kernel/const.h"
#include "kernel/config.h"
#include "kernel/proc.h"	/* struct priv: the parent's asynsend table */

#include "glo.h"
#include "vm.h"
#include "proto.h"
#include "util.h"
#include "sanitycheck.h"
#include "region.h"

/*
 * What a fork may still need once sys_fork() is done: a copy of the
 * message page for each side, a page table for the child's, the slab pages
 * the copies are recorded in - with room to spare.
 */
#define FORK_ROOM_PAGES	8

/*===========================================================================*
 *				do_fork					     *
 *===========================================================================*/
int do_fork(message *msg)
{
  int r, proc, childproc;
  struct vmproc *vmp, *vmc;
  pt_t origpt;
  vir_bytes msgaddr;

  SANITYCHECK(SCL_FUNCTIONS);

  if(vm_isokendpt(msg->VMF_ENDPOINT, &proc) != OK) {
	printf("VM: bogus endpoint VM_FORK %d\n", msg->VMF_ENDPOINT);
  SANITYCHECK(SCL_FUNCTIONS);
	return EINVAL;
  }

  childproc = msg->VMF_SLOTNO;
  if(childproc < 0 || childproc >= NR_PROCS) {
	printf("VM: bogus slotno VM_FORK %d\n", msg->VMF_SLOTNO);
  SANITYCHECK(SCL_FUNCTIONS);
	return EINVAL;
  }

  vmp = &vmproc[proc];		/* parent */
  vmc = &vmproc[childproc];	/* child */
  assert(vmc->vm_slot == childproc);

  /*
   * PM asks; the work is the parent's. The child's arrays and page tables
   * are VM's memory, and for a user process they are had outside the
   * reserve (alloc.c, mem_charge()): a fork of a sparsely touched 4 GiB
   * costs the child up to 8 MB of page tables without one page of its own.
   */
  mem_charge(vmp);

  /* The child is basically a copy of the parent. */
  origpt = vmc->vm_pt;
  *vmc = *vmp;
  vmc->vm_slot = childproc;
  region_init(&vmc->vm_regions_avl);
  vmc->vm_endpoint = NONE;	/* In case someone tries to use it. */
  vmc->vm_pt = origpt;

#if VMSTATS
  vmc->vm_bytecopies = 0;
#endif

  if(pt_new(&vmc->vm_pt) != OK) {
	return ENOMEM;
  }

  SANITYCHECK(SCL_DETAIL);

  if(map_proc_copy(vmc, vmp) != OK) {
	if(!mem_charged_to_user())	/* else: the user's ENOMEM */
		printf("VM: fork: map_proc_copy failed\n");
	pt_free(&vmc->vm_pt);
	vmc->vm_flags = 0;	/* the slot is still free */
	return(ENOMEM);
  }

  /*
   * Past sys_fork() there is no way back, and failing to make the two
   * message pages writable below is a panic. For a user process those are
   * its pages, refused at the reserve line - so the room for them is made
   * sure of here, while the fork can still be refused.
   */
  if(mem_charged_to_user() && !mem_user_room(FORK_ROOM_PAGES * VM_PAGE_SIZE)) {
	map_free_proc(vmc);
	pt_free(&vmc->vm_pt);
	vmc->vm_flags = 0;
	return(ENOMEM);
  }

  /* Only inherit these flags. */
  vmc->vm_flags &= VMF_INUSE;

  /* Deal with ACLs. */
  acl_fork(vmc);

  /* Tell kernel about the (now successful) FORK. */
  if((r=sys_fork(vmp->vm_endpoint, childproc,
	&vmc->vm_endpoint, PFF_VMINHIBIT, &msgaddr)) != OK) {
        panic("do_fork can't sys_fork: %d", r);
  }

  if((r=pt_bind(&vmc->vm_pt, vmc)) != OK)
	panic("fork can't pt_bind: %d", r);

  {
	vir_bytes vir;
	/* making these messages writable is an optimisation
	 * and its return value needn't be checked.
	 */
	vir = msgaddr;
	if (handle_memory_once(vmc, vir, sizeof(message), 1) != OK)
	    panic("do_fork: handle_memory for child failed\n");
	vir = msgaddr;
	if (handle_memory_once(vmp, vir, sizeof(message), 1) != OK)
	    panic("do_fork: handle_memory for parent failed\n");
  }

  {
	/*
	 * The same for the parent's asynsend table, and here it is not an
	 * optimisation. The kernel writes each entry's result back into that
	 * table when the message is delivered - which can be long after this
	 * fork has made every page of the parent copy-on-write, and so
	 * read-only. The kernel cannot wait for VM on that path; the write
	 * fails, the entry is never marked done, and the same message is
	 * delivered again, forever. That is what "minix-service refresh"
	 * did: RS forks the new instance while its message to the old one is
	 * still in the table, and the system spins on "asyn failed for
	 * message entry in rs". Only a registered table (the kernel keeps
	 * s_asyntab while entries are pending) needs it.
	 */
	static struct priv ppriv;	/* 1.3 KB; VM has one thread */

	if (sys_getpriv(&ppriv, vmp->vm_endpoint) == OK &&
	    ppriv.s_asyntab != (vir_bytes)-1 && ppriv.s_asynsize > 0 &&
	    handle_memory_once(vmp, ppriv.s_asyntab,
	    ppriv.s_asynsize * sizeof(asynmsg_t), 1) != OK)
		printf("VM: fork: parent %d's asynsend table stays "
		    "copy-on-write\n", vmp->vm_endpoint);
  }

  /* Inform caller of new child endpoint. */
  msg->VMF_CHILD_ENDPOINT = vmc->vm_endpoint;

  SANITYCHECK(SCL_FUNCTIONS);
  return OK;
}

