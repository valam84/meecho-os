/*
 * Built without _SYSTEM on purpose: with it <errno.h> makes every code
 * negative (the servers' way) while errno in a process is positive, and
 * "errno = ENOSYS" below would store -ENOSYS.  Nothing here speaks the
 * servers' codes - _syscall() has already made errno positive, and OK
 * is only 0 - so the user's view of <errno.h> is the right one.
 */
#define _MINIX_SYSTEM	1

#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <minix/rs.h>

#include <lib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static int get_ipc_endpt(endpoint_t *pt)
{
	return minix_rs_lookup("ipc", pt);
}

/* Attach shared memory segment. */
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
	message m;
	endpoint_t ipc_pt;
	int r;

	if (get_ipc_endpt(&ipc_pt) != 0) {
		errno = ENOSYS;
		return (void *) -1;	/* shmat(2)'s failure; NULL is "attached" */
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shmat.id = shmid;
	m.m_lc_ipc_shmat.addr = shmaddr;
	m.m_lc_ipc_shmat.flag = shmflg;

	r = _syscall(ipc_pt, IPC_SHMAT, &m);
	if (r != 0)
		return (void *) -1;
	return m.m_lc_ipc_shmat.retaddr;
}

/* Deattach shared memory segment. */
int shmdt(const void *shmaddr)
{
	message m;
	endpoint_t ipc_pt;

	if (get_ipc_endpt(&ipc_pt) != 0) {
		errno = ENOSYS;
		return -1;
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shmdt.addr = shmaddr;

	return _syscall(ipc_pt, IPC_SHMDT, &m);
}

