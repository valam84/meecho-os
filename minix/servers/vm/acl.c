
/* Call mask ACL management. */

#include <minix/drivers.h>

#include "proto.h"
#include "glo.h"
#include "util.h"

#define NO_ACL		-1
#define USER_ACL	 0
#define FIRST_SYS_ACL	 1

static bitchunk_t acl_mask[NR_SYS_PROCS][VM_CALL_MASK_SIZE];
static bitchunk_t acl_inuse[BITMAP_CHUNKS(NR_SYS_PROCS)];

/*
 * Initialize ACL data structures.
 */
void
acl_init(void)
{
	int i;

	for (i = 0; i < ELEMENTS(vmproc); i++)
		vmproc[i].vm_acl = NO_ACL;

	memset(acl_mask, 0, sizeof(acl_mask));
	memset(acl_inuse, 0, sizeof(acl_inuse));
}

/*
 * Check whether a process is allowed to make a certain (zero-based) call.
 * Return OK or an error.
 */
int
acl_check(struct vmproc *vmp, int call)
{

	/* VM makes asynchronous calls to itself.  Always allow those. */
	if (vmp->vm_endpoint == VM_PROC_NR)
		return OK;

	/* If the process has no ACL, all calls are allowed.. for now. */
	if (vmp->vm_acl == NO_ACL) {
		/* RS instrumented with ASR may call VM_BRK at startup. */
		if (vmp->vm_endpoint == RS_PROC_NR)
			return OK;

		printf("VM: calling process %u has no ACL!\n",
		    vmp->vm_endpoint);

		return OK;
	}

	/* See if the call is allowed. */
	if (!GET_BIT(acl_mask[vmp->vm_acl], call))
		return EPERM;

	return OK;
}

/*
 * Assign a call mask to a process.  User processes share the first ACL entry.
 * System processes are assigned to any of the other slots.  For user
 * processes, no call mask need to be provided: it will simply be inherited in
 * that case.
 */
void
acl_set(struct vmproc *vmp, bitchunk_t *mask, int sys_proc)
{
	int i;

	acl_clear(vmp);

	if (sys_proc) {
		for (i = FIRST_SYS_ACL; i < NR_SYS_PROCS; i++)
			if (!GET_BIT(acl_inuse, i))
				break;

		/*
		 * This should never happen.  If it does, then different user
		 * processes have been assigned call masks separately.  It is
		 * RS's responsibility to prevent that.
		 */
		if (i == NR_SYS_PROCS) {
			printf("VM: no ACL entries available!\n");
			return;
		}
	} else
		i = USER_ACL;

	if (!GET_BIT(acl_inuse, i) && mask == NULL)
		printf("VM: WARNING: inheriting uninitialized ACL mask\n");

	SET_BIT(acl_inuse, i);
	vmp->vm_acl = i;

	if (mask != NULL)
		memcpy(&acl_mask[vmp->vm_acl], mask, sizeof(acl_mask[0]));
}

/*
 * A process has forked.  User processes inherit their parent's ACL by default,
 * although they may be turned into system processes later.  System processes
 * do not inherit an ACL, and will have to be assigned one before getting to
 * run.
 */
void
acl_fork(struct vmproc *vmp)
{
	if (vmp->vm_acl != USER_ACL)
		vmp->vm_acl = NO_ACL;
}

/*
 * A process has exited.  Decrease the reference count on its ACL entry, and
 * mark the process as having no ACL.
 */
void
acl_clear(struct vmproc *vmp)
{
	if (vmp->vm_acl != NO_ACL) {
		if (vmp->vm_acl != USER_ACL)
			UNSET_BIT(acl_inuse, vmp->vm_acl);

		vmp->vm_acl = NO_ACL;
	}
}

/*
 * Whether a process is a system process, for the memory reserve (alloc.c).
 *
 * The ACL is the one record VM has of what RS decided: a user process
 * shares USER_ACL - init gets it from RS, and its descendants inherit it
 * in acl_fork() - while a system process has an entry of its own. NO_ACL
 * is what a process has before RS has spoken for it: VM and RS themselves,
 * the boot image at startup, a service RS has forked and not yet
 * privileged. All of those are the system's, and a wrong answer there
 * would refuse memory to the very processes the reserve exists for.
 */
int
acl_is_system(struct vmproc *vmp)
{
	return vmp->vm_acl != USER_ACL;
}
