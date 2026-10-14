#define _MTHREADIFY_PTHREADS
#include <minix/mthread.h>
#include "global.h"
#include "proto.h"

/* WARNING:
 * The following works under the hypothesis that we have only green threads,
 * which implies that we have no preemption, unless explicit yield or possible
 * calls done to mthread functions.
 *
 * This has impact on the fact we do not maintain a table of currently being
 * initialized mutexes or condition variables, to prevent double initialization
 * and/or TOCTU problems. TOCTU could appear between the test against the
 * initializer value, and the actual initialization, which could lead to double
 * initialization of the same mutex AND get two threads at the same time in the
 * critical section as they both hold a (different) mutex.
 */

/*
 * MEECHO: a mutex or a condition variable of all zero bytes is a default
 * one, as the static initializer is.  libmthread's are pointers and its
 * initializer is -1, so a zeroed one reached mthread_mutex_lock() as a NULL
 * to follow; but glibc's initializer is all zeros (FreeBSD's is NULL), and
 * code written for them zeroes a structure with calloc() and locks the
 * mutex in it with no pthread_mutex_init() - Mesa's Panfrost does so with
 * its bo_map_lock, a SIGSEGV at address 0x14 on the board's first frame.
 * Only here, in the POSIX names: mthread_*() callers (the services) are
 * told NULL is not a mutex, as before.
 */
#define MUTEX_UNSET(m)	(*(m) == NULL || *(m) == PTHREAD_MUTEX_INITIALIZER)
#define COND_UNSET(c)	(*(c) == NULL || *(c) == PTHREAD_COND_INITIALIZER)


/*===========================================================================*
 *				pthread_mutex_init			     *
 *===========================================================================*/
int pthread_mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *mattr)
{
	return mthread_mutex_init(mutex, mattr);
}

/*===========================================================================*
 *				pthread_mutex_destroy			     *
 *===========================================================================*/
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	if (MUTEX_UNSET(mutex)) {
		*mutex = NULL;
		return 0;
	}

	return mthread_mutex_destroy(mutex);
}

/*===========================================================================*
 *				pthread_mutex_lock			     *
 *===========================================================================*/
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	if (MUTEX_UNSET(mutex)) {
		mthread_mutex_init(mutex, NULL);	
	}

	return mthread_mutex_lock(mutex);
}

/*===========================================================================*
 *				pthread_mutex_timedlock			     *
 *===========================================================================*/
int pthread_mutex_timedlock(pthread_mutex_t *mutex,
	const struct timespec *abstime)
{
	if (MUTEX_UNSET(mutex)) {
		mthread_mutex_init(mutex, NULL);
	}

	return mthread_mutex_timedlock(mutex, abstime);
}

/*===========================================================================*
 *				pthread_mutex_trylock			     *
 *===========================================================================*/
int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	if (MUTEX_UNSET(mutex)) {
		mthread_mutex_init(mutex, NULL);
	}

	/*
	 * MEECHO: this called itself, not mthread_mutex_trylock(), since the
	 * file was written (2014): endless recursion, which GCC makes a jump
	 * to itself.  Every trylock hung its caller in a loop with no system
	 * call in it - fuzzel's render, the first program here to try a lock.
	 */
	return mthread_mutex_trylock(mutex);
}

/*===========================================================================*
 *				pthread_mutex_unlock			     *
 *===========================================================================*/
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	if (MUTEX_UNSET(mutex)) {
		mthread_mutex_init(mutex, NULL);	
	}

	return mthread_mutex_unlock(mutex);
}

/*===========================================================================*
 *				pthread_cond_init			     *
 *===========================================================================*/
int pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *cattr)
{
	return mthread_cond_init(cond, cattr);
}

/*===========================================================================*
 *				pthread_cond_broadcast			     *
 *===========================================================================*/
int pthread_cond_broadcast(pthread_cond_t *cond)
{
	if (COND_UNSET(cond)) {
		mthread_cond_init(cond, NULL);
	}

	return mthread_cond_broadcast(cond);
}

/*===========================================================================*
 *				pthread_cond_destroy			     *
 *===========================================================================*/
int pthread_cond_destroy(pthread_cond_t *cond)
{
	if (COND_UNSET(cond)) {
		*cond = NULL;
		return 0;
	}

	return mthread_cond_destroy(cond);
}

/*===========================================================================*
 *				pthread_cond_signal			     *
 *===========================================================================*/
int pthread_cond_signal(pthread_cond_t *cond)
{
	if (COND_UNSET(cond)) {
		mthread_cond_init(cond, NULL);
	}

	return mthread_cond_signal(cond);
}

/*===========================================================================*
 *				pthread_cond_timedwait			     *
 *===========================================================================*/
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
	const struct timespec *abstime)
{
	if (COND_UNSET(cond)) {
		mthread_cond_init(cond, NULL);
	}

	return mthread_cond_timedwait(cond, mutex, abstime);
}

/*===========================================================================*
 *				pthread_cond_wait			     *
 *===========================================================================*/
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	if (COND_UNSET(cond)) {
		mthread_cond_init(cond, NULL);
	}

	return mthread_cond_wait(cond, mutex);
}

/*===========================================================================*
 *				pthread_rwlock_init			     *
 *===========================================================================*/
int pthread_rwlock_init(pthread_rwlock_t *rwlock, pthread_rwlockattr_t *UNUSED(attr))
{
	return mthread_rwlock_init(rwlock);
}

#if !defined(__weak_alias)
#error __weak_alias is required to compile the pthread compat library
#endif

