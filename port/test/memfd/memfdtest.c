/*
 * memfdtest - memory objects (memfd_create(2)) on MEECHO: what a Wayland
 * client and its compositor will do with them, and what must not break.
 *
 *   memfdtest [-q] [-u]
 *
 * -u drops to uid and gid 9999 first (as port/test/audit does): nothing
 * about a memory object may need privilege.
 *
 * Every check prints "ok" or "FAIL" with what was seen; the last line is
 *
 *   MEMFD checks N failed F
 *
 * The heart of it is "pass": two processes, the object made by one AFTER
 * the fork and handed to the other over a socket (SCM_RIGHTS), both
 * mapping it shared and writable; what each writes the other reads.  And
 * "returned": the free page count of the machine (/proc/meminfo) before a
 * 16 MiB object is made and touched, and after both processes are done
 * with it - the pages come back.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAGE	4096
#define BIG	(16 * 1024 * 1024)	/* 4096 pages: well above the noise */

static int checks, fails, quiet;

static void
check(const char *name, int ok, const char *fmt, ...)
{
	char why[256] = "";
	va_list ap;

	checks++;
	if (fmt) {
		va_start(ap, fmt);
		vsnprintf(why, sizeof(why), fmt, ap);
		va_end(ap);
	}
	if (!ok) {
		fails++;
		printf("FAIL  %s: %s\n", name, why);
	} else if (!quiet && strncmp(why, "free pages", 10) == 0)
		printf("ok    %s (%s)\n", name, why);	/* the numbers */
	else if (!quiet)
		printf("ok    %s\n", name);
	fflush(stdout);
}

/* Free pages of the machine, from /proc/meminfo: pagesize total free ... */
static long
free_pages(void)
{
	unsigned long psz, total, fr, largest, cached;
	FILE *f;
	int n;

	if ((f = fopen("/proc/meminfo", "r")) == NULL)
		return -1;
	n = fscanf(f, "%lu %lu %lu %lu %lu", &psz, &total, &fr, &largest,
	    &cached);
	fclose(f);
	return n >= 3 ? (long)fr : -1;
}

/* Free pages once the system has settled: VM frees on the way out. */
static long
settled_free(void)
{
	long a, b;
	int i;

	b = free_pages();
	for (i = 0; i < 20; i++) {
		usleep(100000);
		a = free_pages();
		if (a == b)
			break;
		b = a;
	}
	return b;
}

static int
send_fd(int sock, int fd)
{
	struct msghdr msg;
	struct iovec iov;
	char byte = 'F';
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} c;
	struct cmsghdr *cm;

	memset(&msg, 0, sizeof(msg));
	memset(&c, 0, sizeof(c));
	iov.iov_base = &byte;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = c.buf;
	msg.msg_controllen = sizeof(c.buf);
	cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &fd, sizeof(int));
	return sendmsg(sock, &msg, 0) == 1 ? 0 : -1;
}

static int
recv_fd(int sock)
{
	struct msghdr msg;
	struct iovec iov;
	char byte;
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} c;
	struct cmsghdr *cm;
	int fd;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = &byte;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = c.buf;
	msg.msg_controllen = sizeof(c.buf);
	if (recvmsg(sock, &msg, 0) != 1)
		return -1;
	if ((cm = CMSG_FIRSTHDR(&msg)) == NULL || cm->cmsg_type != SCM_RIGHTS)
		return -1;
	memcpy(&fd, CMSG_DATA(cm), sizeof(int));
	return fd;
}

/* One word per page, so that every page of the object is touched. */
static uint32_t
pat(uint32_t who, size_t page)
{
	return (who << 24) ^ (uint32_t)(page * 2654435761u);
}

/*
 * The child of "pass": receives the object, checks the parent's half,
 * writes its own, says so, and exits.  Exit status is the number of
 * pages that were wrong (capped), or 100+ for a step that failed.
 */
