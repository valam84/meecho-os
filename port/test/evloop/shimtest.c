/*
 * shimtest - libepoll-shim against what libwayland asks of it.
 *
 * Built two ways: by the host's gcc against the library's sources
 * (run.sh, seconds, on Linux - whose own epoll is shadowed by the shim's
 * headers), and for MEECHO (qemu-shim.py), where it is the real thing.
 * Each check prints "ok" or "FAIL"; the last line is
 *
 *   SHIM checks N failed F
 */
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int checks, fails;

static void
check(const char *name, int ok, const char *fmt, ...)
{
	char why[200] = "";
	va_list ap;

	checks++;
	if (fmt) {
		va_start(ap, fmt);
		vsnprintf(why, sizeof(why), fmt, ap);
		va_end(ap);
	}
	if (ok)
		printf("ok    %s\n", name);
	else {
		fails++;
		printf("FAIL  %s: %s\n", name, why);
	}
	fflush(stdout);
}

static long
ms_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void
t_epoll(void)
{
	struct epoll_event ev, out[4];
	int ep, p[2], q[2], n;
	char c;

	ep = epoll_create1(EPOLL_CLOEXEC);
	check("epoll_create1", ep >= 0, "%s", strerror(errno));
	check("EPOLL_CLOEXEC", (fcntl(ep, F_GETFD) & FD_CLOEXEC) != 0, NULL);
	if (pipe(p) != 0 || pipe(q) != 0)
		return;

	ev.events = EPOLLIN;
	ev.data.u64 = 0x1122334455667788ULL;
	check("EPOLL_CTL_ADD", epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev) == 0,
	    "%s", strerror(errno));
	check("ADD twice is EEXIST", epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev)
	    == -1 && errno == EEXIST, "%s", strerror(errno));
	check("nothing ready: 0", epoll_wait(ep, out, 4, 0) == 0, NULL);

	write(p[1], "x", 1);
	n = epoll_wait(ep, out, 4, 1000);
	check("readable: EPOLLIN with the caller's data", n == 1 &&
	    out[0].events == EPOLLIN && out[0].data.u64 ==
	    0x1122334455667788ULL, "n %d events %#x", n,
	    n > 0 ? out[0].events : 0);
	check("level-triggered: still there", epoll_wait(ep, out, 4, 0) == 1,
	    NULL);
	read(p[0], &c, 1);
	check("read: gone", epoll_wait(ep, out, 4, 0) == 0, NULL);

	ev.events = EPOLLOUT;
	ev.data.fd = q[1];
	epoll_ctl(ep, EPOLL_CTL_ADD, q[1], &ev);
	n = epoll_wait(ep, out, 4, 0);
	check("EPOLLOUT on a pipe's writing end", n == 1 && out[0].events ==
	    EPOLLOUT && out[0].data.fd == q[1], "n %d", n);

	ev.events = EPOLLIN | EPOLLONESHOT;
	ev.data.fd = p[0];
	check("EPOLL_CTL_MOD", epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev) == 0,
	    "%s", strerror(errno));
	epoll_ctl(ep, EPOLL_CTL_DEL, q[1], NULL);
	write(p[1], "y", 1);
	check("EPOLLONESHOT: once", epoll_wait(ep, out, 4, 0) == 1, NULL);
	check("EPOLLONESHOT: and not again", epoll_wait(ep, out, 4, 0) == 0,
	    NULL);
	epoll_ctl(ep, EPOLL_CTL_MOD, p[0], &ev);
	check("re-armed by MOD", epoll_wait(ep, out, 4, 0) == 1, NULL);
	read(p[0], &c, 1);

	check("DEL", epoll_ctl(ep, EPOLL_CTL_DEL, p[0], NULL) == 0, "%s",
	    strerror(errno));
	check("DEL twice is ENOENT", epoll_ctl(ep, EPOLL_CTL_DEL, p[0], NULL)
	    == -1 && errno == ENOENT, "%s", strerror(errno));

	/* A pipe's writing end closed: the reader sees its end. */
	ev.events = EPOLLIN;
	ev.data.fd = q[0];
	epoll_ctl(ep, EPOLL_CTL_ADD, q[0], &ev);
	close(q[1]);
	n = epoll_wait(ep, out, 4, 1000);
	check("a pipe's end: readable or a hangup", n == 1 &&
	    (out[0].events & (EPOLLIN | EPOLLHUP)), "n %d events %#x", n,
	    n > 0 ? out[0].events : 0);
	close(q[0]);
	n = epoll_wait(ep, out, 4, 50);
	check("a watched descriptor closed: dropped", n == 0, "n %d", n);

	/* A socket's peer gone - a Wayland client: EPOLLHUP, as Linux says. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, q) == 0) {
		ev.events = EPOLLIN | EPOLLRDHUP;
		ev.data.fd = q[0];
		epoll_ctl(ep, EPOLL_CTL_ADD, q[0], &ev);
		write(q[1], "z", 1);
		close(q[1]);
		/*
		 * Linux says EPOLLHUP at once, data or not; the shim can tell
		 * only once the data is read (a peek that finds nothing).
		 * Either way the data comes first.
		 */
		n = epoll_wait(ep, out, 4, 1000);
		check("data before the hangup: EPOLLIN",
		    n == 1 && (out[0].events & EPOLLIN), "n %d events %#x", n,
		    n > 0 ? out[0].events : 0);
		read(q[0], &c, 1);
		n = epoll_wait(ep, out, 4, 1000);
		check("then EPOLLHUP and EPOLLRDHUP when the peer goes",
		    n == 1 && (out[0].events & EPOLLHUP) &&
		    (out[0].events & EPOLLRDHUP), "n %d events %#x", n,
		    n > 0 ? out[0].events : 0);
		epoll_ctl(ep, EPOLL_CTL_DEL, q[0], NULL);
		close(q[0]);
	}

	check("epoll_ctl on a non-epoll fd is EINVAL",
	    epoll_ctl(p[0], EPOLL_CTL_ADD, p[1], &ev) == -1 &&
	    errno == EINVAL, "%s", strerror(errno));
	close(p[0]);
	close(p[1]);
	close(ep);
}

