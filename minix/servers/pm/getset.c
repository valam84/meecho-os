/* This file handles the 6 system calls that get and set uids and gids.
 * It also handles getpid(), setsid(), and getpgrp().  The code for each
 * one is so tiny that it hardly seemed worthwhile to make each a separate
 * function.
 */

#include "pm.h"
#include <minix/callnr.h>
#include <minix/endpoint.h>
#include <limits.h>
#include <minix/com.h>
#include <signal.h>
#include "mproc.h"

/*===========================================================================*
 *				do_get					     *
 *===========================================================================*/
int
do_get(void)
{
/* Handle PM_GETUID, PM_GETGID, PM_GETGROUPS, PM_GETPID, PM_GETPGRP, PM_GETSID,
 * PM_ISSETUGID.
 */
  register struct mproc *rmp = mp;
  int r;
  int ngroups;

  switch(call_nr) {
	case PM_GETGROUPS:
		ngroups = m_in.m_lc_pm_groups.num;
		if (ngroups > NGROUPS_MAX || ngroups < 0)
			return(EINVAL);

		if (ngroups == 0) {
			r = rmp->mp_ngroups;
			break;
		}

		if (ngroups < rmp->mp_ngroups)
			/* Asking for less groups than available */
			return(EINVAL);

		r = sys_datacopy(SELF, (vir_bytes) rmp->mp_sgroups, who_e,
			m_in.m_lc_pm_groups.ptr, ngroups * sizeof(gid_t));

		if (r != OK)
			return(r);

		r = rmp->mp_ngroups;
		break;
	case PM_GETUID:
		r = rmp->mp_realuid;
		rmp->mp_reply.m_pm_lc_getuid.euid = rmp->mp_effuid;
		break;

	case PM_GETGID:
		r = rmp->mp_realgid;
		rmp->mp_reply.m_pm_lc_getgid.egid = rmp->mp_effgid;
		break;

	case PM_GETPID:
		r = mproc[who_p].mp_pid;
		rmp->mp_reply.m_pm_lc_getpid.parent_pid = mproc[rmp->mp_parent].mp_pid;
		break;

	case PM_GETPGRP:
	case PM_GETSID:
	{
		/* getpgrp() sends a zeroed message, so a pid of 0 is the
		 * caller; getpgid(pid) and getsid(pid) name a process.
		 */
		struct mproc *target;
		pid_t p = m_in.m_lc_pm_getsid.pid;
		target = p ? find_proc(p) : &mproc[who_p];
		r = ESRCH;
		if(target)
			r = (call_nr == PM_GETPGRP) ? target->mp_procgrp :
			    target->mp_session;
		break;
	}
	case PM_ISSETUGID:
		r = !!(rmp->mp_flags & TAINTED);
		break;

	default:
		r = EINVAL;
		break;
  }
  return(r);
}

/*===========================================================================*
 *				do_set					     *
 *===========================================================================*/
