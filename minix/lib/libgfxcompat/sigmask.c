/*
 * pthread_sigmask(3), which NetBSD's <signal.h> calls __libc_thr_sigsetmask
 * and libpthread implements: with no kernel threads the process mask is
 * the thread's.  It answers an error number, not -1.
 *
 * Only the main thread's mask is the process's.  A thread libmthread made
 * is not a thread to the kernel: its "own" mask would be the process's
 * mask, and the usual start of a worker - block everything, so that the
 * main thread is the one that gets the signals - blocked them for the
 * whole program, the main thread included, for good.  fuzzel's match and
 * render workers do just that, and fuzzel did not end on SIGTERM or
 * SIGINT any more (it catches neither: the default action was the way
 * out).  A worker's mask is therefore not applied, and it is told the
 * process's in return.  What blocking in a worker is for - that a handler
 * runs on the main thread - libmthread gives anyway as far as it matters:
 * a handler runs on whichever thread the process is in, between that
 * thread's own calls, and the handlers these programs install set a flag
 * or write a pipe.
 */
#include <errno.h>
#include <signal.h>

#include <minix/mthread.h>

int
__libc_thr_sigsetmask(int how, const sigset_t * __restrict set,
    sigset_t * __restrict old)
{
	if ((int)mthread_self() >= 0) {		/* not the main thread */
		if (set != NULL && how != SIG_BLOCK && how != SIG_UNBLOCK &&
		    how != SIG_SETMASK)
			return EINVAL;
		return sigprocmask(SIG_BLOCK, NULL, old) == 0 ? 0 : errno;
	}
	return sigprocmask(how, set, old) == 0 ? 0 : errno;
}
