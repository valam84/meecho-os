/*
 * poll(2) for threads that switch only in libmthread's calls.
 *
 * libmthread's threads are user-level: a thread in a system call is the
 * whole process in it.  A program whose threads each wait on descriptors
 * of their own - yambar, a thread per module and one for the bar; fuzzel,
 * one loading the list while another waits on the display - would stop in
 * the first thread to reach poll(2), waiting for what only another thread
 * could make ready.
 *
 * So the wait is shared.  A thread with nothing ready parks on a condition
 * variable, and one of the parked threads is the poller.  The poller lets
 * every thread that can run do so first - mthread_yield() says when none
 * can - and only then blocks: in one poll(2) on the descriptors of every
 * parked thread, for the nearest of their deadlines, after which it wakes
 * those that have something or whose time is up.  A poller that is done
 * hands the job to a parked thread that is not.  Between its turns the
 * poller looks without waiting, so that its own descriptors are seen while
 * others run.
 *
 * Only programs that start threads get any of this: thrd_create() (in
 * threads.c) marks the program threaded, libepoll-shim asks for gfx_poll()
 * weakly - linked in only beside thrd_create() - and <poll.h> here
 * redirects poll() only where it is included.  Before the first thread it
 * is poll(2) itself.
 *
 * Not covered: a thread blocking in another call (read(2) of an empty
 * pipe, sleep(3)) still blocks the process; and threads made with
 * pthread_create() directly, not thrd_create(), do not mark the program.
 */
#define GFXCOMPAT_NO_POLL	/* this file calls the real one */

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <minix/mthread.h>

struct waiter {
	struct waiter *next;
	struct pollfd *fds;
	nfds_t n;
	long deadline;			/* ms, CLOCK_MONOTONIC; -1: none */
	int result, error;
	bool done;
	mthread_cond_t cv;
};

static struct waiter *waiters;		/* every parked thread, poller too */
static struct waiter *poller;
static mthread_mutex_t lock;		/* only for the condition variables */
static bool threaded, ready;

void gfx_poll_threaded(void);
int gfx_poll(struct pollfd *, nfds_t, int);

void
gfx_poll_threaded(void)
{
	threaded = true;
}

static long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int
remaining(long deadline)
{
	long r;

	if (deadline < 0)
		return -1;
	r = deadline - now_ms();
	return r <= 0 ? 0 : r > INT_MAX ? INT_MAX : (int)r;
}

/*
 * One poll(2) for every parked thread that is not done yet - blocking, for
 * the nearest deadline, or only looking - and a wake-up for each it is
 * done with.  A failure is every waiter's: EINTR (a signal's handler ran)
 * as much as anything else.
 */
static void
poll_all(bool block)
{
	struct waiter *w;
	struct pollfd *all;
	nfds_t total = 0, k = 0, i;
	long nearest = -1;
	int r, err, cnt;

	for (w = waiters; w != NULL; w = w->next) {
		if (w->done)
			continue;
		total += w->n;
		if (w->deadline >= 0 && (nearest < 0 || w->deadline < nearest))
			nearest = w->deadline;
	}
	if ((all = malloc((total > 0 ? total : 1) * sizeof(*all))) == NULL) {
		r = -1;
		err = ENOMEM;
	} else {
		for (w = waiters; w != NULL; w = w->next)
			if (!w->done) {
				memcpy(all + k, w->fds, w->n * sizeof(*all));
				k += w->n;
			}
		r = poll(all, total, block ? remaining(nearest) : 0);
		err = errno;
	}

	k = 0;
	for (w = waiters; w != NULL; w = w->next) {
		if (w->done)
			continue;
		if (r < 0) {
			w->result = -1;
			w->error = err;
			w->done = true;
			continue;
		}
		for (i = 0, cnt = 0; i < w->n; i++, k++) {
			w->fds[i].revents = all[k].revents;
			if (all[k].revents != 0)
				cnt++;
		}
		if (cnt > 0) {
			w->result = cnt;
			w->done = true;
		} else if (w->deadline >= 0 && remaining(w->deadline) == 0) {
			w->result = 0;
			w->done = true;
		}
	}
	free(all);
	for (w = waiters; w != NULL; w = w->next)
		if (w->done)
			mthread_cond_signal(&w->cv);
}

int
gfx_poll(struct pollfd *fds, nfds_t n, int timeout)
{
	struct waiter w, **pp, *o;
	int r;

	if (!threaded)
		return poll(fds, n, timeout);
	if ((r = poll(fds, n, 0)) != 0 || timeout == 0)
		return r;
	if (!ready) {
		if (mthread_mutex_init(&lock, NULL) != 0)
			return poll(fds, n, timeout);
		ready = true;
	}

	memset(&w, 0, sizeof(w));
	w.fds = fds;
	w.n = n;
	w.deadline = timeout < 0 ? -1 : now_ms() + timeout;
	if (mthread_cond_init(&w.cv, NULL) != 0)
		return poll(fds, n, timeout);
	w.next = waiters;
	waiters = &w;

	while (!w.done) {
		if (poller == NULL)
			poller = &w;
		if (poller != &w) {
			mthread_mutex_lock(&lock);
			mthread_cond_wait(&w.cv, &lock);
			mthread_mutex_unlock(&lock);
			continue;
		}
		if (mthread_yield() == 0)	/* the others ran first */
			poll_all(false);
		else				/* nobody else can run */
			poll_all(true);
	}

	for (pp = &waiters; *pp != &w; pp = &(*pp)->next)
		;
	*pp = w.next;
	if (poller == &w) {
		poller = NULL;
		for (o = waiters; o != NULL; o = o->next)
			if (!o->done) {
				mthread_cond_signal(&o->cv);
				break;
			}
	}
	mthread_cond_destroy(&w.cv);
	if (w.result < 0)
		errno = w.error;
	return w.result;
}
