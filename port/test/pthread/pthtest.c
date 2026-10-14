/*
 * What Mesa asks of POSIX threads beyond what libmthread had (stage 11.6):
 * Mesa's C11 threads (src/c11/impl/threads_posix.c) make recursive mutexes
 * with pthread_mutexattr_settype(), lock with a deadline through
 * pthread_mutex_timedlock(), wait with one through pthread_cond_timedwait();
 * u_cnd_monotonic sets the condition's clock to CLOCK_MONOTONIC;
 * util_barrier is pthread_barrier_*; util_thread_get_time_nano() asks
 * pthread_getcpuclockid() and reads the clock.
 *
 * libmthread's threads are user-level, so a wait with a deadline cannot be
 * a sleep that someone ends: the waiter takes turns, and when nothing else
 * can run the process sleeps until the next look.  The parts:
 *
 *	pthtest mutex	types, recursion, a normal one locked again
 *	pthtest timed	timedlock: gives up at the deadline, or gets the
 *			mutex when its holder lets go
 *	pthtest cond	cond_timedwait: times out, is woken, both clocks,
 *			the main thread alone, a recursive mutex across it
 *	pthtest barrier	five threads, three rounds, one serial thread each
 *	pthtest cpu	pthread_getcpuclockid() and the CPU-time clocks
 *	pthtest names	pthread_setname_np() (NetBSD's form; Mesa names its
 *			queue threads), CLOCK_MONOTONIC_RAW (Panfrost's
 *			counters run by it)
 *	pthtest sched	the POSIX scheduling calls libc now has (Panfrost
 *			asks for SCHED_FIFO): SCHED_OTHER only, ENOSYS to set
 *	pthtest c11	<threads.h> of libgfxcompat: a recursive mtx_t
 *
 * Every check prints a line; the last line counts them.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

static int checks, failures;

static void
check(int ok, const char *what, long got, long want)
{
	checks++;
	if (!ok)
		failures++;
	printf("%s %s: %ld (want %ld)\n", ok ? "ok  " : "FAIL", what, got,
	    want);
	fflush(stdout);
}

static long
ms_since(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1000 +
	    (t.tv_nsec - t0->tv_nsec) / 1000000;
}

static void
deadline(clockid_t clock, long ms, struct timespec *ts)
{
	clock_gettime(clock, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

/* ---- mutex ---- */

static pthread_mutex_t shared;
static int other_got;

static void *
try_other(void *arg)
{
	(void)arg;
	other_got = pthread_mutex_trylock(&shared);
	if (other_got == 0)
		pthread_mutex_unlock(&shared);
	return NULL;
}

struct zeroed {
	pthread_mutex_t m;
	pthread_cond_t c;
};

static void *
try_zeroed(void *arg)
{
	struct zeroed *z = arg;

	other_got = pthread_mutex_trylock(&z->m);
	if (other_got == 0)
		pthread_mutex_unlock(&z->m);
	return NULL;
}

