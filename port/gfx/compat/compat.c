/*
 * The rest of POSIX and C11 threading the Wayland stack asks for, for the
 * gfx sysroot only: sem_*(3) - declared by the system's <semaphore.h>,
 * implemented by nothing (NetBSD keeps them in libpthread and librt, which
 * MINIX has not) - and C11 thrd_create/thrd_join, which need a trampoline
 * between C11's int-returning start function and POSIX's.  Everything over
 * libmthread's POSIX names; it goes into the sysroot's libpthread.a with
 * libmthread itself (build-stack.sh, "sysroot").
 */
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include "threads.h"

/* NetBSD's sem_t is a pointer to this. */
struct _sem_st {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	unsigned value;
};

int
sem_init(sem_t *sem, int pshared, unsigned int value)
{
	struct _sem_st *s;

	if (pshared) {
		errno = ENOSYS;		/* no memory shared that way here */
		return -1;
	}
	if ((s = calloc(1, sizeof(*s))) == NULL) {
		errno = ENOSPC;
		return -1;
	}
	pthread_mutex_init(&s->lock, NULL);
	pthread_cond_init(&s->cond, NULL);
	s->value = value;
	*sem = s;
	return 0;
}

int
sem_destroy(sem_t *sem)
{
	struct _sem_st *s = *sem;

	pthread_cond_destroy(&s->cond);
	pthread_mutex_destroy(&s->lock);
	free(s);
	return 0;
}

int
sem_post(sem_t *sem)
{
	struct _sem_st *s = *sem;

	pthread_mutex_lock(&s->lock);
	s->value++;
	pthread_cond_signal(&s->cond);
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int
sem_wait(sem_t *sem)
{
	struct _sem_st *s = *sem;

	pthread_mutex_lock(&s->lock);
	while (s->value == 0)
		pthread_cond_wait(&s->cond, &s->lock);
	s->value--;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

int
sem_trywait(sem_t *sem)
{
	struct _sem_st *s = *sem;
	int r = 0;

	pthread_mutex_lock(&s->lock);
	if (s->value == 0) {
		errno = EAGAIN;
		r = -1;
	} else
		s->value--;
	pthread_mutex_unlock(&s->lock);
	return r;
}

int
sem_getvalue(sem_t *sem, int *v)
{
	*v = (int)(*sem)->value;
	return 0;
}

/*
 * pthread_sigmask(3), which NetBSD's <signal.h> calls __libc_thr_sigsetmask
 * and libpthread implements: with no kernel threads the process mask is
 * the thread's.  It answers an error number, not -1.
 */
int
__libc_thr_sigsetmask(int how, const sigset_t *set, sigset_t *old)
{
	return sigprocmask(how, set, old) == 0 ? 0 : errno;
}

struct start { thrd_start_t fn; void *arg; };

static void *
trampoline(void *p)
{
	struct start s = *(struct start *)p;

	free(p);
	return (void *)(intptr_t)s.fn(s.arg);
}

/*
 * libmthread gives a thread MINSIGSTKSZ, 8 KiB, unless asked for more, and
 * the threads here run fontconfig and FreeType (foot loads its four faces
 * in four).  musl gives 128 KiB, glibc megabytes; this is between.
 */
#define THRD_STACK	(256 * 1024)

int
thrd_create(thrd_t *t, thrd_start_t fn, void *arg)
{
	struct start *s;
	pthread_attr_t attr;
	pthread_t id;
	int r;

	if ((s = malloc(sizeof(*s))) == NULL)
		return thrd_nomem;
	s->fn = fn;
	s->arg = arg;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, THRD_STACK);
	r = pthread_create(&id, &attr, trampoline, s);
	pthread_attr_destroy(&attr);
	if (r != 0) {
		free(s);
		return thrd_error;
	}
	*t = THRD_FROM_PTHREAD(id);
	return thrd_success;
}

int
thrd_join(thrd_t t, int *res)
{
	void *v;

	if (pthread_join(THRD_TO_PTHREAD(t), &v) != 0)
		return thrd_error;
	if (res != NULL)
		*res = (int)(intptr_t)v;
	return thrd_success;
}
