/*
 * cooppoll - libgfxcompat's shared poll(2) (gfx_poll(), poll.c), on MEECHO.
 *
 * libmthread's threads are user-level, so a thread in poll(2) is the whole
 * process in it.  These are the shapes in which that stops a program -
 * each the shape of a real one - and what gfx_poll() must make of them:
 *
 *   hup      libc's poll(2) itself: POLLHUP of a pipe with no writer left
 *            (yambar's script module knows a script's end by nothing else)
 *   before   poll(2) before any thread: the plain call, its timeout kept
 *   ring     three threads pass a byte round a ring of pipes, each waiting
 *            in poll() on its own; the main thread waits for the end in
 *            poll() too (yambar: a thread per module, one for the bar)
 *   timeout  a thread waits 500 ms on a pipe nobody writes while the ring
 *            runs: it must wake after its time, not before, not much after
 *   which    two threads wait on two pipes, one is written: that one wakes
 *            with POLLIN, the other sleeps on to its own time
 *   epoll    the main thread in epoll_wait() (libepoll-shim, whose wait is
 *            gfx_poll() when the program has threads) for a pipe a thread
 *            writes after waiting in poll() itself (fuzzel: the list loads
 *            in a thread while the main thread waits on the display)
 *
 * Built a second time with -DGFXCOMPAT_NO_POLL, where poll() here is the
 * plain call: port/test/gfxcompat/qemu-cooppoll.py runs both, and the
 * plain one must stop at "ring" - the test must be able to see what it
 * tests.  (libepoll-shim's wait is gfx_poll() in both: that is decided by
 * the program having threads, not by this header.)  A ring that fails ends
 * the run there: its threads would wait for ever.
 *
 *   cooppoll
 */
/* poll() here is whatever libgfxcompat's <poll.h> makes of it, not
 * libepoll-shim's; epoll_wait() is a function and needs no redirect. */
#define EPOLL_SHIM_NO_INTERPOSE
#include <sys/epoll.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

static int checks, failed;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (c)								\
		printf("ok   ");					\
	else {								\
		failed++;						\
		printf("FAIL ");					\
	}								\
	printf(__VA_ARGS__);						\
	printf("\n");							\
	fflush(stdout);							\
} while (0)

static long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int
wait_in(int fd, int timeout)
{
	struct pollfd p = { fd, POLLIN, 0 };
	int r = poll(&p, 1, timeout);

	return r == 1 && (p.revents & POLLIN) ? 1 : r;
}

/* ring: thread i waits on ring[i], passes the byte on to ring[i + 1]. */
#define NRING	3
#define ROUNDS	200
static int ring[NRING + 1][2];
static int rounds_done;

static int
ring_thread(void *arg)
{
	int i = (int)(long)arg, k;
	char c;

	for (k = 0; k < ROUNDS; k++) {
		if (wait_in(ring[i][0], 20000) != 1 ||
		    read(ring[i][0], &c, 1) != 1)
			return 1;
		if (i == NRING - 1)
			rounds_done++;
		/* The last one hands the byte back round, and the main
		 * thread its end. */
		if (write(i == NRING - 1 && k == ROUNDS - 1 ? ring[NRING][1] :
		    ring[(i + 1) % NRING][1], &c, 1) != 1)
			return 1;
	}
	return 0;
}

/* timeout and which: a thread that waits, and says how it went. */
struct waiting {
	int fd, timeout, result;
	short revents;
	long took;
};

static int
wait_thread(void *arg)
{
	struct waiting *w = arg;
	struct pollfd p = { w->fd, POLLIN, 0 };
	long t0 = now_ms();

	w->result = poll(&p, 1, w->timeout);
	w->revents = p.revents;
	w->took = now_ms() - t0;
	return 0;
}

/* epoll: waits in poll() for a while, then writes. */
static int epipe[2];

static int
writer_thread(void *arg)
{
	(void)arg;
	poll(NULL, 0, 300);		/* a wait on nothing: 300 ms */
	return write(epipe[1], "x", 1) == 1 ? 0 : 1;
}

