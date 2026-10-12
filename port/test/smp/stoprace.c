/*
 * stoprace: is a process that PM has stopped kept from running its call?
 *
 * PM stops a process before it delivers a signal (sys_delay_stop). The
 * kernel defers the stop when the process is sending, but a process that
 * has already trapped on another core and is waiting there for the big
 * kernel lock is not sending yet: smp_schedule_stop_proc() stops it with an
 * IPI, and the trap that was waiting goes on to run the call anyway. PM then
 * gets a message from a process it holds stopped - with an unpause to VFS
 * outstanding, that is the assertion in do_sigprocmask() that brought the
 * board down twice on 2026-10-12 - and if PM survives it, the reply lands in
 * the signal handler's frame instead of the call's.
 *
 * Victims call PM without end (sigprocmask, getpid) and check every answer;
 * killers send them SIGUSR1 as fast as PM takes it; hogs keep the kernel
 * busy with calls to VFS and a driver, so that a trap often has to wait for
 * the lock. Everything runs for the given number of seconds.
 *
 *	stoprace [-v victims] [-k killers] [-h hogs] [-t seconds]
 *
 * Prints the totals and "stoprace: ok" or "stoprace: CORRUPT" (exit 1).
 * With the race open, PM usually panics before that line - which is the
 * other way the probe answers. DEBUG_STOPRACE in kernel/debug.h names each
 * call the kernel served for a stopped process.
 *
 * port/PORTING-LOG.md, "Остановленный процесс не входит в ядро".
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAXPROC	32

struct tally {
	unsigned long calls;	/* calls made and checked */
	unsigned long sigs;	/* SIGUSR1 handled */
	unsigned long badsig;	/* the handler got some other number */
	unsigned long badpid;	/* getpid() said someone else */
	unsigned long badmask;	/* sigprocmask() failed or the mask was not empty */
};

static volatile sig_atomic_t nsigs, nbadsig;

static void
on_usr1(int sig)
{

	if (sig != SIGUSR1)
		nbadsig++;
	nsigs++;
}

static void
victim(int wfd, time_t end)
{
	struct sigaction sa;
	struct tally t;
	sigset_t none, old;
	pid_t me = getpid();
	unsigned long n;

	memset(&t, 0, sizeof(t));
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);
	sigemptyset(&none);

	for (n = 0; ; n++) {
		/* The mask is empty outside the handler, so it must read so. */
		sigfillset(&old);
		if (sigprocmask(SIG_SETMASK, &none, &old) != 0 ||
		    sigismember(&old, SIGUSR1))
			t.badmask++;
		if (getpid() != me)
			t.badpid++;
		t.calls += 2;
		if ((n & 1023) == 0 && time(NULL) >= end)
			break;
	}

	/* No more signals counted from here on: the totals are final. */
	signal(SIGUSR1, SIG_IGN);
	t.sigs = nsigs;
	t.badsig = nbadsig;
	(void)write(wfd, &t, sizeof(t));
	_exit(0);
}

static void
killer(pid_t *pids, int n, time_t end)
{
	unsigned long k;
	int i;

	for (k = 0; ; k++) {
		for (i = 0; i < n; i++)
			(void)kill(pids[i], SIGUSR1);
		if ((k & 255) == 0 && time(NULL) >= end)
			break;
	}
	_exit(0);
}

static void
hog(time_t end)
{
	char c;
	unsigned long k;
	int fd = open("/dev/null", O_WRONLY);

	for (k = 0; ; k++) {
		(void)write(fd, &c, 1);
		if ((k & 1023) == 0 && time(NULL) >= end)
			break;
	}
	_exit(0);
}

int
main(int argc, char **argv)
{
	pid_t vpid[MAXPROC], others[2 * MAXPROC];
	int nv = 3, nk = 2, nh = 2, secs = 60, nothers = 0;
	int c, i, p[2];
	struct tally sum, t;
	time_t end;

	while ((c = getopt(argc, argv, "v:k:h:t:")) != -1) {
		switch (c) {
		case 'v': nv = atoi(optarg); break;
		case 'k': nk = atoi(optarg); break;
		case 'h': nh = atoi(optarg); break;
		case 't': secs = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: stoprace [-v victims] "
			    "[-k killers] [-h hogs] [-t seconds]\n");
			return 2;
		}
	}
	if (nv < 1 || nv > MAXPROC || nk < 0 || nk > MAXPROC ||
	    nh < 0 || nh > MAXPROC) {
		fprintf(stderr, "stoprace: 1..%d victims, 0..%d of the others\n",
		    MAXPROC, MAXPROC);
		return 2;
	}
	if (pipe(p) != 0) {
		perror("pipe");
		return 2;
	}
	end = time(NULL) + secs;

	for (i = 0; i < nv; i++) {
		if ((vpid[i] = fork()) == 0) {
			close(p[0]);
			victim(p[1], end);
		}
	}
	close(p[1]);
	for (i = 0; i < nk; i++)
		if ((others[nothers++] = fork()) == 0)
			killer(vpid, nv, end);
	for (i = 0; i < nh; i++)
		if ((others[nothers++] = fork()) == 0)
			hog(end);

	memset(&sum, 0, sizeof(sum));
	for (i = 0; i < nv; i++) {
		if (read(p[0], &t, sizeof(t)) != sizeof(t)) {
			fprintf(stderr, "stoprace: a victim did not report\n");
			sum.badpid++;
			continue;
		}
		sum.calls += t.calls;
		sum.sigs += t.sigs;
		sum.badsig += t.badsig;
		sum.badpid += t.badpid;
		sum.badmask += t.badmask;
	}
	while (wait(NULL) > 0 || errno == EINTR)
		;

	printf("stoprace: %d victims, %d killers, %d hogs, %d s: %lu calls, "
	    "%lu signals; bad: signo %lu, pid %lu, mask %lu\n", nv, nk, nh,
	    secs, sum.calls, sum.sigs, sum.badsig, sum.badpid, sum.badmask);
	if (sum.badsig || sum.badpid || sum.badmask) {
		printf("stoprace: CORRUPT\n");
		return 1;
	}
	printf("stoprace: ok\n");
	return 0;
}
