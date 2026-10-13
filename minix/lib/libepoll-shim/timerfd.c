/*
 * libepoll-shim: timerfd(2).
 *
 * A deadline and an interval, kept here; the descriptor is a pipe that
 * holds one byte exactly when expirations have happened and not been
 * read.  Nothing outside the process makes a timer expire, so it expires
 * where the library gets control: in its wait (epoll_wait(), and poll(2)
 * of code that includes its headers), which is cut short at the nearest
 * deadline, and in read(2) of the timerfd itself.  A program that waits
 * on a timerfd with select(2), or with poll(2) without the headers, waits
 * in vain.
 *
 * Deadlines are kept on CLOCK_MONOTONIC; a CLOCK_REALTIME timer is
 * converted when it is set (a later change of the wall clock does not move
 * it, and TFD_TIMER_CANCEL_ON_SET is not kept).  How late a timer fires is
 * up to how poll(2) sleeps: on MINIX, to the clock tick.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "shim.h"
#include "sys/timerfd.h"

#define NS	1000000000LL

void shim_now(struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);
}

static int64_t ts_ns(const struct timespec *t)
{
	return (int64_t)t->tv_sec * NS + t->tv_nsec;
}

static struct timespec ns_ts(int64_t ns)
{
	struct timespec t;

	if (ns < 0)
		ns = 0;
	t.tv_sec = (time_t)(ns / NS);
	t.tv_nsec = (long)(ns % NS);
	return t;
}

int timerfd_create(int clock, int flags)
{
	int fd;

	if ((clock != CLOCK_MONOTONIC && clock != CLOCK_REALTIME) ||
	    (flags & ~(TFD_CLOEXEC | TFD_NONBLOCK))) {
		errno = EINVAL;
		return -1;
	}
	if ((fd = shim_new(SK_TIMERFD, flags)) < 0)
		return -1;
	shim_tab[fd].u.tm.clock = clock;
	return fd;
}

int timerfd_gettime(int fd, struct itimerspec *cur)
{
	struct shim_fd *e;
	struct timespec now;

	if ((e = shim_get(fd, SK_TIMERFD)) == NULL) {
		errno = fcntl(fd, F_GETFD) < 0 ? EBADF : EINVAL;
		return -1;
	}
	shim_timers_run();
	memset(cur, 0, sizeof(*cur));
	cur->it_interval = e->u.tm.interval;
	if (e->u.tm.armed) {
		shim_now(&now);
		cur->it_value = ns_ts(ts_ns(&e->u.tm.deadline) - ts_ns(&now));
		if (cur->it_value.tv_sec == 0 && cur->it_value.tv_nsec == 0)
			cur->it_value.tv_nsec = 1;	/* still armed */
	}
	return 0;
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new,
	struct itimerspec *old)
{
	struct shim_fd *e;
	struct timespec now, real;
	int64_t v;

	if ((e = shim_get(fd, SK_TIMERFD)) == NULL) {
		errno = fcntl(fd, F_GETFD) < 0 ? EBADF : EINVAL;
		return -1;
	}
	if (new == NULL || (flags & ~(TFD_TIMER_ABSTIME |
	    TFD_TIMER_CANCEL_ON_SET)) ||
	    new->it_value.tv_nsec < 0 || new->it_value.tv_nsec >= NS ||
	    new->it_interval.tv_nsec < 0 || new->it_interval.tv_nsec >= NS) {
		errno = EINVAL;
		return -1;
	}
	if (old != NULL && timerfd_gettime(fd, old) != 0)
		return -1;

	/* Setting a timer forgets what it had counted. */
	if (e->u.tm.expirations != 0)
		shim_drain(fd);
	e->u.tm.expirations = 0;
	e->u.tm.interval = new->it_interval;
	e->u.tm.armed = new->it_value.tv_sec != 0 || new->it_value.tv_nsec != 0;
	if (!e->u.tm.armed)
		return 0;

	shim_now(&now);
	v = ts_ns(&new->it_value);
	if (!(flags & TFD_TIMER_ABSTIME))
		v += ts_ns(&now);
	else if (e->u.tm.clock == CLOCK_REALTIME) {
		clock_gettime(CLOCK_REALTIME, &real);
		v = ts_ns(&now) + (v - ts_ns(&real));
	}
	e->u.tm.deadline = ns_ts(v < ts_ns(&now) ? ts_ns(&now) : v);
	return 0;
}

/* Expire what is due; answers whether anything did. */
int shim_timers_run(void)
{
	struct timespec now;
	struct shim_fd *e;
	int64_t t, d, iv;
	uint64_t n;
	int fd, fired = 0;

	shim_now(&now);
	t = ts_ns(&now);
	for (fd = 0; fd < SHIM_MAX_FD; fd++) {
		e = &shim_tab[fd];
		if (e->kind != SK_TIMERFD || !e->u.tm.armed)
			continue;
		d = ts_ns(&e->u.tm.deadline);
		if (t < d)
			continue;
		iv = ts_ns(&e->u.tm.interval);
		n = 1;
		if (iv > 0) {
			n += (uint64_t)((t - d) / iv);
			e->u.tm.deadline = ns_ts(d + (int64_t)n * iv);
		} else
			e->u.tm.armed = 0;
		if (e->u.tm.expirations == 0)
			shim_mark(e);
		e->u.tm.expirations += n;
		fired = 1;
	}
	return fired;
}

/* Milliseconds to the nearest deadline, rounded up; -1 if none. */
int shim_timers_next_ms(void)
{
	struct timespec now;
	struct shim_fd *e;
	int64_t t, best = -1, d;
	int fd;

	shim_now(&now);
	t = ts_ns(&now);
	for (fd = 0; fd < SHIM_MAX_FD; fd++) {
		e = &shim_tab[fd];
		if (e->kind != SK_TIMERFD || !e->u.tm.armed)
			continue;
		d = ts_ns(&e->u.tm.deadline) - t;
		if (d < 0)
			d = 0;
		if (best < 0 || d < best)
			best = d;
	}
	if (best < 0)
		return -1;
	best = (best + 999999) / 1000000;
	return best > 0x7fffffff ? 0x7fffffff : (int)best;
}

ssize_t shim_timerfd_read(int fd, struct shim_fd *e, void *buf, size_t n)
{
	uint64_t v;
	int ms;

	if (n < sizeof(v)) {
		errno = EINVAL;
		return -1;
	}
	shim_timers_run();
	while (e->u.tm.expirations == 0) {
		if (fcntl(fd, F_GETFL) & O_NONBLOCK) {
			errno = EAGAIN;
			return -1;
		}
		/* Blocking: sleep to the deadline, or for ever unarmed. */
		ms = e->u.tm.armed ? shim_timers_next_ms() : -1;
		if (poll(NULL, 0, ms) < 0 && errno == EINTR)
			return -1;
		shim_timers_run();
	}
	v = e->u.tm.expirations;
	e->u.tm.expirations = 0;
	shim_drain(fd);
	memcpy(buf, &v, sizeof(v));
	return sizeof(v);
}
