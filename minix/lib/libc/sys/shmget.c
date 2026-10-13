/*
 * Built without _SYSTEM on purpose: with it <errno.h> makes every code
 * negative (the servers' way) while errno in a process is positive, and
 * "errno = ENOSYS" below would store -ENOSYS.  Nothing here speaks the
 * servers' codes - _syscall() has already made errno positive, and OK
 * is only 0 - so the user's view of <errno.h> is the right one.
 */
#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <minix/rs.h>
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

/* Get shared memory segment. */
int shmget(key_t key, size_t size, int shmflg)
{
	message m;
	endpoint_t ipc_pt;
	int r;

	if (get_ipc_endpt(&ipc_pt) != 0) {
		errno = ENOSYS;
		return -1;
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shmget.key = key;
	m.m_lc_ipc_shmget.size = size;
	m.m_lc_ipc_shmget.flag = shmflg;

	r = _syscall(ipc_pt, IPC_SHMGET, &m);
	if (r != 0)
		return r;
	return m.m_lc_ipc_shmget.retid;
}
