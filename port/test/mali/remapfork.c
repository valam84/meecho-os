/*
 * remapfork - two things stage 11.6 changed under the GPU's buffers,
 * proved on QEMU with fb (virtual=1), which lends memory the same way.
 *
 *	remapfork nodev
 *		mmap(2) of a character device whose driver maps nothing
 *		(/dev/null): ENODEV now - VFS says it quietly and libc asks
 *		the driver (MIOCMMAP), which does not know the request - where
 *		it was EINVAL and a console line from VFS.
 *	remapfork fork
 *		map fb's buffers (FBIOC_MAP), touch the first page of buffer
 *		0, say READY and wait for a line on stdin (meanwhile the test
 *		restarts fb, and the region the mapping came from is gone);
 *		then fork twice: the first child reads the page it has (it
 *		must work - the page is the process's own now), the second
 *		touches a page it never had (it must die of SIGSEGV).  Before
 *		11.6 the first fork panicked VM: "copy: original getsrc
 *		failed" - any process could ask for it.
 *
 * Prints "REMAPFORK <what> ok" or "REMAPFORK <what> FAIL: why".
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fb_ioc.h"

static int
nodev(void)
{
	void *p;
	int fd;

	if ((fd = open("/dev/null", O_RDWR)) < 0) {
		printf("REMAPFORK nodev FAIL: /dev/null: %s\n", strerror(errno));
		return 1;
	}
	errno = 0;
	p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (p != MAP_FAILED || errno != ENODEV) {
		printf("REMAPFORK nodev FAIL: mmap gave %p, errno %d (%s), want "
		    "ENODEV\n", p, errno, strerror(errno));
		return 1;
	}
	printf("REMAPFORK nodev ok: mmap(2) of /dev/null: ENODEV\n");
	return 0;
}

static int
child(volatile unsigned char *page)
{
	pid_t pid;
	int st;
	unsigned char c;

	if ((pid = fork()) < 0)
		return -1;
	if (pid == 0) {
		c = *page;
		_exit(c == 0x5a ? 0 : 3);
	}
	if (waitpid(pid, &st, 0) != pid)
		return -1;
	return st;
}

static int
dofork(void)
{
	struct fb_map m;
	volatile unsigned char *b;
	char line[16];
	int fd, st;

	if ((fd = open("/dev/fb0", O_RDWR)) < 0 ||
	    ioctl(fd, FBIOC_MAP, &m) != 0) {
		printf("REMAPFORK fork FAIL: FBIOC_MAP: %s\n", strerror(errno));
		return 1;
	}
	b = (volatile unsigned char *)(uintptr_t)m.addr[0];
	b[0] = 0x5a;			/* page 0 is the process's from now on */
	printf("READY buffer 0 at %p, %u bytes\n", (void *)b, m.size);
	fflush(stdout);
	if (fgets(line, sizeof(line), stdin) == NULL)
		return 1;

	/* The source is gone (fb restarted): fork, and read what we have. */
	st = child(b);
	if (st != 0) {
		printf("REMAPFORK fork FAIL: the child that reads page 0: status "
		    "0x%x\n", st);
		return 1;
	}
	/* A page the mapping never had: there is nothing behind it now. */
	st = child(b + 8 * 4096);
	if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGSEGV) {
		printf("REMAPFORK fork FAIL: the child that touches page 8: "
		    "status 0x%x, want SIGSEGV\n", st);
		return 1;
	}
	printf("REMAPFORK fork ok: two forks after the source went; page 0 "
	    "read, page 8 SIGSEGV\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc == 2 && strcmp(argv[1], "nodev") == 0)
		return nodev();
	if (argc == 2 && strcmp(argv[1], "fork") == 0)
		return dofork();
	fprintf(stderr, "usage: remapfork nodev | fork\n");
	return 2;
}
