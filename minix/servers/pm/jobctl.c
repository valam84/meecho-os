/* This file holds what job control needs of process groups and sessions, and
 * the terminal drivers' side of it.
 *
 * Upstream MINIX had sessions but no process groups apart from them: one
 * field, mp_procgrp, was both, and setpgid() in libc was setsid().  Now a
 * process has a process group (mp_procgrp) and a session (mp_session), and
 * setpgid() moves it between the groups of its session (getset.c).
 *
 * A terminal's foreground process group is kept here too, with the session
 * the terminal belongs to (mp_fgpgrp of the session leader), and not in the
 * terminal driver: every question about it - may this process read the
 * terminal, may it make that group the foreground, is its group orphaned -
 * is a question about process groups.  The driver knows the session of its
 * terminal and asks (PM_JOBCTL, <minix/callnr.h>).
 *
 * The entry points into this file are:
 *   do_jobctl:		a terminal driver's question (PM_JOBCTL)
 *   session_leader:	the live leader of a session
 *   pgrp_orphaned:	is a process group orphaned?
 *   pgrp_stopped:	has a process group a member stopped by a stop signal?
 *   pgrp_in_session:	is there a process group of that id in a session?
 *   pgrp_signal:	send a signal to every member of a process group
 */

#include "pm.h"
#include <minix/callnr.h>
#include <minix/endpoint.h>
#include <signal.h>
#include "mproc.h"

/* A process that counts for job control: alive, and not exiting. */
#define LIVE(rmp) (((rmp)->mp_flags & (IN_USE | EXITING)) == IN_USE)

/*===========================================================================*
 *				session_leader				     *
 *===========================================================================*/
struct mproc *
session_leader(pid_t sid)
{
/* The leader of session 'sid', if it is still alive.  Its slot holds what the
 * session keeps, the foreground group of its terminal.
 */
  struct mproc *rmp;

  if (sid <= 0)
	return(NULL);
  rmp = find_proc(sid);
  if (rmp == NULL || !LIVE(rmp) || rmp->mp_session != sid)
	return(NULL);
  return(rmp);
}

/*===========================================================================*
 *				pgrp_orphaned				     *
 *===========================================================================*/
int
pgrp_orphaned(pid_t pgrp)
{
/* POSIX: a process group is orphaned if the parent of every member is either
 * a member itself or not in the group's session.  Then nobody in the session
 * outside the group - a shell with job control - is there to continue it.
 */
  struct mproc *rmp, *pmp;

  for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
	if (!LIVE(rmp) || rmp->mp_procgrp != pgrp)
		continue;
	pmp = &mproc[rmp->mp_parent];
	if (LIVE(pmp) && pmp->mp_procgrp != pgrp &&
	    pmp->mp_session == rmp->mp_session)
		return(FALSE);
  }
  return(TRUE);
}

/*===========================================================================*
 *				pgrp_stopped				     *
 *===========================================================================*/
int
pgrp_stopped(pid_t pgrp)
{
/* Is any process of the process group stopped by a stop signal? */
  struct mproc *rmp;

  for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
	if (LIVE(rmp) && (rmp->mp_flags & JOB_STOPPED) &&
	    rmp->mp_procgrp == pgrp)
		return(TRUE);
  }
  return(FALSE);
}

/*===========================================================================*
 *				pgrp_in_session				     *
 *===========================================================================*/
int
pgrp_in_session(pid_t pgrp, pid_t sid)
{
/* Is there a process group 'pgrp' in session 'sid'?  A group has the session
 * of its members; setpgid() keeps them from straddling two.
 */
  struct mproc *rmp;

  if (pgrp <= 0)
	return(FALSE);
  for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
	if (LIVE(rmp) && rmp->mp_procgrp == pgrp && rmp->mp_session == sid)
		return(TRUE);
  }
  return(FALSE);
}

/*===========================================================================*
 *				pgrp_signal				     *
 *===========================================================================*/
void
pgrp_signal(pid_t pgrp, int signo, int ksig)
{
/* Send a signal to every member of a process group, on behalf of PM itself:
 * no permission check, unlike check_sig() for kill(2).  System processes
 * have no process group of a session and are left alone.  A signal may end
 * a member, and its exit may signal other groups; the loop only looks at
 * slots that are still members when it gets to them.
 */
  struct mproc *rmp;

  if (pgrp <= 0)
	return;
  for (rmp = &mproc[NR_PROCS-1]; rmp >= &mproc[0]; rmp--) {
	if (!LIVE(rmp) || rmp->mp_procgrp != pgrp ||
	    (rmp->mp_flags & PRIV_PROC))
		continue;
	sig_proc(rmp, signo, TRUE /*trace*/, ksig);
  }
}

/*===========================================================================*
 *				ttysig_blocked				     *
 *===========================================================================*/
static int
ttysig_blocked(struct mproc *rmp, int signo)
{
/* Does the process ignore or block a signal?  For SIGTTIN and SIGTTOU, that
 * decides between a signal and an answer straight away.
 */
  return(sigismember(&rmp->mp_ignore, signo) ||
	sigismember(&rmp->mp_sigmask, signo));
}

/*===========================================================================*
 *				do_jobctl				     *
 *===========================================================================*/
