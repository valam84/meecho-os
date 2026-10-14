/*
 * Thread-local storage of a static MEECHO program (stage 11.6: what Mesa
 * needs of threads - its current GL context and dispatch table are
 * thread_local, initial-exec).
 *
 * On aarch64 a thread's TLS is found through TPIDR_EL0: the block of the
 * running thread starts 16 bytes past it (variant I).  Three parties keep
 * it: libc sets up the main thread's block from PT_TLS
 * (__libc_static_tls_setup()), the kernel keeps each process's register
 * across switches, and libmthread gives every thread a block of its own and
 * loads it when it switches to the thread.  Each part is checked here:
 *
 *	tlstest		main thread: initial values (.tdata), zeros (.tbss)
 *	tlstest procs	two processes, each its own values, through many
 *			switches (the kernel keeps the register per process)
 *	tlstest fork	a child has a copy, and changing it does not change
 *			the parent's
 *	tlstest threads	libmthread threads: each its own copy, through
 *			yields; the main thread's untouched
 *
 * Every check prints a line; the last line counts them.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minix/mthread.h>

static _Thread_local int t_init = 42;
static _Thread_local char t_str[64] = "initial";
static _Thread_local long t_zero;
static __thread int t_gnu = 7;

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

static void
main_thread(void)
{
	check(t_init == 42, "an initialized thread-local", t_init, 42);
	check(strcmp(t_str, "initial") == 0, "an initialized array",
	    strcmp(t_str, "initial"), 0);
	check(t_zero == 0, "a zero thread-local", t_zero, 0);
	check(t_gnu == 7, "a __thread one", t_gnu, 7);
	t_gnu = 8;	/* written somewhere, so not folded into a constant */
	check(t_gnu == 8, "a __thread one written", t_gnu, 8);
	t_gnu = 7;
	t_init = 43;
	check(t_init == 43, "written and read back", t_init, 43);
	t_init = 42;
}

/* Two processes, each with its own values, through many switches. */
static void
procs(void)
{
	int i, bad = 0, st;
	pid_t pid;

	if ((pid = fork()) == 0) {
		t_init = 1000;
		for (i = 0; i < 2000; i++) {
			t_zero = i;
			if (i % 50 == 0)
				usleep(1000);
			if (t_init != 1000 || t_zero != i)
				bad++;
		}
		_exit(bad != 0);
	}
	t_init = 2000;
	for (i = 0; i < 2000; i++) {
		t_zero = -i;
		if (i % 50 == 0)
			usleep(1000);
		if (t_init != 2000 || t_zero != -i)
			bad++;
	}
	waitpid(pid, &st, 0);
	check(bad == 0, "the parent's values through switches", bad, 0);
	check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the child's values "
	    "through switches", WIFEXITED(st) ? WEXITSTATUS(st) : -1, 0);
	t_init = 42;
	t_zero = 0;
}

static void
forked(void)
{
	int st;
	pid_t pid;

	t_init = 77;
	if ((pid = fork()) == 0) {
		int ok = t_init == 77;

		t_init = 88;
		_exit(ok && t_init == 88 ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the child has a copy",
	    WIFEXITED(st) ? WEXITSTATUS(st) : -1, 0);
	check(t_init == 77, "the parent's is its own", t_init, 77);
	t_init = 42;
}

#define NTHREADS	4

static int thread_bad[NTHREADS];
static void *thread_tls[NTHREADS];

static void *
worker(void *arg)
{
	int id = (int)(intptr_t)arg, i;

	thread_bad[id] = 0;
	thread_tls[id] = &t_init;
	if (t_init != 42 || t_zero != 0 || strcmp(t_str, "initial") != 0 ||
	    t_gnu != 7)
		thread_bad[id] |= 1;	/* a fresh copy of the template */
	t_init = 100 + id;
	t_gnu = 200 + id;
	for (i = 0; i < 100; i++) {
		t_zero = id * 1000 + i;
		mthread_yield();
		if (t_init != 100 + id || t_zero != id * 1000 + i ||
		    t_gnu != 200 + id)
			thread_bad[id] |= 2;
	}
	return NULL;
}

static void
threads(void)
{
	mthread_thread_t th[NTHREADS];
	int i, bad = 0, distinct = 1, j;

	t_init = 5;
	for (i = 0; i < NTHREADS; i++)
		if (mthread_create(&th[i], NULL, worker, (void *)(intptr_t)i)
		    != 0)
			bad |= 4;
	for (i = 0; i < NTHREADS; i++)
		mthread_join(th[i], NULL);
	for (i = 0; i < NTHREADS; i++) {
		bad |= thread_bad[i];
		for (j = 0; j < i; j++)
			if (thread_tls[i] == thread_tls[j] ||
			    thread_tls[i] == &t_init)
				distinct = 0;
	}
	check(bad == 0, "each thread its own copy, through yields", bad, 0);
	check(distinct, "every thread's block apart, and from the main "
	    "thread's", distinct, 1);
	check(t_init == 5, "the main thread's untouched", t_init, 5);
	t_init = 42;
}

int
main(int argc, char **argv)
{
	const char *what = argc > 1 ? argv[1] : "all";

	main_thread();
	if (strcmp(what, "all") == 0 || strcmp(what, "procs") == 0)
		procs();
	if (strcmp(what, "all") == 0 || strcmp(what, "fork") == 0)
		forked();
	if (strcmp(what, "all") == 0 || strcmp(what, "threads") == 0)
		threads();
	printf("TLSTEST %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
