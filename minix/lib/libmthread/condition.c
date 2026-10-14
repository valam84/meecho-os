#include <minix/mthread.h>
#include <stdint.h>
#include "global.h"
#include "proto.h"

#ifdef MTHREAD_STRICT
static struct __mthread_cond *vc_front, *vc_rear;
static void mthread_cond_add(mthread_cond_t *c);
static void mthread_cond_remove(mthread_cond_t *c);
#else
# define mthread_cond_add(c)		((*c)->mc_magic = MTHREAD_INIT_MAGIC)
# define mthread_cond_remove(c)		((*c)->mc_magic = MTHREAD_NOT_INUSE)
#endif
#define MAIN_COND mainthread.m_cond

/* Is this thread waiting on condition 'c'? Suspended in mthread_cond_wait, or
 * taking turns in mthread_cond_timedwait and not yet signalled. */
#define waits_on(tcb, c) ((tcb)->m_cond == (c) && \
	((tcb)->m_state == MS_CONDITION || ((tcb)->m_timed && !(tcb)->m_woken)))

static void cond_wake(mthread_thread_t t, mthread_tcb_t *tcb);

/*===========================================================================*
 *				mthread_init_valid_conditions		     *
 *===========================================================================*/
void mthread_init_valid_conditions(void)
{
#ifdef MTHREAD_STRICT
/* Initialize condition variable list */
  vc_front = vc_rear = NULL;
#endif
}


/*===========================================================================*
 *				mthread_cond_add			     *
 *===========================================================================*/
#ifdef MTHREAD_STRICT
static void mthread_cond_add(c) 
mthread_cond_t *c;
{
/* Add condition to list of valid, initialized conditions */

  if (vc_front == NULL) {	/* Empty list */
  	vc_front = *c;
  	(*c)->mc_prev = NULL;
  } else {
  	vc_rear->mc_next = *c;
  	(*c)->mc_prev = vc_rear;
  }

  (*c)->mc_next = NULL;
  vc_rear = *c;
}
#endif

/*===========================================================================*
 *				mthread_cond_broadcast			     *
 *===========================================================================*/
int mthread_cond_broadcast(cond)
mthread_cond_t *cond;
{
/* Signal all threads waiting for condition 'cond'. */
  mthread_thread_t t;
  mthread_tcb_t *tcb;

  if (cond == NULL) 
  	return(EINVAL);
  else if (!mthread_cond_valid(cond))
  	return(EINVAL);

  tcb = mthread_find_tcb(MAIN_THREAD);
  if (waits_on(tcb, *cond))
  	cond_wake(MAIN_THREAD, tcb);

  for (t = (mthread_thread_t) 0; t < no_threads; t++) {
  	tcb = mthread_find_tcb(t);
	if (waits_on(tcb, *cond))
		cond_wake(t, tcb);
  }

  return(0);
}


/*===========================================================================*
 *				mthread_cond_destroy			     *
 *===========================================================================*/
int mthread_cond_destroy(cond)
mthread_cond_t *cond;
{
/* Destroy a condition variable. Make sure it's not in use */
  mthread_thread_t t;
  mthread_tcb_t *tcb;

  if (cond == NULL)
  	return(EINVAL);
  else if (!mthread_cond_valid(cond))
  	return(EINVAL);

  /* Is another thread currently using this condition variable? A timed
   * waiter uses it until it has left, signalled or not. */
  tcb = mthread_find_tcb(MAIN_THREAD);
  if (tcb->m_cond == *cond && (tcb->m_state == MS_CONDITION || tcb->m_timed))
  	return(EBUSY);

  for (t = (mthread_thread_t) 0; t < no_threads; t++) {
  	tcb = mthread_find_tcb(t);
	if (tcb->m_cond == *cond &&
	    (tcb->m_state == MS_CONDITION || tcb->m_timed))
		return(EBUSY);
  }

  /* Not in use; invalidate it. */
  mthread_cond_remove(cond);
  free(*cond);
  *cond = NULL;

  return(0);
}


/*===========================================================================*
 *				mthread_cond_init			     *
 *===========================================================================*/
int mthread_cond_init(cond, cattr)
mthread_cond_t *cond;
mthread_condattr_t *cattr;
{
/* Initialize condition variable to a known state. The one attribute is the
 * clock of its timed waits. */
  struct __mthread_cond *c;
  clockid_t clock = CLOCK_REALTIME;

  if (cattr != NULL)
	clock = (clockid_t) (intptr_t) *cattr;

  if (cond == NULL)
	return(EINVAL);
  else if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
  	return(EINVAL);

#ifdef MTHREAD_STRICT
  else if (mthread_cond_valid(cond)) 
	/* Already initialized */
  	return(EBUSY);
#endif
  else if ((c = malloc(sizeof(struct __mthread_cond))) == NULL) 
  	return(ENOMEM);

  c->mc_mutex = NULL;
  c->mc_clock = clock;
  *cond = (mthread_cond_t) c;
  mthread_cond_add(cond);

  return(0);
}


/*===========================================================================*
 *				mthread_cond_remove			     *
 *===========================================================================*/