int
main(void)
{
	struct waiting tw, wa, wb;
	struct epoll_event ev, got;
	thrd_t th[NRING], t1, t2;
	long t0, took;
	int i, r, ep, res;
	char c;

	/*
	 * hup: libc's poll(2) - select(2) underneath - says POLLHUP of a pipe
	 * with no writer left, as POSIX and NetBSD do: open and empty is a
	 * timeout; data and no writer is POLLIN; drained, POLLHUP.
	 */
	{
		struct pollfd hp;
		int hu[2];

		if (pipe(hu) != 0) {
			perror("pipe");
			return 2;
		}
		hp.fd = hu[0];
		hp.events = POLLIN;
		r = poll(&hp, 1, 0);
		CHECK(r == 0, "hup: an open, empty pipe is not ready: %d", r);
		if (write(hu[1], "ab", 2) != 2)
			return 2;
		close(hu[1]);
		r = poll(&hp, 1, 0);
		CHECK(r == 1 && (hp.revents & POLLIN), "hup: data, no writer: "
		    "%d, revents 0x%x", r, hp.revents);
		if (read(hu[0], &c, 1) != 1 || read(hu[0], &c, 1) != 1)
			return 2;
		r = poll(&hp, 1, 0);
		CHECK(r == 1 && (hp.revents & POLLHUP), "hup: drained, no "
		    "writer: POLLHUP: %d, revents 0x%x", r, hp.revents);
		close(hu[0]);
	}

	/* before: no thread yet. */
	if (pipe(epipe) != 0) {
		perror("pipe");
		return 2;
	}
	t0 = now_ms();
	r = wait_in(epipe[0], 200);
	took = now_ms() - t0;
	CHECK(r == 0 && took >= 190 && took < 2000,
	    "before: no thread, a 200 ms wait: %d after %ld ms", r, took);

	/* ring, with a thread beside it that times out. */
	for (i = 0; i <= NRING; i++)
		if (pipe(ring[i]) != 0) {
			perror("pipe");
			return 2;
		}
	for (i = 0; i < NRING; i++)
		thrd_create(&th[i], ring_thread, (void *)(long)i);
	tw.fd = epipe[0];
	tw.timeout = 500;
	thrd_create(&t1, wait_thread, &tw);
	t0 = now_ms();
	if (write(ring[0][1], "r", 1) != 1)
		return 2;
	r = wait_in(ring[NRING][0], 20000);
	took = now_ms() - t0;
	CHECK(r == 1 && rounds_done == ROUNDS,
	    "ring: %d rounds of %d threads in %ld ms, the main thread woken "
	    "(%d)", rounds_done, NRING, took, r);
	if (r != 1) {
		printf("COOPPOLL %d checks, %d failed (stopped at the ring)\n",
		    checks, failed);
		fflush(stdout);
		_exit(1);
	}
	for (i = 0; i < NRING; i++)
		thrd_join(th[i], &res);
	thrd_join(t1, &res);
	CHECK(tw.result == 0 && tw.took >= 490 && tw.took < 3000,
	    "timeout: a 500 ms wait beside the ring: %d after %ld ms",
	    tw.result, tw.took);

	/* which: two waiting, one written to. */
	wa.fd = ring[0][0];
	wa.timeout = 3000;
	wb.fd = ring[1][0];
	wb.timeout = 1500;
	thrd_create(&t1, wait_thread, &wa);
	thrd_create(&t2, wait_thread, &wb);
	poll(NULL, 0, 200);		/* both parked by now */
	if (write(ring[0][1], "w", 1) != 1)
		return 2;
	thrd_join(t1, &res);
	thrd_join(t2, &res);
	CHECK(wa.result == 1 && (wa.revents & POLLIN) && wa.took < 1500,
	    "which: the one written to wakes: %d, revents 0x%x, %ld ms",
	    wa.result, wa.revents, wa.took);
	CHECK(wb.result == 0 && wb.revents == 0 && wb.took >= 1490,
	    "which: the other sleeps on to its time: %d, revents 0x%x, "
	    "%ld ms", wb.result, wb.revents, wb.took);
	(void)read(ring[0][0], &c, 1);

	/* epoll: the main thread in epoll_wait(), a thread to write. */
	if ((ep = epoll_create1(0)) < 0) {
		perror("epoll_create1");
		return 2;
	}
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = epipe[0];
	epoll_ctl(ep, EPOLL_CTL_ADD, epipe[0], &ev);
	thrd_create(&t1, writer_thread, NULL);
	t0 = now_ms();
	r = epoll_wait(ep, &got, 1, 10000);
	took = now_ms() - t0;
	thrd_join(t1, &res);
	CHECK(r == 1 && got.data.fd == epipe[0] && took >= 250 && took < 5000,
	    "epoll: epoll_wait() woken by a thread that waited first: %d "
	    "after %ld ms", r, took);

	printf("COOPPOLL %d checks, %d failed\n", checks, failed);
	return failed != 0;
}