static void
t_eventfd(void)
{
	struct epoll_event ev, out[2];
	eventfd_t v;
	int ep, fd;

	fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	check("eventfd", fd >= 0, "%s", strerror(errno));
	check("empty: EAGAIN", eventfd_read(fd, &v) == -1 && errno == EAGAIN,
	    "%s", strerror(errno));
	ep = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
	check("empty: not ready", epoll_wait(ep, out, 2, 0) == 0, NULL);

	/* As wl_display_terminate() does it: write(2) on the descriptor. */
	v = 3;
	check("write(2) adds", write(fd, &v, sizeof(v)) == sizeof(v), "%s",
	    strerror(errno));
	eventfd_write(fd, 4);
	check("ready when not zero", epoll_wait(ep, out, 2, 0) == 1, NULL);
	check("read(2) takes it all: 7", read(fd, &v, sizeof(v)) == sizeof(v)
	    && v == 7, "v %llu", (unsigned long long)v);
	check("and it is empty again", epoll_wait(ep, out, 2, 0) == 0, NULL);
	check("a short read is EINVAL", read(fd, &v, 4) == -1 &&
	    errno == EINVAL, "%s", strerror(errno));
	close(fd);

	fd = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
	check("EFD_SEMAPHORE: one at a time",
	    eventfd_read(fd, &v) == 0 && v == 1 && eventfd_read(fd, &v) == 0 &&
	    v == 1 && eventfd_read(fd, &v) == -1, NULL);
	close(fd);
	close(ep);
}