int
do_set(void)
{
/* Handle PM_SETUID, PM_SETEUID, PM_SETGID, PM_SETGROUPS, PM_SETEGID, and
 * SETSID. These calls have in common that, if successful, they will be
 * forwarded to VFS as well.
 */
  register struct mproc *rmp = mp;
  message m;
  int r, i;
  int ngroups;
  uid_t uid;
  gid_t gid;

  memset(&m, 0, sizeof(m));

  switch(call_nr) {
	case PM_SETUID:
		uid = m_in.m_lc_pm_setuid.uid;
		/* NetBSD specific semantics: setuid(geteuid()) may fail. */
		if (rmp->mp_realuid != uid && rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		/* BSD semantics: always update all three fields. */
		rmp->mp_realuid = uid;
		rmp->mp_effuid = uid;
		rmp->mp_svuid = uid;

		m.m_type = VFS_PM_SETUID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effuid;
		m.VFS_PM_RID = rmp->mp_realuid;

		break;

	case PM_SETEUID:
		uid = m_in.m_lc_pm_setuid.uid;
		/* BSD semantics: seteuid(geteuid()) may fail. */
		if (rmp->mp_realuid != uid && rmp->mp_svuid != uid &&
		    rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_effuid = uid;

		m.m_type = VFS_PM_SETUID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effuid;
		m.VFS_PM_RID = rmp->mp_realuid;

		break;

	case PM_SETGID:
		gid = m_in.m_lc_pm_setgid.gid;
		if (rmp->mp_realgid != gid && rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_realgid = gid;
		rmp->mp_effgid = gid;
		rmp->mp_svgid = gid;

		m.m_type = VFS_PM_SETGID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effgid;
		m.VFS_PM_RID = rmp->mp_realgid;

		break;

	case PM_SETEGID:
		gid = m_in.m_lc_pm_setgid.gid;
		if (rmp->mp_realgid != gid && rmp->mp_svgid != gid &&
		    rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_effgid = gid;

		m.m_type = VFS_PM_SETGID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effgid;
		m.VFS_PM_RID = rmp->mp_realgid;

		break;

	case PM_SETGROUPS:
		if (rmp->mp_effuid != SUPER_USER)
			return(EPERM);

		ngroups = m_in.m_lc_pm_groups.num;

		if (ngroups > NGROUPS_MAX || ngroups < 0)
			return(EINVAL);

		if (ngroups > 0 && m_in.m_lc_pm_groups.ptr == 0)
			return(EFAULT);

		r = sys_datacopy(who_e, m_in.m_lc_pm_groups.ptr, SELF,
			     (vir_bytes) rmp->mp_sgroups,
			     ngroups * sizeof(gid_t));
		if (r != OK)
			return(r);

		for (i = 0; i < ngroups; i++) {
			if (rmp->mp_sgroups[i] > GID_MAX)
				return(EINVAL);
		}
		for (i = ngroups; i < NGROUPS_MAX; i++) {
			rmp->mp_sgroups[i] = 0;
		}
		rmp->mp_ngroups = ngroups;

		m.m_type = VFS_PM_SETGROUPS;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_GROUP_NO = rmp->mp_ngroups;
		m.VFS_PM_GROUP_ADDR = (char *) rmp->mp_sgroups;

		break;
	case PM_SETSID:
		/* POSIX: not for a process group leader, and not for a
		 * process whose pid is another process's process group: the
		 * new session's one group would not be its only one.  A
		 * process its parent has waited for is no member any more,
		 * even while its slot waits for VFS.
		 */
		if (rmp->mp_procgrp == rmp->mp_pid) return(EPERM);
		for (i = 0; i < NR_PROCS; i++) {
			if ((mproc[i].mp_flags & (IN_USE | TOLD_PARENT)) ==
			    IN_USE && mproc[i].mp_procgrp == rmp->mp_pid)
				return(EPERM);
		}
		rmp->mp_session = rmp->mp_procgrp = rmp->mp_pid;
		rmp->mp_fgpgrp = 0;

		m.m_type = VFS_PM_SETSID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;

		break;

	default:
		return(EINVAL);
  }

  /* Send the request to VFS */
  tell_vfs(rmp, &m);

  /* Do not reply until VFS has processed the request */
  return(SUSPEND);
}

/*===========================================================================*
 *				do_setpgid				     *
 *===========================================================================*/
int
do_setpgid(void)
{
/* Perform setpgid(pid, pgid): put the caller, or a child of it that has not
 * yet exec()ed, into a process group of its own session - an existing one, or
 * a new one named after it.  Upstream MINIX had no such call: a process group
 * was the session, and setpgid() in libc was setsid() under another name.
 * Only PM knows of process groups, so VFS is not told.
 */
  struct mproc *rmp;
  pid_t pid, pgid;

  pid = m_in.m_lc_pm_setpgid.pid;
  pgid = m_in.m_lc_pm_setpgid.pgid;

  if (pid < 0 || pgid < 0)
	return(EINVAL);

  if (pid == 0 || pid == mp->mp_pid) {
	rmp = mp;
  } else {
	rmp = find_proc(pid);
	if (rmp == NULL || (rmp->mp_flags & EXITING) ||
	    rmp->mp_parent != who_p)
		return(ESRCH);
	if (rmp->mp_session != mp->mp_session)
		return(EPERM);
	if (rmp->mp_flags & EXEC_DONE)
		return(EACCES);
  }

  /* A session leader stays the leader of its session's first group. */
  if (rmp->mp_session == rmp->mp_pid)
	return(EPERM);

  if (pgid == 0)
	pgid = rmp->mp_pid;

  /* A new group is named after its first member; joining an existing one
   * is only possible within the session.
   */
  if (pgid != rmp->mp_pid && !pgrp_in_session(pgid, mp->mp_session))
	return(EPERM);

  rmp->mp_procgrp = pgid;
  return(OK);
}