static void
mutex(void)
{
	pthread_mutexattr_t a;
	pthread_mutex_t m;
	pthread_t t;
	struct zeroed *z, *z2;
	int type = -1, r;

	check(pthread_mutexattr_init(&a) == 0, "mutexattr_init", 0, 0);
	pthread_mutexattr_gettype(&a, &type);
	check(type == PTHREAD_MUTEX_DEFAULT, "the default type", type,
	    PTHREAD_MUTEX_DEFAULT);
	r = pthread_mutexattr_settype(&a, 77);
	check(r == EINVAL, "a type there is not", r, EINVAL);
	r = pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	check(r == 0, "settype RECURSIVE", r, 0);
	pthread_mutexattr_gettype(&a, &type);
	check(type == PTHREAD_MUTEX_RECURSIVE, "gettype", type,
	    PTHREAD_MUTEX_RECURSIVE);

	r = pthread_mutex_init(&shared, &a);
	check(r == 0, "a recursive mutex made", r, 0);
	pthread_mutexattr_destroy(&a);
	r = pthread_mutex_lock(&shared);
	r |= pthread_mutex_lock(&shared);
	r |= pthread_mutex_trylock(&shared);
	check(r == 0, "held three times by its owner", r, 0);
	pthread_create(&t, NULL, try_other, NULL);
	pthread_join(t, NULL);
	check(other_got == EBUSY, "another thread cannot have it", other_got,
	    EBUSY);
	r = pthread_mutex_unlock(&shared);
	r |= pthread_mutex_unlock(&shared);
	check(r == 0, "let go twice", r, 0);
	pthread_create(&t, NULL, try_other, NULL);
	pthread_join(t, NULL);
	check(other_got == EBUSY, "still held once", other_got, EBUSY);
	r = pthread_mutex_unlock(&shared);
	check(r == 0, "let go the third time", r, 0);
	r = pthread_mutex_unlock(&shared);
	check(r == EPERM, "and not a fourth", r, EPERM);
	pthread_create(&t, NULL, try_other, NULL);
	pthread_join(t, NULL);
	check(other_got == 0, "free for another thread", other_got, 0);
	r = pthread_mutex_destroy(&shared);
	check(r == 0, "destroyed", r, 0);

	r = pthread_mutex_init(&m, NULL);
	r |= pthread_mutex_lock(&m);
	check(r == 0, "a normal mutex locked", r, 0);
	r = pthread_mutex_lock(&m);
	check(r == EDEADLK, "locked again by its owner", r, EDEADLK);
	pthread_mutex_unlock(&m);
	pthread_mutex_destroy(&m);

	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&m, &a);
	pthread_mutex_lock(&m);
	r = pthread_mutex_trylock(&m);
	check(r == EDEADLK, "an error-checking one tried again", r, EDEADLK);
	pthread_mutex_unlock(&m);
	pthread_mutex_destroy(&m);

	/*
	 * Zero bytes, as calloc() leaves a structure: a default mutex and a
	 * default condition, as glibc's initializers are.  Mesa's Panfrost
	 * locks such a mutex with no pthread_mutex_init() (on the board: a
	 * SIGSEGV at address 0x14 in the first frame).
	 */
	z = calloc(1, sizeof(*z));
	r = pthread_mutex_lock(&z->m);
	pthread_create(&t, NULL, try_zeroed, z);
	pthread_join(t, NULL);
	check(r == 0 && other_got == EBUSY, "a mutex of zero bytes locked, "
	    "another thread cannot have it", r, 0);
	r = pthread_mutex_unlock(&z->m);
	r |= pthread_cond_signal(&z->c);
	r |= pthread_cond_broadcast(&z->c);
	check(r == 0, "let go; a condition of zero bytes signalled", r, 0);
	z2 = calloc(1, sizeof(*z2));
	r = pthread_mutex_destroy(&z->m);
	r |= pthread_cond_destroy(&z->c);
	r |= pthread_mutex_destroy(&z2->m);
	r |= pthread_cond_destroy(&z2->c);
	check(r == 0, "both destroyed, used or not", r, 0);
	free(z);
	free(z2);
}

/* ---- timedlock ---- */

static pthread_mutex_t tm = PTHREAD_MUTEX_INITIALIZER;
static int timed_r;
static long timed_ms;
static long timed_for;
static int timed_badts;

static void *
timed_locker(void *arg)
{
	struct timespec ts, t0;

	(void)arg;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	deadline(CLOCK_REALTIME, timed_for, &ts);
	if (timed_badts)
		ts.tv_nsec = 1000000000;
	timed_r = pthread_mutex_timedlock(&tm, &ts);
	timed_ms = ms_since(&t0);
	if (timed_r == 0)
		pthread_mutex_unlock(&tm);
	return NULL;
}

static void
timed(void)
{
	struct timespec ts;
	pthread_t t;
	int i, r;

	/* The static initializer, through timedlock. */
	deadline(CLOCK_REALTIME, 1000, &ts);
	r = pthread_mutex_timedlock(&tm, &ts);
	check(r == 0, "timedlock of a free, statically made mutex", r, 0);

	/* Held throughout: the other thread gives up at its deadline. */
	timed_for = 300;
	pthread_create(&t, NULL, timed_locker, NULL);
	pthread_join(t, NULL);
	check(timed_r == ETIMEDOUT, "timedlock of a held mutex", timed_r,
	    ETIMEDOUT);
	check(timed_ms >= 290 && timed_ms < 1500, "... gave up at the deadline "
	    "(ms)", timed_ms, 300);

	/* Let go after a few turns: the other thread gets it, long before
	 * its deadline. */
	timed_for = 5000;
	pthread_create(&t, NULL, timed_locker, NULL);
	for (i = 0; i < 5; i++)
		sched_yield();
	pthread_mutex_unlock(&tm);
	pthread_join(t, NULL);
	check(timed_r == 0, "timedlock once its holder lets go", timed_r, 0);
	check(timed_ms < 1000, "... long before the deadline (ms)", timed_ms,
	    0);

	/* A deadline in the past, the mutex held: at once. */
	pthread_mutex_lock(&tm);
	timed_for = -1000;
	pthread_create(&t, NULL, timed_locker, NULL);
	pthread_join(t, NULL);
	check(timed_r == ETIMEDOUT, "a deadline already gone", timed_r,
	    ETIMEDOUT);
	pthread_mutex_unlock(&tm);

	/* A deadline that is no time: EINVAL, since it would have to wait. */
	pthread_mutex_lock(&tm);
	timed_for = 5000;
	timed_badts = 1;
	pthread_create(&t, NULL, timed_locker, NULL);
	pthread_join(t, NULL);
	timed_badts = 0;
	check(timed_r == EINVAL, "a deadline that is no time", timed_r,
	    EINVAL);
	pthread_mutex_unlock(&tm);
}

