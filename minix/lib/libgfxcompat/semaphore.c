/*
 * sem_*(3): declared by the system's <semaphore.h>, implemented by nothing
 * (NetBSD keeps them in libpthread and librt, which MINIX has not).  Over
 * libmthread's POSIX names; process-private only.
 */
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

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
sem_getvalue(sem_t * __restrict sem, int * __restrict v)
{
	*v = (int)(*sem)->value;
	return 0;
}
