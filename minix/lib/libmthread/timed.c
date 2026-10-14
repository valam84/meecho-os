#include <minix/mthread.h>
#include "global.h"
#include "proto.h"

/* The waits with a deadline. They are a file of their own because they read
 * the clock and sleep: the services that link this library (VFS, the block
 * drivers) wait without deadlines, and must not get clock_gettime() and
 * nanosleep() - which they have not, or should not call - through
 * mthread_cond_wait() or mthread_yield().
 */

/*===========================================================================*
 *				mthread_timed_turn			     *
 *===========================================================================*/
#define TIMED_NAP_NS	10000000	/* a clock tick at the usual 100 Hz */

int mthread_timed_turn(clockid_t clock, const struct timespec *abstime)
{
/* One turn of a wait with a deadline (mutex.c, condition.c): ETIMEDOUT once
 * the deadline has passed, otherwise let the other threads run and return 0.
 *
 * Such a waiter is not suspended but stays runnable: nobody would wake a
 * suspended thread when its time is up - the threads are the process's own,
 * and only another thread's call into this library switches between them.
 * When no other thread can run, nothing but the clock can end the wait, and
 * the whole process sleeps a little, up to a tick, before looking again.
 */
  struct timespec now, nap;

  if (abstime == NULL || abstime->tv_nsec < 0 ||
      abstime->tv_nsec >= 1000000000L)
	return(EINVAL);
  if (clock_gettime(clock, &now) != 0)
	return(EINVAL);
  if (now.tv_sec > abstime->tv_sec ||
      (now.tv_sec == abstime->tv_sec && now.tv_nsec >= abstime->tv_nsec))
	return(ETIMEDOUT);

  if (mthread_yield() == 0)
	return(0);		/* Others ran; look at the clock again. */

  nap.tv_sec = abstime->tv_sec - now.tv_sec;
  nap.tv_nsec = abstime->tv_nsec - now.tv_nsec;
  if (nap.tv_nsec < 0) {
	nap.tv_sec--;
	nap.tv_nsec += 1000000000L;
  }
  if (nap.tv_sec > 0 || nap.tv_nsec > TIMED_NAP_NS) {
	nap.tv_sec = 0;
	nap.tv_nsec = TIMED_NAP_NS;
  }
  (void) nanosleep(&nap, NULL);
  return(0);
}


/*===========================================================================*
 *				mthread_mutex_timedlock			     *
 *===========================================================================*/
int mthread_mutex_timedlock(mutex, abstime)
mthread_mutex_t *mutex;	/* Mutex that is to be locked */
const struct timespec *abstime;	/* ... by this CLOCK_REALTIME time */
{
/* Lock this mutex, or give up with ETIMEDOUT at the deadline. The waiter does
 * not queue on the mutex: it takes turns until the mutex is free or the time
 * is up (see mthread_timed_turn). */
  int r;

  for (;;) {
	if ((r = mthread_mutex_trylock(mutex)) != EBUSY)
		return(r);
	if ((r = mthread_timed_turn(CLOCK_REALTIME, abstime)) != 0)
		return(r);
  }
}


/*===========================================================================*
 *				mthread_cond_timedwait			     *
 *===========================================================================*/
int mthread_cond_timedwait(cond, mutex, abstime)
mthread_cond_t *cond;
mthread_mutex_t *mutex;
const struct timespec *abstime;	/* Deadline, in the condition's clock */
{
/* Wait for a condition to be signaled, or give up with ETIMEDOUT at the
 * deadline. The waiter is not suspended but takes turns until it is signalled
 * or its time is up (see mthread_timed_turn); either way it has the mutex
 * again when it returns. */
  mthread_tcb_t *tcb;
  struct __mthread_cond *c;
  struct __mthread_mutex *m;
  unsigned int count;
  int r;

  if (cond == NULL || mutex == NULL || abstime == NULL)
	return(EINVAL);

  c = (struct __mthread_cond *) *cond;
  m = (struct __mthread_mutex *) *mutex;

  if (!mthread_cond_valid(cond) || !mthread_mutex_valid(mutex))
	return(EINVAL);
  if (m->mm_owner != current_thread)
	return(EPERM);

  c->mc_mutex = m;
  count = m->mm_count;
  m->mm_count = 0;
  if (mthread_mutex_unlock(mutex) != 0)
	return(EPERM);

  tcb = mthread_find_tcb(current_thread);
  tcb->m_cond = c;
  tcb->m_woken = 0;
  tcb->m_timed = 1;
  do {
	r = mthread_timed_turn(c->mc_clock, abstime);
  } while (r == 0 && !tcb->m_woken);
  tcb->m_timed = 0;
  tcb->m_woken = 0;
  tcb->m_cond = NULL;
  c->mc_mutex = NULL;

  if (mthread_mutex_lock(mutex) != 0)
	return(EINVAL);
  m->mm_count = count;

  return(r);
}
