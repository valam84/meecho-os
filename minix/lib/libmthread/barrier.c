#include <minix/mthread.h>
#include "global.h"
#include "proto.h"

/* A barrier: a mutex, a condition and a count of the threads still to come.
 * The cycle number tells a waiter its barrier has tripped, so that one that
 * wakes for another reason - the main thread does, when nothing else can
 * run - waits again, and one already gone through is not held up by the
 * threads of the next round. */
struct __mthread_barrier {
  mthread_mutex_t mb_mutex;
  mthread_cond_t mb_cond;
  unsigned int mb_count;	/* Threads it takes to trip it */
  unsigned int mb_waiting;	/* ... of which here now */
  unsigned int mb_cycle;	/* Times it has tripped */
};

/*===========================================================================*
 *				mthread_barrier_init			     *
 *===========================================================================*/
int mthread_barrier_init(barrier, battr, count)
mthread_barrier_t *barrier;
const mthread_barrierattr_t *battr;	/* Nothing to say; ignored */
unsigned int count;
{
  struct __mthread_barrier *b;
  int r;

  (void) battr;
  if (barrier == NULL || count == 0)
	return(EINVAL);
  if ((b = malloc(sizeof(*b))) == NULL)
	return(ENOMEM);
  if ((r = mthread_mutex_init(&b->mb_mutex, NULL)) != 0) {
	free(b);
	return(r);
  }
  if ((r = mthread_cond_init(&b->mb_cond, NULL)) != 0) {
	mthread_mutex_destroy(&b->mb_mutex);
	free(b);
	return(r);
  }
  b->mb_count = count;
  b->mb_waiting = 0;
  b->mb_cycle = 0;
  *barrier = b;
  return(0);
}

/*===========================================================================*
 *				mthread_barrier_destroy			     *
 *===========================================================================*/
int mthread_barrier_destroy(barrier)
mthread_barrier_t *barrier;
{
  struct __mthread_barrier *b;

  if (barrier == NULL || (b = *barrier) == NULL)
	return(EINVAL);
  if (b->mb_waiting > 0)
	return(EBUSY);
  mthread_cond_destroy(&b->mb_cond);
  mthread_mutex_destroy(&b->mb_mutex);
  free(b);
  *barrier = NULL;
  return(0);
}

/*===========================================================================*
 *				mthread_barrier_wait			     *
 *===========================================================================*/
int mthread_barrier_wait(barrier)
mthread_barrier_t *barrier;
{
/* Wait until 'count' threads have come. The last to come returns
 * MTHREAD_BARRIER_SERIAL_THREAD, the others 0. */
  struct __mthread_barrier *b;
  unsigned int cycle;

  if (barrier == NULL || (b = *barrier) == NULL)
	return(EINVAL);

  mthread_mutex_lock(&b->mb_mutex);
  cycle = b->mb_cycle;
  if (++b->mb_waiting == b->mb_count) {
	b->mb_cycle++;
	b->mb_waiting = 0;
	mthread_cond_broadcast(&b->mb_cond);
	mthread_mutex_unlock(&b->mb_mutex);
	return(MTHREAD_BARRIER_SERIAL_THREAD);
  }
  while (cycle == b->mb_cycle)
	mthread_cond_wait(&b->mb_cond, &b->mb_mutex);
  mthread_mutex_unlock(&b->mb_mutex);
  return(0);
}

/*===========================================================================*
 *				mthread_barrierattr_*			     *
 *===========================================================================*/
int mthread_barrierattr_init(battr)
mthread_barrierattr_t *battr;
{
  if (battr == NULL)
	return(EINVAL);
  *battr = NULL;
  return(0);
}

int mthread_barrierattr_destroy(battr)
mthread_barrierattr_t *battr;
{
  return(battr == NULL ? EINVAL : 0);
}

/* pthread compatibility layer. */
__weak_alias(pthread_barrier_destroy, mthread_barrier_destroy)
__weak_alias(pthread_barrier_init, mthread_barrier_init)
__weak_alias(pthread_barrier_wait, mthread_barrier_wait)
__weak_alias(pthread_barrierattr_destroy, mthread_barrierattr_destroy)
__weak_alias(pthread_barrierattr_init, mthread_barrierattr_init)
