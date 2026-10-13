/*
 * pollbench - what waiting on many descriptors costs on this system, to
 * choose the event loop under libwayland by a number (milestone 11.3).
 *
 * A compositor waits on a socket per client plus a few of its own (input,
 * timers), wakes for each frame or event, and goes back to waiting.  Two
 * ways to give it epoll were on the table: a library that turns epoll into
 * poll(2), paying for every descriptor on every wait, or kqueue in VFS,
 * paying only for what is ready but costing a new mechanism in VFS.  The
 * library is the cheap one to build; this measures what it costs to run.
 *
 *   pollbench [-r rounds] N...
 *
 * For each N: N socketpairs (AF_UNIX, as a Wayland client's connection
 * is), and
 *
 *   idle   poll(2) on all N with nothing ready and timeout 0 - the price
 *          of looking, which a poll-based loop pays on every wait;
 *   rtt    a child sends a byte on the LAST of the N pairs, the parent,
 *          blocked in poll(2) on all N, reads it and answers; the child
 *          times the round trip - waking up through a wait on N;
 *   sel    the idle price through select(2), which poll(2) is built on
 *          here, to see the library's own share.
 *
 * Output, one line per N:
 *
 *   POLL n N idle_us X rtt_us Y sel_us Z
 */
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double
now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void
one(int n, int rounds)
{
	int (*sv)[2], res[2], i, r, status, maxfd = 0;
	struct pollfd *pfd;
	double t0, idle, rtt, sel;
	fd_set rs;
	struct timeval tv;
	pid_t pid;
	char c;

	if ((sv = calloc(n, sizeof(*sv))) == NULL ||
	    (pfd = calloc(n, sizeof(*pfd))) == NULL)
		err(1, "calloc");
	for (i = 0; i < n; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]) != 0)
			err(1, "socketpair %d", i);
		pfd[i].fd = sv[i][0];
		pfd[i].events = POLLIN;
		if (sv[i][0] > maxfd)
			maxfd = sv[i][0];
	}

	/* The price of looking. */
	t0 = now_us();
	for (r = 0; r < rounds; r++)
		if (poll(pfd, n, 0) != 0)
			errx(1, "idle poll saw something");
	idle = (now_us() - t0) / rounds;

	t0 = now_us();
	for (r = 0; r < rounds; r++) {
		FD_ZERO(&rs);
		for (i = 0; i < n; i++)
			FD_SET(sv[i][0], &rs);
		tv.tv_sec = tv.tv_usec = 0;
		if (select(maxfd + 1, &rs, NULL, NULL, &tv) != 0)
			errx(1, "idle select saw something");
	}
	sel = (now_us() - t0) / rounds;

	/* Waking up through a wait on all of them. */
	if (pipe(res) != 0)
		err(1, "pipe");
	if ((pid = fork()) == 0) {
		int fd = sv[n - 1][1];
		double sum = 0;

		for (r = 0; r < rounds; r++) {
			t0 = now_us();
			if (write(fd, "p", 1) != 1 || read(fd, &c, 1) != 1)
				_exit(1);
			sum += now_us() - t0;
		}
		sum /= rounds;
		_exit(write(res[1], &sum, sizeof(sum)) == sizeof(sum) ? 0 : 1);
	}
	for (r = 0; r < rounds; r++) {
		if (poll(pfd, n, -1) != 1 || !(pfd[n - 1].revents & POLLIN))
			errx(1, "poll woke for the wrong thing");
		if (read(sv[n - 1][0], &c, 1) != 1 ||
		    write(sv[n - 1][0], "q", 1) != 1)
			err(1, "pong");
	}
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
	    read(res[0], &rtt, sizeof(rtt)) != sizeof(rtt))
		errx(1, "the child did not finish");
	close(res[0]);
	close(res[1]);

	printf("POLL n %d idle_us %.1f rtt_us %.1f sel_us %.1f\n", n, idle,
	    rtt, sel);

	for (i = 0; i < n; i++) {
		close(sv[i][0]);
		close(sv[i][1]);
	}
	free(sv);
	free(pfd);
}

int
main(int argc, char **argv)
{
	int c, rounds = 2000;

	while ((c = getopt(argc, argv, "r:")) != -1) {
		if (c == 'r')
			rounds = atoi(optarg);
		else
			errx(2, "usage: pollbench [-r rounds] N...");
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (; optind < argc; optind++)
		one(atoi(argv[optind]), rounds);
	return 0;
}