static void
t_timerfd(void)
{
	struct epoll_event ev, out[2];
	struct itimerspec its, cur;
	struct pollfd pfd;
	uint64_t v;
	long t0, dt;
	int ep, fd, n;

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	check("timerfd_create", fd >= 0, "%s", strerror(errno));
	ep = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);

	memset(&its, 0, sizeof(its));
	its.it_value.tv_nsec = 100 * 1000000;
	timerfd_settime(fd, 0, &its, NULL);
	timerfd_gettime(fd, &cur);
	check("gettime: armed, at most 100 ms left", cur.it_value.tv_sec == 0
	    && cur.it_value.tv_nsec > 0 && cur.it_value.tv_nsec <= 100000000,
	    "%ld ns", cur.it_value.tv_nsec);
	t0 = ms_now();
	n = epoll_wait(ep, out, 2, 2000);
	dt = ms_now() - t0;
	check("a 100 ms timer wakes epoll_wait", n == 1 && dt >= 95 &&
	    dt < 400, "n %d after %ld ms", n, dt);
	check("read: one expiration", read(fd, &v, sizeof(v)) == sizeof(v) &&
	    v == 1, "v %llu", (unsigned long long)v);
	check("then not ready", epoll_wait(ep, out, 2, 0) == 0, NULL);

	/* As libwayland arms its one timerfd: an absolute deadline. */
	clock_gettime(CLOCK_MONOTONIC, &its.it_value);
	its.it_value.tv_nsec += 50 * 1000000;
	if (its.it_value.tv_nsec >= 1000000000) {
		its.it_value.tv_sec++;
		its.it_value.tv_nsec -= 1000000000;
	}
	timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, NULL);
	t0 = ms_now();
	n = epoll_wait(ep, out, 2, 2000);
	dt = ms_now() - t0;
	check("TFD_TIMER_ABSTIME", n == 1 && dt < 300, "n %d after %ld ms",
	    n, dt);
	read(fd, &v, sizeof(v));

	/* An interval, counted while nobody looked. */
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 20 * 1000000;
	its.it_interval = its.it_value;
	timerfd_settime(fd, 0, &its, NULL);
	epoll_wait(ep, out, 2, 0);
	usleep(210000);
	n = read(fd, &v, sizeof(v));
	check("an interval counts every expiration (210 ms of 20)",
	    n == sizeof(v) && v >= 8 && v <= 11, "v %llu",
	    (unsigned long long)v);

	memset(&its, 0, sizeof(its));
	timerfd_settime(fd, 0, &its, NULL);
	check("disarmed: nothing for 150 ms", epoll_wait(ep, out, 2, 150) ==
	    0, NULL);

	/* poll(2) of code with the headers expires timers too. */
	its.it_value.tv_nsec = 60 * 1000000;
	timerfd_settime(fd, 0, &its, NULL);
	pfd.fd = fd;
	pfd.events = POLLIN;
	t0 = ms_now();
	n = poll(&pfd, 1, -1);
	dt = ms_now() - t0;
	check("poll(2) with no timeout wakes for the timer", n == 1 &&
	    dt >= 55 && dt < 400, "n %d after %ld ms", n, dt);
	close(fd);
	close(ep);
}

static void
t_signalfd(void)
{
	struct epoll_event ev, out[2];
	struct signalfd_siginfo si;
	sigset_t mask;
	long t0, dt;
	pid_t pid;
	int ep, fd, n, status;

	/* As wl_event_loop_add_signal(): block, then signalfd. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	check("signalfd", fd >= 0, "%s", strerror(errno));
	ep = epoll_create1(0);
	ev.events = EPOLLIN;
	ev.data.fd = fd;
	epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);

	kill(getpid(), SIGUSR1);	/* pending: it is blocked */
	n = epoll_wait(ep, out, 2, 1000);
	check("a pending signal makes it ready", n == 1, "n %d", n);
	check("read: the record says SIGUSR1",
	    read(fd, &si, sizeof(si)) == sizeof(si) && si.ssi_signo == SIGUSR1,
	    "signo %u", si.ssi_signo);
	check("then EAGAIN", read(fd, &si, sizeof(si)) == -1 &&
	    errno == EAGAIN, "%s", strerror(errno));

	if ((pid = fork()) == 0) {
		usleep(150000);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	t0 = ms_now();
	n = epoll_wait(ep, out, 2, -1);
	dt = ms_now() - t0;
	check("a signal from another process wakes epoll_wait(-1)", n == 1 &&
	    dt < 1000, "n %d after %ld ms", n, dt);
	read(fd, &si, sizeof(si));
	waitpid(pid, &status, 0);

	sigprocmask(SIG_SETMASK, NULL, &mask);
	check("outside the wait the signal stays blocked",
	    sigismember(&mask, SIGUSR1), NULL);
	close(fd);
	close(ep);
}

static void
t_reuse(void)
{
	uint64_t v = 5;
	int fd, p[2];

	/*
	 * An eventfd closed by code that does not see the shim's macros -
	 * (close)(fd) is the real call - and its number given to a pipe:
	 * the pipe must be a pipe.
	 */
	fd = eventfd(0, EFD_NONBLOCK);
	(close)(fd);
	if (pipe(p) != 0)
		return;
	check("the number comes back", p[0] == fd || p[1] == fd,
	    "eventfd %d, pipe %d %d", fd, p[0], p[1]);
	check("and is the pipe now, not an eventfd",
	    write(p[1], &v, sizeof(v)) == sizeof(v) &&
	    read(p[0], &v, sizeof(v)) == sizeof(v) && v == 5, NULL);
	close(p[0]);
	close(p[1]);
}

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	t_epoll();
	t_eventfd();
	t_timerfd();
	t_signalfd();
	t_reuse();
	printf("SHIM checks %d failed %d\n", checks, fails);
	return fails ? 1 : 0;
}
