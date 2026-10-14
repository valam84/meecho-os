/*
 * flowprobe - known messages to known services, for the message matrix
 * (<minix/ipcflow.h>, /proc/<pid>/ipc).
 *
 * Between two waits of its own it sends, by its own calls:
 *
 *   N x getpid()            to PM
 *   N x fstat()             to VFS
 *   N x mmap() + munmap()   to VM, twice N
 *
 * and waits before and after, printing a line each time, so that another
 * process can read its row and /proc/ktrace while it cannot add to either:
 * the two are then counts of the same moment, and the stand
 * (qemu-ipcflow.py) compares what changed between the waits.
 *
 * A wait is an open(2) of the FIFO given with -g, which blocks until a
 * writer opens it (the stand: echo > FIFO), then a read and a close.  Not
 * raise(SIGSTOP): MINIX's PM has no stopped state for a signal - the
 * default action of SIGSTOP there is to end the process.
 *
 *   flowprobe -g FIFO [-n N]    the work between two waits
 *   flowprobe -g FIFO -c K      a child sends K fstat() to VFS and exits;
 *                               the parent waits for it, then waits, so
 *                               that VFS's "gone" column can be read
 *
 * Built statically against DESTDIR and fetched by the guest over HTTP, as
 * port/test/ptime's probe is, never put in DESTDIR.
 */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *gate;

static void
wait_gate(const char *tag)
{
	char c;
	int fd;

	printf("FLOWPROBE wait %s %d\n", tag, (int)getpid());
	fflush(stdout);
	if ((fd = open(gate, O_RDONLY)) < 0) {
		perror(gate);
		exit(1);
	}
	(void)read(fd, &c, 1);
	(void)close(fd);
}

static void
work(long n)
{
	struct stat st;
	void *p;
	long i;

	for (i = 0; i < n; i++)
		(void)getpid();
	for (i = 0; i < n; i++)
		(void)fstat(1, &st);
	for (i = 0; i < n; i++) {
		p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}
		(void)munmap(p, 4096);
	}
}

int
main(int argc, char **argv)
{
	long n = 1000, k = 0;
	int c, status;
	pid_t pid;

	while ((c = getopt(argc, argv, "g:n:c:")) != -1)
		switch (c) {
		case 'g': gate = optarg; break;
		case 'n': n = atol(optarg); break;
		case 'c': k = atol(optarg); break;
		default:
			goto usage;
		}
	if (gate == NULL)
		goto usage;

	if (k > 0) {
		wait_gate("a");
		if ((pid = fork()) < 0) {
			perror("fork");
			return 1;
		}
		if (pid == 0) {
			struct stat st;
			long i;

			printf("FLOWPROBE child %d\n", (int)getpid());
			fflush(stdout);
			for (i = 0; i < k; i++)
				(void)fstat(1, &st);
			_exit(0);
		}
		while (waitpid(pid, &status, 0) < 0)
			;
		wait_gate("b");
		printf("FLOWPROBE done\n");
		return 0;
	}

	wait_gate("a");
	work(n);
	wait_gate("b");
	printf("FLOWPROBE done n %ld\n", n);
	return 0;
usage:
	fprintf(stderr, "usage: flowprobe -g FIFO [-n N | -c K]\n");
	return 2;
}
