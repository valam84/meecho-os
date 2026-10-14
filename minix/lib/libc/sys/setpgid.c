#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>

#include <unistd.h>

/*
 * MEECHO: upstream MINIX had no process groups apart from sessions, and this
 * was setsid() under another name - setpgid(0, 0) and nothing else.  PM keeps
 * process groups of their own now (PM_SETPGID), and the rules are POSIX's;
 * see minix/servers/pm/getset.c.
 */
int setpgid(pid_t pid, pid_t pgid)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_setpgid.pid = pid;
	m.m_lc_pm_setpgid.pgid = pgid;
	return(_syscall(PM_PROC_NR, PM_SETPGID, &m));
}
