/*
 * pthread_sigmask(3), which NetBSD's <signal.h> calls __libc_thr_sigsetmask
 * and libpthread implements: with no kernel threads the process mask is
 * the thread's.  It answers an error number, not -1.
 */
#include <errno.h>
#include <signal.h>

int
__libc_thr_sigsetmask(int how, const sigset_t * __restrict set,
    sigset_t * __restrict old)
{
	return sigprocmask(how, set, old) == 0 ? 0 : errno;
}