/* ---- cond ---- */

static pthread_mutex_t cm;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cvm;
static int flag;
static int waiter_r, waiter_held;
static long waiter_ms, waiter_for;
static clockid_t waiter_clock;
static int waiter_ran;

static void *
cond_waiter(void *arg)
{
	pthread_cond_t *c = arg;
	struct timespec ts, t0;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	pthread_mutex_lock(&cm);
	waiter_ran = 1;
	deadline(waiter_clock, waiter_for, &ts);
	waiter_r = 0;
	while (!flag && waiter_r == 0)
		waiter_r = pthread_cond_timedwait(c, &cm, &ts);
	/* It has the mutex again, whatever happened: trylock says so. */
	waiter_held = pthread_mutex_trylock(&cm) == EDEADLK;
	waiter_ms = ms_since(&t0);
	pthread_mutex_unlock(&cm);
	return NULL;
}

static void
cond(void)
{
	pthread_condattr_t ca;
	pthread_mutexattr_t a;
	struct timespec ts, t0;
	clockid_t clk = -1;
	pthread_t t;
	int r;

	pthread_mutex_init(&cm, NULL);

	/* Nobody signals. */
	flag = 0;
	waiter_clock = CLOCK_REALTIME;
	waiter_for = 250;
	pthread_create(&t, NULL, cond_waiter, &cv);
	pthread_join(t, NULL);
	check(waiter_r == ETIMEDOUT, "cond_timedwait, nobody signals",
	    waiter_r, ETIMEDOUT);
	check(waiter_ms >= 240 && waiter_ms < 1500, "... at the deadline (ms)",
	    waiter_ms, 250);
	check(waiter_held, "... with the mutex again", waiter_held, 1);

	/* Signalled. */
	flag = 0;
	waiter_for = 5000;
	waiter_ran = 0;
	pthread_create(&t, NULL, cond_waiter, &cv);
	while (!waiter_ran)
		sched_yield();
	sched_yield();
	r = pthread_cond_destroy(&cv);
	check(r == EBUSY, "destroying it under a timed waiter", r, EBUSY);
	pthread_mutex_lock(&cm);
	flag = 1;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&cm);
	pthread_join(t, NULL);
	check(waiter_r == 0, "cond_timedwait, signalled", waiter_r, 0);
	check(waiter_ms < 1000, "... long before the deadline (ms)",
	    waiter_ms, 0);
	check(waiter_held, "... with the mutex again", waiter_held, 1);

	/* Broadcast, too. */
	flag = 0;
	waiter_ran = 0;
	pthread_create(&t, NULL, cond_waiter, &cv);
	while (!waiter_ran)
		sched_yield();
	pthread_mutex_lock(&cm);
	flag = 1;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&cm);
	pthread_join(t, NULL);
	check(waiter_r == 0 && waiter_ms < 1000, "cond_timedwait, broadcast",
	    waiter_r, 0);

	/* A monotonic condition, as u_cnd_monotonic makes it. */
	r = pthread_condattr_init(&ca);
	r |= pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_condattr_getclock(&ca, &clk);
	check(r == 0 && clk == CLOCK_MONOTONIC, "condattr with "
	    "CLOCK_MONOTONIC", clk, CLOCK_MONOTONIC);
	r = pthread_condattr_setclock(&ca, CLOCK_PROCESS_CPUTIME_ID);
	check(r == EINVAL, "... not a CPU-time clock", r, EINVAL);
	r = pthread_cond_init(&cvm, &ca);
	check(r == 0, "a condition made with it", r, 0);
	pthread_condattr_destroy(&ca);
	flag = 0;
	waiter_clock = CLOCK_MONOTONIC;
	waiter_for = 200;
	pthread_create(&t, NULL, cond_waiter, &cvm);
	pthread_join(t, NULL);
	check(waiter_r == ETIMEDOUT && waiter_ms >= 190 && waiter_ms < 1500,
	    "a monotonic deadline (ms)", waiter_ms, 200);

	/* The main thread alone: nothing else can run, the process sleeps. */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	pthread_mutex_lock(&cm);
	deadline(CLOCK_MONOTONIC, 150, &ts);
	r = pthread_cond_timedwait(&cvm, &cm, &ts);
	pthread_mutex_unlock(&cm);
	check(r == ETIMEDOUT && ms_since(&t0) >= 140, "the main thread alone "
	    "(ms)", ms_since(&t0), 150);

	/* A deadline that is no time. */
	pthread_mutex_lock(&cm);
	ts.tv_nsec = -1;
	r = pthread_cond_timedwait(&cvm, &cm, &ts);
	pthread_mutex_unlock(&cm);
	check(r == EINVAL, "a deadline that is no time", r, EINVAL);

	/* A recursive mutex is let go entirely across the wait and held as
	 * many times after it. */
	pthread_mutex_destroy(&cm);
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&cm, &a);
	pthread_mutex_lock(&cm);
	pthread_mutex_lock(&cm);
	deadline(CLOCK_MONOTONIC, 50, &ts);
	r = pthread_cond_timedwait(&cvm, &cm, &ts);
	check(r == ETIMEDOUT, "a recursive mutex across a timed wait", r,
	    ETIMEDOUT);
	r = pthread_mutex_unlock(&cm);
	r |= pthread_mutex_unlock(&cm);
	check(r == 0, "... held twice after it", r, 0);
	r = pthread_mutex_unlock(&cm);
	check(r == EPERM, "... and not three times", r, EPERM);
	pthread_mutex_destroy(&cm);
	pthread_cond_destroy(&cvm);
}

