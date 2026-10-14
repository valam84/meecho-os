/*
 * A process that only counts, for port/test/jobctl/qemu-jobctl.py: the
 * stand stops it with kill -STOP while it runs on a CPU and sees whether the
 * count still moves.
 *
 *	jobprobe FILE N		count to N; the count goes to FILE every 2^20
 *				and at the end, then "JOBPROBE done N"
 *	jobprobe -t SECS	count for SECS seconds: "JOBPROBE rate R" (per
 *				second), to size N for this machine
 *	jobprobe -s		what this process got from its parent for the
 *				job control signals: "JOBPROBE sig TSTP dfl ..."
 *
 * The loop makes no system calls between the writes, so a stopped probe is
 * stopped where it computes, not where it waits.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EVERY	0xfffffUL	/* write or look at the clock every 2^20 */

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
	volatile unsigned long x = 0;
	unsigned long i, n;
	double t0, secs;
	char buf[32];
	int fd, len;

	if (argc == 2 && strcmp(argv[1], "-s") == 0) {
		static const struct { int sig; const char *name; } js[] = {
			{ SIGTSTP, "TSTP" }, { SIGTTIN, "TTIN" },
			{ SIGTTOU, "TTOU" }, { SIGCONT, "CONT" },
			{ SIGCHLD, "CHLD" }, { SIGHUP, "HUP" }, { SIGINT, "INT" },
		};
		struct sigaction sa;
		sigset_t set;
		unsigned j;

		sigprocmask(SIG_SETMASK, NULL, &set);
		printf("JOBPROBE sig");
		for (j = 0; j < sizeof(js) / sizeof(js[0]); j++) {
			sigaction(js[j].sig, NULL, &sa);
			printf(" %s %s%s", js[j].name,
			    sa.sa_handler == SIG_IGN ? "ign" :
			    sa.sa_handler == SIG_DFL ? "dfl" : "caught",
			    sigismember(&set, js[j].sig) ? ",blocked" : "");
		}
		printf("\n");
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "-t") == 0) {
		secs = atof(argv[2]);
		t0 = now();
		for (i = 1; ; i++) {
			x++;
			if ((i & EVERY) == 0 && now() - t0 >= secs)
				break;
		}
		printf("JOBPROBE rate %lu\n", (unsigned long)(i / (now() - t0)));
		return 0;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: jobprobe FILE N | jobprobe -t SECS\n");
		return 2;
	}
	n = strtoul(argv[2], NULL, 0);
	if ((fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
		perror(argv[1]);
		return 1;
	}
	for (i = 1; i <= n; i++) {
		x++;
		if ((i & EVERY) == 0 || i == n) {
			len = snprintf(buf, sizeof(buf), "%20lu\n", i);
			if (pwrite(fd, buf, len, 0) != len) {
				perror("pwrite");
				return 1;
			}
		}
	}
	printf("JOBPROBE done %lu\n", n);
	return 0;
}
