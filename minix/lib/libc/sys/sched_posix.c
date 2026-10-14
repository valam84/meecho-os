/*
 * POSIX process scheduling, <sched.h>.  MINIX schedules processes in its
 * sched server, by queues and quanta, and offers a process none of POSIX's
 * policies: SCHED_OTHER is the one there is, at priority 0, and setting a
 * policy or a priority is ENOSYS (posix_spawn() leaves its scheduling
 * attributes out for the same reason).  Code that only tries - Mesa's
 * Panfrost asks for SCHED_FIFO for its counter thread and goes on without
 * it - finds that out here instead of failing to link.
 *
 * sched_yield() is the thread library's (libmthread: __libc_thr_yield).
 */
#include <sys/cdefs.h>

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

static int
proc_exists(pid_t pid)
{
	if (pid < 0) {
		errno = EINVAL;
		return 0;
	}
	if (pid == 0 || pid == getpid())
		return 1;
	if (kill(pid, 0) == 0 || errno == EPERM)
		return 1;
	errno = ESRCH;
	return 0;
}

int
sched_setparam(pid_t pid, const struct sched_param *param)
{
	(void)pid;
	(void)param;
	errno = ENOSYS;
	return -1;
}

int
sched_getparam(pid_t pid, struct sched_param *param)
{
	if (param == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (!proc_exists(pid))
		return -1;
	param->sched_priority = 0;
	return 0;
}

int
sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	(void)pid;
	(void)policy;
	(void)param;
	errno = ENOSYS;
	return -1;
}

int
sched_getscheduler(pid_t pid)
{
	if (!proc_exists(pid))
		return -1;
	return SCHED_OTHER;
}

int
sched_get_priority_max(int policy)
{
	if (policy != SCHED_OTHER) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
sched_get_priority_min(int policy)
{
	if (policy != SCHED_OTHER) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