static int
pass_child(int sock)
{
	uint32_t *p;
	size_t i, bad = 0;
	struct stat st;
	char done = 'D';
	int fd;

	if ((fd = recv_fd(sock)) < 0)
		return 101;
	if (fstat(fd, &st) != 0 || st.st_size != BIG)
		return 102;
	p = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		return 103;
	close(fd);		/* the mapping keeps it */

	for (i = 0; i < BIG / 2 / PAGE; i++)
		if (p[i * PAGE / 4] != pat(1, i))
			bad++;
	for (i = BIG / 2 / PAGE; i < BIG / PAGE; i++)
		p[i * PAGE / 4] = pat(2, i);

	if (write(sock, &done, 1) != 1)
		return 104;
	/* No munmap: exit has to give it back as well. */
	return bad > 99 ? 99 : (int)bad;
}

static void
t_pass(void)
{
	int sv[2], fd, status;
	long before, after;
	uint32_t *p;
	size_t i, bad = 0;
	pid_t pid;
	char c;

	before = settled_free();

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
		check("pass: socketpair", 0, "%s", strerror(errno));
		return;
	}
	if ((pid = fork()) == 0) {
		close(sv[0]);
		_exit(pass_child(sv[1]));
	}
	close(sv[1]);

	fd = memfd_create("pass", MFD_CLOEXEC);
	check("memfd_create", fd >= 0, "%s", strerror(errno));
	if (fd < 0)
		return;
	check("MFD_CLOEXEC sets close-on-exec",
	    (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0, NULL);
	check("ftruncate to 16 MiB", ftruncate(fd, BIG) == 0, "%s",
	    strerror(errno));

	p = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check("mmap shared and writable", p != MAP_FAILED, "%s",
	    strerror(errno));
	if (p == MAP_FAILED)
		return;
	for (i = 0; i < BIG / 2 / PAGE; i++)
		p[i * PAGE / 4] = pat(1, i);

	check("the descriptor goes over the socket (SCM_RIGHTS)",
	    send_fd(sv[0], fd) == 0, "%s", strerror(errno));
	close(fd);

	check("the other process answers", read(sv[0], &c, 1) == 1 &&
	    c == 'D', NULL);
	for (i = BIG / 2 / PAGE; i < BIG / PAGE; i++)
		if (p[i * PAGE / 4] != pat(2, i))
			bad++;
	check("what the other process wrote is here", bad == 0,
	    "%zu of %d pages wrong", bad, BIG / 2 / PAGE);

	waitpid(pid, &status, 0);
	check("what this process wrote was there",
	    WIFEXITED(status) && WEXITSTATUS(status) == 0,
	    "child status %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);

	check("munmap", munmap(p, BIG) == 0, "%s", strerror(errno));
	close(sv[0]);

	after = settled_free();
	check("the pages come back after both are done",
	    before > 0 && after >= before - 64,
	    "free pages %ld before, %ld after (%ld held)", before, after,
	    before - after);
}

static void
t_private(void)
{
	char *s, *q;
	int fd;

	fd = memfd_create("private", 0);
	if (fd < 0 || ftruncate(fd, PAGE) != 0) {
		check("private: setup", 0, "%s", strerror(errno));
		return;
	}
	s = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	q = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (s == MAP_FAILED || q == MAP_FAILED) {
		check("private: mmap", 0, "%s", strerror(errno));
		return;
	}
	s[0] = 'S';
	check("a private mapping sees the object", q[0] == 'S', "got %d",
	    q[0]);
	q[0] = 'P';
	check("a private write stays private", s[0] == 'S' && q[0] == 'P',
	    "shared %d private %d", s[0], q[0]);
	s[1] = 'T';
	check("after its copy, the private page no longer follows",
	    q[1] == 0, "got %d", q[1]);
	munmap(s, PAGE);
	munmap(q, PAGE);
	close(fd);
}

static void
t_fork(void)
{
	volatile char *s;
	int fd, status;
	pid_t pid;

	fd = memfd_create("fork", 0);
	if (fd < 0 || ftruncate(fd, PAGE) != 0) {
		check("fork: setup", 0, "%s", strerror(errno));
		return;
	}
	s = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (s == MAP_FAILED) {
		check("fork: mmap", 0, "%s", strerror(errno));
		return;
	}
	s[0] = 'A';
	if ((pid = fork()) == 0) {
		s[1] = s[0] == 'A' ? 'C' : 'X';
		_exit(0);
	}
	waitpid(pid, &status, 0);
	check("a shared mapping stays shared across fork", s[1] == 'C',
	    "got %d", s[1]);
	munmap((void *)s, PAGE);
}

static void
t_trunc(void)
{
	unsigned char *p;
	struct stat st;
	int fd;

	fd = memfd_create("trunc", 0);
	if (fd < 0 || ftruncate(fd, 2 * PAGE) != 0) {
		check("trunc: setup", 0, "%s", strerror(errno));
		return;
	}
	p = mmap(NULL, 2 * PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		check("trunc: mmap", 0, "%s", strerror(errno));
		return;
	}
	memset(p, 0xaa, 2 * PAGE);
	check("shrink to 4196", ftruncate(fd, PAGE + 100) == 0, "%s",
	    strerror(errno));
	check("fstat says 4196", fstat(fd, &st) == 0 && st.st_size == PAGE +
	    100, "size %lld", (long long)st.st_size);
	check("below the end is kept", p[PAGE + 99] == 0xaa, "got %#x",
	    p[PAGE + 99]);
	check("past the end reads zero", p[PAGE + 100] == 0 &&
	    p[2 * PAGE - 1] == 0, "got %#x %#x", p[PAGE + 100],
	    p[2 * PAGE - 1]);
	check("grow back", ftruncate(fd, 2 * PAGE) == 0, "%s",
	    strerror(errno));
	check("and what grew is zero", p[PAGE + 200] == 0, "got %#x",
	    p[PAGE + 200]);
	munmap(p, 2 * PAGE);
	close(fd);
}

static void
t_seals(void)
{
	int fd;

	fd = memfd_create("noseal", 0);
	check("without MFD_ALLOW_SEALING: sealed against sealing",
	    fcntl(fd, F_GET_SEALS) == F_SEAL_SEAL, "seals %d",
	    fcntl(fd, F_GET_SEALS));
	check("and F_ADD_SEALS is refused",
	    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) == -1 && errno == EPERM,
	    "%s", strerror(errno));
	close(fd);

	fd = memfd_create("seal", MFD_ALLOW_SEALING);
	ftruncate(fd, 2 * PAGE);
	check("F_SEAL_SHRINK", fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) == 0,
	    "%s", strerror(errno));
	check("then shrinking is refused",
	    ftruncate(fd, PAGE) == -1 && errno == EPERM, "%s",
	    strerror(errno));
	check("and growing is not", ftruncate(fd, 3 * PAGE) == 0, "%s",
	    strerror(errno));
	check("F_SEAL_WRITE is refused here (not kept)",
	    fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EINVAL,
	    "%s", strerror(errno));
	check("F_SEAL_GROW | F_SEAL_SEAL", fcntl(fd, F_ADD_SEALS,
	    F_SEAL_GROW | F_SEAL_SEAL) == 0, "%s", strerror(errno));
	check("then growing is refused",
	    ftruncate(fd, 4 * PAGE) == -1 && errno == EPERM, "%s",
	    strerror(errno));
	check("F_GET_SEALS",
	    fcntl(fd, F_GET_SEALS) == (F_SEAL_SHRINK | F_SEAL_GROW |
	    F_SEAL_SEAL), "seals %d", fcntl(fd, F_GET_SEALS));
	close(fd);
}