/* ---- barrier ---- */

#define BTHREADS	4
#define ROUNDS		3

static pthread_barrier_t bar;
static int arrived[ROUNDS], serial[ROUNDS], wrong;

static void *
bworker(void *arg)
{
	int i, r;

	(void)arg;
	for (i = 0; i < ROUNDS; i++) {
		arrived[i]++;
		sched_yield();
		r = pthread_barrier_wait(&bar);
		if (r == PTHREAD_BARRIER_SERIAL_THREAD)
			serial[i]++;
		else if (r != 0)
			wrong++;
		if (arrived[i] != BTHREADS + 1)
			wrong++;	/* through before everyone came */
	}
	return NULL;
}

static void
barrier(void)
{
	pthread_t t[BTHREADS];
	int i, r, ok;

	r = pthread_barrier_init(&bar, NULL, 0);
	check(r == EINVAL, "a barrier for no threads", r, EINVAL);
	r = pthread_barrier_init(&bar, NULL, BTHREADS + 1);
	check(r == 0, "a barrier for five", r, 0);
	for (i = 0; i < BTHREADS; i++)
		pthread_create(&t[i], NULL, bworker, NULL);
	bworker(NULL);
	for (i = 0; i < BTHREADS; i++)
		pthread_join(t[i], NULL);
	ok = 1;
	for (i = 0; i < ROUNDS; i++)
		if (serial[i] != 1 || arrived[i] != BTHREADS + 1)
			ok = 0;
	check(ok, "three rounds, one serial thread each", ok, 1);
	check(wrong == 0, "nobody through early", wrong, 0);
	r = pthread_barrier_destroy(&bar);
	check(r == 0, "destroyed", r, 0);
}

/* ---- cpu ---- */

static volatile unsigned long sink;

static void
cpu(void)
{
	struct timespec a, b, res;
	clockid_t cid = -1;
	long ms;
	unsigned long i;
	int r;

	r = pthread_getcpuclockid(pthread_self(), &cid);
	check(r == 0, "pthread_getcpuclockid", r, 0);
	r = clock_getres(cid, &res);
	check(r == 0 && res.tv_sec == 0 && res.tv_nsec > 0, "its resolution "
	    "(ns)", res.tv_nsec, 10000000);
	r = clock_gettime(cid, &a);
	r |= clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b);
	check(r == 0, "both clocks read", r, 0);
	clock_gettime(cid, &a);
	for (i = 0; i < 40000000UL; i++)
		sink += i ^ (sink >> 3);
	clock_gettime(cid, &b);
	ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
	check(ms >= 50, "a busy loop shows on it (ms)", ms, 50);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a);
	check(a.tv_sec < 3600, "the process clock is time used, not a date "
	    "(s)", (long)a.tv_sec, 0);
	r = clock_gettime(0x7777, &a);
	check(r == -1 && errno == EINVAL, "a clock there is not", r, -1);
}

