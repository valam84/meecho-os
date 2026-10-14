#include "syslib.h"
#include <string.h>

/*
 * Ask PM about job control on a terminal (PM_JOBCTL; the operations are in
 * <minix/callnr.h>).  Terminal drivers call this: process groups and
 * sessions are PM's, and so is a terminal's foreground process group, which
 * PM keeps with the session the terminal belongs to.  'sidp' and 'pgrpp', if
 * not NULL, receive the session of 'endpt' and that session's foreground
 * process group.  The result is PM's: OK, JOBCTL_SIGNALLED, or an error.
 */
int
jobctl(int op, endpoint_t endpt, pid_t sid, pid_t pgrp, int signo,
	pid_t *sidp, pid_t *pgrpp)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_pm_jobctl.op = op;
	m.m_lsys_pm_jobctl.endpt = endpt;
	m.m_lsys_pm_jobctl.sid = sid;
	m.m_lsys_pm_jobctl.pgrp = pgrp;
	m.m_lsys_pm_jobctl.signo = signo;

	r = _taskcall(PM_PROC_NR, PM_JOBCTL, &m);

	if (sidp != NULL)
		*sidp = m.m_pm_lsys_jobctl.sid;
	if (pgrpp != NULL)
		*pgrpp = m.m_pm_lsys_jobctl.pgrp;
	return r;
}