#ifdef MTHREAD_STRICT
static void mthread_cond_remove(c)
mthread_cond_t *c;
{
/* Remove condition from list of valid, initialized conditions */

  if ((*c)->mc_prev == NULL)
  	vc_front = (*c)->mc_next;
  else
  	(*c)->mc_prev->mc_next = (*c)->mc_next;

  if ((*c)->mc_next == NULL)
  	vc_rear = (*c)->mc_prev;
  else
  	(*c)->mc_next->mc_prev = (*c)->mc_prev;

}
#endif

/*===========================================================================*
 *				mthread_cond_signal			     *
 *===========================================================================*/
int mthread_cond_signal(cond)
mthread_cond_t *cond;
{
/* Signal a thread that condition 'cond' was met. Just a single thread. */
  mthread_thread_t t;
  mthread_tcb_t *tcb;

  if (cond == NULL)
	return(EINVAL);
  else if (!mthread_cond_valid(cond))
	return(EINVAL);

  tcb = mthread_find_tcb(MAIN_THREAD);
  if (waits_on(tcb, *cond))
  	cond_wake(MAIN_THREAD, tcb);

  for (t = (mthread_thread_t) 0; t < no_threads; t++) {
  	tcb = mthread_find_tcb(t);
	if (waits_on(tcb, *cond)) {
		cond_wake(t, tcb);
		break;
	}
  }

  return(0);
}


/*===========================================================================*
 *				cond_wake				     *
 *===========================================================================*/
static void cond_wake(t, tcb)
mthread_thread_t t;
mthread_tcb_t *tcb;
{
/* A waiter is signalled: a suspended one is made runnable, a timed one -
 * runnable already - finds out on its next turn. */

  if (tcb->m_timed)
	tcb->m_woken = 1;
  else
	mthread_unsuspend(t);
}


/*===========================================================================*
 *				mthread_cond_valid			     *
 *===========================================================================*/
#ifdef MTHREAD_STRICT
int mthread_cond_valid(c)
mthread_cond_t *c;
{
/* Check to see if cond is on the list of valid conditions */
  struct __mthread_cond *loopitem;

  loopitem = vc_front;

  while (loopitem != NULL) {
  	if (loopitem == *c)
  		return(1);

  	loopitem = loopitem->mc_next;
  }

  return(0);
}
#endif

/*===========================================================================*
 *				mthread_cond_verify			     *
 *===========================================================================*/
#ifdef MDEBUG
int mthread_cond_verify(void)
{
/* Return true in case no condition variables are in use. */

  return(vc_front == NULL);
}
#endif


/*===========================================================================*
 *				mthread_cond_wait			     *
 *===========================================================================*/
int mthread_cond_wait(cond, mutex)
mthread_cond_t *cond;
mthread_mutex_t *mutex;
{
/* Wait for a condition to be signaled */
  mthread_tcb_t *tcb;
  struct __mthread_cond *c;
  struct __mthread_mutex *m;
  unsigned int count;

  if (cond == NULL || mutex == NULL)
	return(EINVAL);

  c = (struct __mthread_cond *) *cond;
  m = (struct __mthread_mutex *) *mutex;

  if (!mthread_cond_valid(cond) || !mthread_mutex_valid(mutex))
	return(EINVAL);
  if (m->mm_owner != current_thread)
  	return(-1);

  c->mc_mutex = m;	/* Remember we're using this mutex in a cond_wait */
  count = m->mm_count;	/* A recursive one is let go entirely */
  m->mm_count = 0;
  if (mthread_mutex_unlock(mutex) != 0) /* Fails when we're not the owner */
  	return(-1);

  tcb = mthread_find_tcb(current_thread);
  tcb->m_cond = c; /* Register condition variable. */
  mthread_suspend(MS_CONDITION);

  /* When execution returns here, the condition was met. Lock mutex again. */
  c->mc_mutex = NULL;				/* Forget about this mutex */
  tcb->m_cond = NULL;				/* ... and condition var */
  if (mthread_mutex_lock(mutex) != 0)
  	return(-1);
  m->mm_count = count;

  return(0);
}


/*===========================================================================*
 *				mthread_condattr_*			     *
 *===========================================================================*/
int mthread_condattr_init(cattr)
mthread_condattr_t *cattr;
{
  if (cattr == NULL)
	return(EINVAL);
  *cattr = (mthread_condattr_t) (intptr_t) CLOCK_REALTIME;
  return(0);
}

int mthread_condattr_destroy(cattr)
mthread_condattr_t *cattr;
{
  return(cattr == NULL ? EINVAL : 0);
}

int mthread_condattr_setclock(cattr, clock)
mthread_condattr_t *cattr;
clockid_t clock;
{
  if (cattr == NULL || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC))
	return(EINVAL);
  *cattr = (mthread_condattr_t) (intptr_t) clock;
  return(0);
}

int mthread_condattr_getclock(cattr, clock)
const mthread_condattr_t *cattr;
clockid_t *clock;
{
  if (cattr == NULL || clock == NULL)
	return(EINVAL);
  *clock = (clockid_t) (intptr_t) *cattr;
  return(0);
}

/* pthread compatibility layer. */
__weak_alias(pthread_cond_init, mthread_cond_init)
__weak_alias(pthread_condattr_destroy, mthread_condattr_destroy)
__weak_alias(pthread_condattr_getclock, mthread_condattr_getclock)
__weak_alias(pthread_condattr_init, mthread_condattr_init)
__weak_alias(pthread_condattr_setclock, mthread_condattr_setclock)

