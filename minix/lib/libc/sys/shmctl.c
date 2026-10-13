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

/* Shared memory control operation. */
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	message m;
	endpoint_t ipc_pt;
	int r;

	if (get_ipc_endpt(&ipc_pt) != 0) {
		errno = ENOSYS;
		return -1;
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shmctl.id = shmid;
	m.m_lc_ipc_shmctl.cmd = cmd;
	m.m_lc_ipc_shmctl.buf = buf;

	r = _syscall(ipc_pt, IPC_SHMCTL, &m);
	if ((cmd == IPC_INFO || cmd == SHM_INFO || cmd == SHM_STAT)
		&& (r == 0))
		return m.m_lc_ipc_shmctl.ret;
	return r;
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(shmctl, __shmctl50)
#endif