/* ---- names, and the raw monotonic clock ---- */

static void *
named(void *arg)
{
	(void)arg;
	sched_yield();
	return NULL;
}

static void
names(void)
{
	char buf[PTHREAD_MAX_NAMELEN_NP];
	struct timespec m, r, res;
	char longname[64];
	pthread_t t;
	long d;
	int rr;

	rr = pthread_setname_np(pthread_self(), "main-%s", (void *)"x");
	pthread_getname_np(pthread_self(), buf, sizeof(buf));
	check(rr == 0 && strcmp(buf, "main-x") == 0, "the main thread named",
	    rr, 0);
	pthread_create(&t, NULL, named, NULL);
	rr = pthread_setname_np(t, "worker", NULL);
	buf[0] = '\0';
	pthread_getname_np(t, buf, sizeof(buf));
	check(rr == 0 && strcmp(buf, "worker") == 0, "another thread named",
	    rr, 0);
	memset(longname, 'n', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	rr = pthread_setname_np(t, longname, NULL);
	check(rr == EINVAL, "a name too long", rr, EINVAL);
	pthread_join(t, NULL);

	rr = clock_gettime(CLOCK_MONOTONIC_RAW, &r);
	clock_gettime(CLOCK_MONOTONIC, &m);
	d = (m.tv_sec - r.tv_sec) * 1000 + (m.tv_nsec - r.tv_nsec) / 1000000;
	check(rr == 0 && d >= 0 && d < 1000, "CLOCK_MONOTONIC_RAW is the "
	    "monotonic clock (ms apart)", d, 0);
	rr = clock_getres(CLOCK_MONOTONIC_RAW, &res);
	check(rr == 0 && res.tv_nsec > 0, "... and has a resolution", rr, 0);
}

/* ---- POSIX scheduling calls: there, and saying there are no policies ---- */

static void
sched(void)
{
	struct sched_param sp = { .sched_priority = 1 };
	int r;

	r = sched_getscheduler(0);
	check(r == SCHED_OTHER, "sched_getscheduler: SCHED_OTHER", r,
	    SCHED_OTHER);
	r = sched_get_priority_max(SCHED_OTHER);
	check(r == 0, "its priority range is 0", r, 0);
	errno = 0;
	r = sched_get_priority_max(SCHED_FIFO);
	check(r == -1 && errno == EINVAL, "no SCHED_FIFO", errno, EINVAL);
	errno = 0;
	r = sched_setscheduler(0, SCHED_FIFO, &sp);
	check(r == -1 && errno == ENOSYS, "setting a policy: ENOSYS", errno,
	    ENOSYS);
	r = sched_getparam(0, &sp);
	check(r == 0 && sp.sched_priority == 0, "sched_getparam", r, 0);
	/* <sched.h> makes this __libc_thr_yield: libmthread's. */
	r = sched_yield();
	check(r == 0 || r == -1, "sched_yield through <sched.h> links and "
	    "returns", r, 0);
}

/* ---- c11 ---- */

static void
c11(void)
{
	mtx_t m;
	int r;

	r = mtx_init(&m, mtx_plain | mtx_recursive);
	check(r == thrd_success, "mtx_init recursive", r, thrd_success);
	r = mtx_lock(&m);
	r |= mtx_lock(&m);
	check(r == thrd_success, "locked twice", r, thrd_success);
	r = mtx_unlock(&m);
	r |= mtx_unlock(&m);
	check(r == thrd_success, "let go twice", r, thrd_success);
	mtx_destroy(&m);
	r = mtx_init(&m, mtx_plain);
	mtx_lock(&m);
	r = mtx_trylock(&m);
	check(r == thrd_busy, "a plain one tried again", r, thrd_busy);
	mtx_unlock(&m);
	mtx_destroy(&m);
}

int
main(int argc, char **argv)
{
	const char *what = argc > 1 ? argv[1] : "all";
	int all = strcmp(what, "all") == 0;

	if (all || strcmp(what, "mutex") == 0)
		mutex();
	if (all || strcmp(what, "timed") == 0)
		timed();
	if (all || strcmp(what, "cond") == 0)
		cond();
	if (all || strcmp(what, "barrier") == 0)
		barrier();
	if (all || strcmp(what, "cpu") == 0)
		cpu();
	if (all || strcmp(what, "names") == 0)
		names();
	if (all || strcmp(what, "sched") == 0)
		sched();
	if (all || strcmp(what, "c11") == 0)
		c11();
	printf("PTHTEST %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
