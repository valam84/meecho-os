/*
 * The client of cdevfdtest (cdevfdtest.c): what a descriptor VFS made for
 * a driver (cdevfd(2)) is to the process that got it - and to VFS, which
 * must count it like any open file: dup, fork, exec with and without
 * close-on-exec, a pass over a socket (SCM_RIGHTS), close; lseek(2) to its
 * end; fstat(2); an ioctl on it reaching the driver with its minor; a
 * descriptor made through an ioctl on another such descriptor; the
 * refusals.  Each check prints a line; the last line counts them.
 *
 *	cdevfdt [/dev/cdevfdtest]
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdevfd.h"

static int checks, failures, ctl;

static void
check(int ok, const char *fmt, ...)
{
	va_list ap;

	checks++;
	if (!ok)
		failures++;
	printf("%s ", ok ? "ok  " : "FAIL");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

static int
cft(int fd, unsigned long req, struct cft *a)
{
	return ioctl(fd, req, a);
}

/* A new descriptor of minor m, made by the driver through cdevfd(2). */
static int
new_fd(int via, int m, int flags, uint64_t size)
{
	struct cft a;

	memset(&a, 0, sizeof(a));
	a.minor = m;
	a.flags = flags;
	a.size = size;
	if (cft(via, CFT_NEW, &a) != 0)
		return -1000 - errno;
	return a.result;
}

static int
get_minor(int c, int fd)
{
	struct cft a;

	memset(&a, 0, sizeof(a));
	a.fd = fd;
	if (cft(c, CFT_GET, &a) != 0)
		return -1000 - errno;
	return a.result;
}

static int
opens(int m)
{
	struct cft a;

	memset(&a, 0, sizeof(a));
	a.minor = m;
	if (cft(ctl, CFT_OPENS, &a) != 0)
		return -1000 - errno;
	return a.result;
}

static int
who(int fd)
{
	struct cft a;

	memset(&a, 0, sizeof(a));
	if (cft(fd, CFT_WHO, &a) != 0)
		return -1000 - errno;
	return a.result;
}