static void
t_refused(void)
{
	char path[] = "/tmp/memfdtest.XXXXXX";
	struct statvfs sv;
	void *p;
	int fd, pfd[2];
	char buf[8];

	/* A file is still not mapped shared and writable. */
	if ((fd = mkstemp(path)) >= 0) {
		ftruncate(fd, PAGE);
		p = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		check("a file: MAP_SHARED|PROT_WRITE still refused",
		    p == MAP_FAILED, NULL);
		if (p != MAP_FAILED)
			munmap(p, PAGE);
		p = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, fd, 0);
		check("a file: a private mapping still works",
		    p != MAP_FAILED, "%s", strerror(errno));
		if (p != MAP_FAILED)
			munmap(p, PAGE);
		close(fd);
		unlink(path);
	}

	/* A pipe: refused, and VFS is still here to refuse the next. */
	if (pipe(pfd) == 0) {
		p = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, pfd[0], 0);
		check("mmap of a pipe is refused", p == MAP_FAILED, NULL);
		check("fstatvfs of a pipe is refused",
		    fstatvfs(pfd[0], &sv) == -1, NULL);
		check("and VFS is alive after both", write(pfd[1], "x", 1) == 1,
		    "%s", strerror(errno));
		close(pfd[0]);
		close(pfd[1]);
	}

	/* read(2) and write(2): not supported, and say so. */
	fd = memfd_create("rw", 0);
	ftruncate(fd, PAGE);
	check("read(2) on a memory object: an error, not a hang",
	    read(fd, buf, sizeof(buf)) == -1, NULL);
	check("write(2) on a memory object: an error, not a hang",
	    write(fd, buf, sizeof(buf)) == -1, NULL);
	check("an unknown flag is refused", memfd_create("x", 0x80) == -1 &&
	    errno == EINVAL, "%s", strerror(errno));
	close(fd);
}

