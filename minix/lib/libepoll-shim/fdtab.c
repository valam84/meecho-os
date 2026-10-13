/*
 * libepoll-shim: the table of the library's descriptors, the calls it
 * has to see on them (read, write, close, poll), and the one wait that
 * everything ends in.
 *
 * The wait, shim_wait(), is poll(2) with two things added.  Timers: no
 * descriptor here becomes ready by itself when a timerfd's time comes, so
 * the wait is cut short at the nearest deadline and the expired timers'
 * pipes are written then.  Signals: a signalfd's signals are blocked by the
 * program (that is how signalfd(2) is used), and are let in for the
 * duration of the wait only, so that their handler can write the record
 * the program will read.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shim.h"

struct shim_fd shim_tab[SHIM_MAX_FD];
volatile sig_atomic_t shim_signalled;

/* A new descriptor of a kind: the reading end of a fresh pipe. */
int shim_new(enum shim_kind kind, int flags)
{
	struct stat st;
	struct shim_fd *e;
	int p[2], i;

	if (pipe(p) != 0)
		return -1;
	if (p[0] >= SHIM_MAX_FD || p[1] < 0 || fstat(p[0], &st) != 0) {
		close(p[0]);
		close(p[1]);
		errno = EMFILE;
		return -1;
	}
	for (i = 0; i < 2; i++)
		if (flags & O_CLOEXEC)
			(void)fcntl(p[i], F_SETFD, FD_CLOEXEC);
	if (flags & O_NONBLOCK)
		(void)fcntl(p[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(p[1], F_SETFL, O_NONBLOCK);	/* never wait to mark */

	e = &shim_tab[p[0]];
	if (e->kind != SK_NONE)
		shim_forget(p[0]);	/* closed behind our back earlier */
	memset(e, 0, sizeof(*e));
	e->kind = kind;
	e->dev = st.st_dev;
	e->ino = st.st_ino;
	e->wr = p[1];
	return p[0];
}

/*
 * The library's entry for a descriptor, of the kind asked for (SK_NONE:
 * any), or NULL.  An entry whose pipe is no longer the descriptor - it was
 * closed without the library and the number reused - is forgotten here.
 */
struct shim_fd *shim_get(int fd, enum shim_kind kind)
{
	struct shim_fd *e;
	struct stat st;

	if (fd < 0 || fd >= SHIM_MAX_FD)
		return NULL;
	e = &shim_tab[fd];
	if (e->kind == SK_NONE)
		return NULL;
	if (fstat(fd, &st) != 0 || st.st_dev != e->dev || st.st_ino != e->ino) {
		shim_forget(fd);
		return NULL;
	}
	if (kind != SK_NONE && e->kind != kind)
		return NULL;
	return e;
}

void shim_forget(int fd)
{
	struct shim_fd *e = &shim_tab[fd];

	switch (e->kind) {
	case SK_EPOLL:
		free(e->u.ep.items);
		break;
	case SK_SIGNALFD:
		shim_signalfd_forget(e);
		break;
	default:
		break;
	}
	if (e->kind != SK_NONE)
		close(e->wr);
	memset(e, 0, sizeof(*e));
}

/* Make the descriptor readable: one byte, by the library's convention. */
void shim_mark(struct shim_fd *e)
{
	char c = 1;

	(void)write(e->wr, &c, 1);
}

/* And no longer: take the byte back. */
void shim_drain(int fd)
{
	char c;

	(void)read(fd, &c, 1);
}

static long
ms_between(const struct timespec *a, const struct timespec *b)
{
	return (long)(b->tv_sec - a->tv_sec) * 1000 +
	    (b->tv_nsec - a->tv_nsec) / 1000000;
}

/*
 * poll(2), with timers and signalfd signals; "mask", if given, is the
 * signal mask to wait with (epoll_pwait()).  Answers as poll(2) does.
 */
int shim_wait(struct pollfd *fds, nfds_t n, int timeout,
	const sigset_t *mask)
{
	struct timespec start, now;
	sigset_t sigs, cur, waitmask;
	int r, t, left, next, fired, err, i;

	shim_now(&start);
	for (;;) {
		left = timeout;
		if (timeout > 0) {
			shim_now(&now);
			left = timeout - (int)ms_between(&start, &now);
			if (left < 0)
				left = 0;
		}
		t = left;
		next = shim_timers_next_ms();
		if (next >= 0 && (t < 0 || next < t))
			t = next;

		/* The mask to wait with: the caller's, less the signalfds'. */
		shim_signals_mask(&sigs);
		sigprocmask(SIG_BLOCK, NULL, &cur);
		waitmask = mask ? *mask : cur;
		for (i = 1; i < NSIG; i++)
			if (sigismember(&sigs, i))
				sigdelset(&waitmask, i);

		shim_signalled = 0;
		sigprocmask(SIG_SETMASK, &waitmask, NULL);
		r = poll(fds, n, t);
		err = errno;
		sigprocmask(SIG_SETMASK, &cur, NULL);

		fired = shim_timers_run();

		if (r < 0 && !(err == EINTR && shim_signalled)) {
			errno = err;
			return -1;
		}
		if (r < 0)
			r = 0;		/* a signalfd's signal: look again */

		/* What the timers and signals made ready since. */
		if (fired || shim_signalled) {
			int r2 = poll(fds, n, 0);

			if (r2 >= 0)
				r = r2;
		}
		if (r != 0 || timeout == 0)
			return r;
		if (timeout > 0 && t == left)
			return 0;	/* the caller's own time is up */
		/* A timer that is not among these, or nothing: go on. */
	}
}

/*===========================================================================*
 *		the calls on the library's descriptors			     *
 *===========================================================================*/
ssize_t epoll_shim_read(int fd, void *buf, size_t n)
{
	struct shim_fd *e;

	if ((e = shim_get(fd, SK_NONE)) == NULL)
		return read(fd, buf, n);

	switch (e->kind) {
	case SK_EVENTFD:
		return shim_eventfd_read(fd, e, buf, n);
	case SK_TIMERFD:
		return shim_timerfd_read(fd, e, buf, n);
	case SK_SIGNALFD:
		return shim_signalfd_read(fd, e, buf, n);
	default:
		errno = EINVAL;
		return -1;
	}
}

ssize_t epoll_shim_write(int fd, const void *buf, size_t n)
{
	struct shim_fd *e;

	if ((e = shim_get(fd, SK_NONE)) == NULL)
		return write(fd, buf, n);

	if (e->kind == SK_EVENTFD)
		return shim_eventfd_write(e, buf, n);
	errno = EINVAL;
	return -1;
}

int epoll_shim_close(int fd)
{
	if (fd >= 0 && fd < SHIM_MAX_FD && shim_tab[fd].kind != SK_NONE)
		shim_forget(fd);
	return close(fd);
}

int epoll_shim_poll(struct pollfd *fds, nfds_t n, int timeout)
{
	sigset_t sigs;
	int i;

	/* Nothing of the library's to look after: the plain call. */
	if (shim_timers_next_ms() < 0) {
		shim_signals_mask(&sigs);
		for (i = 1; i < NSIG; i++)
			if (sigismember(&sigs, i))
				break;
		if (i == NSIG)
			return poll(fds, n, timeout);
	}
	return shim_wait(fds, n, timeout, NULL);
}