static void
basics(void)
{
	struct stat st;
	int fd, fd2, d, n;

	fd = new_fd(ctl, CFT_OBJ, O_RDWR | O_CLOEXEC, 0x4000);
	check(fd >= 0, "new descriptor of minor %d: %d", CFT_OBJ, fd);
	if (fd < 0)
		return;
	check(fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) &&
	    major(st.st_rdev) == CFT_MAJOR && minor(st.st_rdev) == CFT_OBJ &&
	    st.st_uid == geteuid(), "fstat: character device %d,%d, mode 0%o, "
	    "uid %d", major(st.st_rdev), minor(st.st_rdev),
	    st.st_mode & 07777, (int)st.st_uid);
	check(lseek(fd, 0, SEEK_END) == 0x4000 && lseek(fd, 0, SEEK_SET) == 0,
	    "lseek to its end: 0x4000, and back");
	check((fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0, "close-on-exec set");
	check((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR, "open for reading "
	    "and writing");
	check(opens(CFT_OBJ) == 1, "the driver counts one open: %d",
	    opens(CFT_OBJ));
	check(who(fd) == CFT_OBJ, "an ioctl on it reaches minor %d",
	    who(fd));
	check(get_minor(ctl, fd) == CFT_OBJ, "the driver names it: minor %d",
	    get_minor(ctl, fd));
	check(get_minor(ctl, ctl) == 0, "and the control node: minor 0");
	d = open("/dev/null", O_RDONLY);
	check(get_minor(ctl, d) == -EINVAL, "another device's: %d (-EINVAL)",
	    get_minor(ctl, d));
	close(d);
	check(get_minor(ctl, 200) == -EBADF, "a closed number: %d (-EBADF)",
	    get_minor(ctl, 200));

	fd2 = new_fd(ctl, CFT_OBJ, O_RDONLY, 0);
	check(fd2 >= 0 && opens(CFT_OBJ) == 2, "a second one of the same "
	    "minor: two opens (%d)", opens(CFT_OBJ));
	check((fcntl(fd2, F_GETFL) & O_ACCMODE) == O_RDONLY &&
	    (fcntl(fd2, F_GETFD) & FD_CLOEXEC) == 0, "read-only, no "
	    "close-on-exec");
	check(lseek(fd2, 0, SEEK_END) == 0, "size 0");
	close(fd2);
	check(opens(CFT_OBJ) == 1, "closed: one open (%d)", opens(CFT_OBJ));

	d = dup(fd);
	close(fd);
	check(opens(CFT_OBJ) == 1 && who(d) == CFT_OBJ, "dup, the original "
	    "closed: still open, still minor %d", who(d));
	close(d);
	check(opens(CFT_OBJ) == 0, "the dup closed: no opens (%d)",
	    opens(CFT_OBJ));

	/* Through an ioctl on an object descriptor, not the control node. */
	fd = new_fd(ctl, CFT_OBJ + 1, O_RDWR, 0);
	fd2 = new_fd(fd, CFT_OBJ + 2, O_RDWR, 0);
	check(fd2 >= 0 && who(fd2) == CFT_OBJ + 2, "made through an ioctl on "
	    "another one: minor %d", who(fd2));
	close(fd);
	close(fd2);
	check(opens(CFT_OBJ + 1) == 0 && opens(CFT_OBJ + 2) == 0, "both "
	    "closed");

	/* Until the table is full. */
	for (n = 0; (d = new_fd(ctl, CFT_OBJ + 3, O_RDWR, 0)) >= 0; n++)
		;
	check(d == -EMFILE && opens(CFT_OBJ + 3) == n, "until the table is "
	    "full: %d made, then %d (-EMFILE), the driver counts %d", n, d,
	    opens(CFT_OBJ + 3));
	for (d = 3; d < 256; d++)
		if (d != ctl && d != fileno(stdout))
			(void)close(d);
	check(opens(CFT_OBJ + 3) == 0, "all closed: %d", opens(CFT_OBJ + 3));
}

static void
processes(void)
{
	struct msghdr msg;
	struct cmsghdr *cm;
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} cbuf;
	struct iovec iov;
	char c = 'x';
	int fd, sv[2], st, m = CFT_OBJ + 10;
	pid_t pid;

	/* fork: the child has it too; the open goes when both closed. */
	fd = new_fd(ctl, m, O_RDWR, 0);
	if ((pid = fork()) == 0)
		_exit(who(fd) == m ? 0 : 1);
	close(fd);
	waitpid(pid, &st, 0);
	check(WIFEXITED(st) && WEXITSTATUS(st) == 0 && opens(m) == 0,
	    "fork: the child reaches minor %d, the open goes with both (%d)",
	    m, opens(m));

	/* Over a socket, to a process that never had it. */
	m++;
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	if ((pid = fork()) == 0) {
		int c2, rfd;

		close(sv[0]);
		close(ctl);
		memset(&msg, 0, sizeof(msg));
		iov.iov_base = &c;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cbuf.buf;
		msg.msg_controllen = sizeof(cbuf.buf);
		if (recvmsg(sv[1], &msg, 0) != 1 ||
		    (cm = CMSG_FIRSTHDR(&msg)) == NULL)
			_exit(2);
		memcpy(&rfd, CMSG_DATA(cm), sizeof(rfd));
		c2 = open("/dev/cdevfdtest", O_RDWR);
		_exit(who(rfd) == m && get_minor(c2, rfd) == m ? 0 : 1);
	}
	close(sv[1]);
	fd = new_fd(ctl, m, O_RDWR, 0);
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = &c;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf.buf;
	msg.msg_controllen = CMSG_LEN(sizeof(int));
	cm = CMSG_FIRSTHDR(&msg);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &fd, sizeof(fd));
	check(sendmsg(sv[0], &msg, 0) == 1, "sent over a socket");
	close(fd);
	waitpid(pid, &st, 0);
	close(sv[0]);
	check(WIFEXITED(st) && WEXITSTATUS(st) == 0 && opens(m) == 0,
	    "the receiver reaches minor %d and the driver names it there; the "
	    "open goes with it (%d, status %d)", m, opens(m),
	    WIFEXITED(st) ? WEXITSTATUS(st) : -1);

	/* exec: close-on-exec closes it, without it stays. */
	m++;
	if ((pid = fork()) == 0) {
		fd = new_fd(ctl, m, O_RDWR | O_CLOEXEC, 0);
		(void)new_fd(ctl, m + 1, O_RDWR, 0);
		execl("/bin/sleep", "sleep", "2", (char *)NULL);
		_exit(1);
	}
	sleep(1);
	check(opens(m) == 0 && opens(m + 1) == 1, "exec: the close-on-exec "
	    "one is closed (%d), the other open (%d)", opens(m), opens(m + 1));
	waitpid(pid, &st, 0);
	check(opens(m + 1) == 0, "and that one goes when the process does "
	    "(%d)", opens(m + 1));
}

static void
refusals(void)
{
	static const struct {
		const char *what;
		int want;
	} t[8] = {
		{ "the driver itself, not waiting on it", -EPERM },
		{ "init, not waiting on it", -EPERM },
		{ "flags O_APPEND", -EINVAL },
		{ "minor -1", -EINVAL },
		{ "minor 0x200000, past 20 bits", -EINVAL },
		{ "descriptor -1", -EBADF },
		{ "descriptor OPEN_MAX", -EBADF },
		{ "a size past off_t", -EINVAL },
	};
	struct cft a;
	int i;

	memset(&a, 0, sizeof(a));
	check(cft(ctl, CFT_PROBE, &a) == 0, "the driver tries what is refused");
	for (i = 0; i < 8; i++)
		check(a.r[i] == t[i].want, "%s: %d (want %d)", t[i].what,
		    a.r[i], t[i].want);
	check(a.result == 0, "the driver saw no close it did not make (%d)",
	    a.result);
	check(opens(CFT_OBJ) == 0, "nothing left open");
}

int
main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/cdevfdtest";

	if ((ctl = open(dev, O_RDWR)) < 0) {
		perror(dev);
		return 1;
	}
	basics();
	processes();
	refusals();
	printf("CDEVFDT %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
