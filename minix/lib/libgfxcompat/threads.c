/*
 * C11 thrd_create/thrd_join over libmthread's POSIX names: a trampoline
 * between C11's int-returning start function and POSIX's.  The rest of
 * <threads.h> is inline in the header.
 *
 * The first thread also makes the program's waits shared among threads
 * (poll.c): from then on a thread in poll(2) lets the others run.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

void gfx_poll_threaded(void);

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
	gfx_poll_threaded();
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