int
do_jobctl(void)
{
/* A terminal driver asks about the session its terminal belongs to.  'endpt'
 * is the process using the terminal, 'sid' the terminal's session (0 if the
 * terminal is nobody's controlling terminal).  The driver gets the session
 * on JOBCTL_CTTY and keeps it until it lets the terminal go.
 */
  struct mproc *rmp, *lmp;
  int op, signo, slot;
  pid_t sid, pgrp;

  /* Terminal drivers only: the answers are about other processes. */
  if (!(mp->mp_flags & PRIV_PROC))
	return(EPERM);

  op = m_in.m_lsys_pm_jobctl.op;
  sid = m_in.m_lsys_pm_jobctl.sid;
  pgrp = m_in.m_lsys_pm_jobctl.pgrp;
  signo = m_in.m_lsys_pm_jobctl.signo;

  mp->mp_reply.m_pm_lsys_jobctl.sid = 0;
  mp->mp_reply.m_pm_lsys_jobctl.pgrp = 0;

  rmp = NULL;
  if (op == JOBCTL_CTTY || op == JOBCTL_ACCESS || op == JOBCTL_SETFG ||
      op == JOBCTL_GETFG) {
	if (pm_isokendpt(m_in.m_lsys_pm_jobctl.endpt, &slot) != OK)
		return(ESRCH);
	rmp = &mproc[slot];
	if (!LIVE(rmp))
		return(ESRCH);
  }
  lmp = session_leader(sid);

  switch (op) {
  case JOBCTL_CTTY:
	/* The process makes the terminal its controlling terminal: VFS has
	 * seen to it that it leads a session without one.  Its group, the
	 * session's first, is the foreground.
	 */
	if ((lmp = session_leader(rmp->mp_session)) == NULL)
		return(EPERM);
	lmp->mp_fgpgrp = rmp->mp_procgrp;
	mp->mp_reply.m_pm_lsys_jobctl.sid = rmp->mp_session;
	mp->mp_reply.m_pm_lsys_jobctl.pgrp = lmp->mp_fgpgrp;
	return(OK);

  case JOBCTL_RELEASE:
	/* The terminal is closed for good, or taken by another session. */
	if (lmp != NULL)
		lmp->mp_fgpgrp = 0;
	return(OK);

  case JOBCTL_ACCESS:
	/* POSIX: a process in a background group of its controlling terminal
	 * that reads it gets its group SIGTTIN, and one that writes it, with
	 * TOSTOP set, SIGTTOU - unless it ignores or blocks the signal, when
	 * a read fails with EIO and a write goes ahead; and if the group is
	 * orphaned, nobody would continue it, and the call fails with EIO.
	 * The driver holds the request while the group is stopped.
	 */
	if (lmp == NULL || rmp->mp_session != sid ||
	    rmp->mp_procgrp == lmp->mp_fgpgrp ||
	    (signo != SIGTTIN && signo != SIGTTOU))
		return(OK);
	if (ttysig_blocked(rmp, signo))
		return(signo == SIGTTOU ? OK : EIO);
	if (pgrp_orphaned(rmp->mp_procgrp))
		return(EIO);
	pgrp_signal(rmp->mp_procgrp, signo, TRUE /*ksig*/);
	return(JOBCTL_SIGNALLED);

  case JOBCTL_SETFG:
	/* tcsetpgrp(): from a process of the terminal's session, to a group
	 * of that session.  A background caller is a writer, as above: it is
	 * stopped by SIGTTOU unless it ignores or blocks that.  A shell with
	 * job control ignores it, and takes the terminal back this way when
	 * its foreground job stops.
	 */
	if (lmp == NULL || rmp->mp_session != sid)
		return(ENOTTY);
	if (rmp->mp_procgrp != lmp->mp_fgpgrp &&
	    !ttysig_blocked(rmp, SIGTTOU)) {
		if (pgrp_orphaned(rmp->mp_procgrp))
			return(EIO);
		pgrp_signal(rmp->mp_procgrp, SIGTTOU, TRUE /*ksig*/);
		return(JOBCTL_SIGNALLED);
	}
	if (pgrp <= 0)
		return(EINVAL);
	if (!pgrp_in_session(pgrp, sid))
		return(EPERM);
	lmp->mp_fgpgrp = pgrp;
	return(OK);

  case JOBCTL_GETFG:
	/* tcgetpgrp() and tcgetsid(): only for the terminal's session. */
	if (lmp == NULL || rmp->mp_session != sid)
		return(ENOTTY);
	mp->mp_reply.m_pm_lsys_jobctl.sid = sid;
	mp->mp_reply.m_pm_lsys_jobctl.pgrp = lmp->mp_fgpgrp;
	return(OK);

  case JOBCTL_KILL:
	/* A signal from the terminal: INTR, QUIT, SUSP, STATUS and a new
	 * window size go to the foreground group; a hangup goes to the
	 * controlling process, the session leader, whose exit then hangs up
	 * the foreground group (forkexit.c).
	 */
	if (lmp == NULL || signo <= 0 || signo >= _NSIG)
		return(ESRCH);
	if (signo == SIGHUP)
		sig_proc(lmp, signo, TRUE /*trace*/, TRUE /*ksig*/);
	else
		pgrp_signal(lmp->mp_fgpgrp, signo, TRUE /*ksig*/);
	return(OK);

  default:
	return(EINVAL);
  }
}