/* Many objects one after another: no inode, page or descriptor leaks. */
static void
t_churn(void)
{
	long before, after;
	int i, fd;
	char *p;

	before = settled_free();
	for (i = 0; i < 2000; i++) {
		if ((fd = memfd_create("churn", 0)) < 0)
			break;
		if (ftruncate(fd, 16 * PAGE) != 0)
			break;
		p = mmap(NULL, 16 * PAGE, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, 0);
		if (p == MAP_FAILED)
			break;
		memset(p, i, 16 * PAGE);
		munmap(p, 16 * PAGE);
		close(fd);
	}
	check("2000 objects made, used and dropped", i == 2000,
	    "stopped at %d: %s", i, strerror(errno));
	after = settled_free();
	check("and their pages came back", after >= before - 64,
	    "free pages %ld before, %ld after", before, after);
}

/* A process that exits holding a mapping and a descriptor. */
static void
t_exit(void)
{
	long before, after;
	int status;
	pid_t pid;

	before = settled_free();
	if ((pid = fork()) == 0) {
		int fd = memfd_create("exit", 0);
		char *p;

		if (fd < 0 || ftruncate(fd, BIG) != 0)
			_exit(1);
		p = mmap(NULL, BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED)
			_exit(2);
		memset(p, 1, BIG);
		_exit(0);
	}
	waitpid(pid, &status, 0);
	after = settled_free();
	check("exit with a mapping and a descriptor gives the pages back",
	    WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
	    after >= before - 64, "free pages %ld before, %ld after, status %d",
	    before, after, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}

int
main(int argc, char **argv)
{
	gid_t g = 9999;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-q") == 0)
			quiet = 1;
		else if (strcmp(argv[i], "-u") == 0) {
			if (setgroups(1, &g) != 0 || setgid(9999) != 0 ||
			    setuid(9999) != 0)
				err(2, "cannot drop privileges");
		}
	}
	printf("MEMFD running as uid %d\n", (int)getuid());

	t_pass();
	t_private();
	t_fork();
	t_trunc();
	t_seals();
	t_refused();
	t_churn();
	t_exit();

	printf("MEMFD checks %d failed %d\n", checks, fails);
	return fails ? 1 : 0;
}
