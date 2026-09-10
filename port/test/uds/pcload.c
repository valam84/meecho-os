/*
 * pcload - the shape of socklib_producer_consumer() with the seed on the
 * command line.
 *
 * minix/tests/test90 subtest 4 kills the UDS service every so often: the
 * segment header at the tail of a receive buffer reads back as nonsense and
 * uds_fetch_hdr() asserts.  The test cannot be used to chase that, for two
 * reasons.  It seeds its random stream from time(NULL), so no two runs walk
 * the same path, and it pushes 16 MiB in each direction, which on QEMU takes
 * twenty-one minutes - measured, not guessed.  Roughly one run in sixteen
 * trips the assertion, and a run that does cannot be repeated.
 *
 * So: same mix of send and receive sizes, same flags, same FIONREAD checks,
 * but the seed is an argument and the volume is an argument, and a seed that
 * fails fails again.
 *
 *	pcload [seed [kbytes]]
 *
 * Exit status is 0 if the whole transfer went through and the data came out
 * the way it went in, 1 if it did not, and 2 if a socket call failed - which
 * is what a restarted UDS looks like from here (EIO on every call).
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t transfer_size = 1024 * 1024;
static int errs;

static void
fail(const char *what, int line)
{

	printf("pcload: %s failed at line %d: %s\n", what, line,
	    strerror(errno));

	if (++errs > 5) {
		printf("pcload: too many errors\n");
		exit(2);
	}
}

#define FAIL(what)	fail(what, __LINE__)

/* The byte the producer puts at absolute offset 'off' of the stream. */
static char
stream_byte(size_t off)
{

	return (char)(off + (off >> 16));
}

static void
consumer(int fd, long seed, socklen_t size)
{
	char *buf;
	size_t off;
	socklen_t len;
	ssize_t r, i;
	int num, flags, exp;

	if ((buf = malloc(size)) == NULL) {
		printf("pcload: out of memory\n");
		exit(2);
	}

	srand48(seed + 1);

	for (off = 0; off < transfer_size; ) {
		if (off < transfer_size / 2)
			len = lrand48() %
			    ((off / (transfer_size / 8) % 2) ? 64 : 256);
		else
			len = lrand48() % size;

		num = lrand48() % 16;
		flags = 0;
		if (num & 1) flags |= MSG_PEEK;
		if (num & 2) flags |= MSG_WAITALL;
		if (num & 4) flags |= MSG_DONTWAIT;
		if (num & 8) {
			if (ioctl(fd, FIONREAD, &exp) != 0) FAIL("FIONREAD");
		} else
			exp = -1;

		if ((r = recv(fd, buf, len, flags)) == -1) {
			if (errno != EWOULDBLOCK) FAIL("recv");
			continue;
		}

		for (i = 0; i < r; i++)
			if (buf[i] != stream_byte(off + i)) {
				printf("pcload: byte %u of the stream is "
				    "0x%02x, wanted 0x%02x\n",
				    (unsigned int)(off + i),
				    (unsigned char)buf[i],
				    (unsigned char)stream_byte(off + i));
				exit(1);
			}

		if (!(flags & MSG_PEEK))
			off += r;
	}

	free(buf);

	if (close(fd) != 0) FAIL("close");

	exit(errs != 0);
}

int
main(int argc, char ** argv)
{
	char *buf;
	size_t off;
	socklen_t len, size;
	ssize_t r;
	size_t i;
	int fd[2], rcvlen, flags, status;
	long seed;
	pid_t pid;

	seed = (argc > 1) ? atol(argv[1]) : 1;
	if (argc > 2)
		transfer_size = (size_t)atol(argv[2]) * 1024;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
		FAIL("socketpair");
		return 2;
	}

	len = sizeof(rcvlen);
	if (getsockopt(fd[0], SOL_SOCKET, SO_RCVBUF, &rcvlen, &len) != 0)
		FAIL("SO_RCVBUF");

	size = rcvlen * 3;

	if ((buf = malloc(size)) == NULL) {
		printf("pcload: out of memory\n");
		return 2;
	}

	switch ((pid = fork())) {
	case -1:
		FAIL("fork");
		return 2;
	case 0:
		errs = 0;
		if (close(fd[0]) != 0) FAIL("close");
		consumer(fd[1], seed, size);
		/* NOTREACHED */
	}

	if (close(fd[1]) != 0) FAIL("close");

	srand48(seed);

	for (off = 0; off < transfer_size; ) {
		if (off < transfer_size / 4 ||
		    (off >= transfer_size / 2 && off < transfer_size * 3 / 4))
			len = lrand48() % 64;
		else
			len = lrand48() % size;

		if (len > transfer_size - off)
			len = transfer_size - off;

		for (i = 0; i < len; i++)
			buf[i] = stream_byte(off + i);

		flags = (lrand48() % 2) ? MSG_DONTWAIT : 0;

		r = send(fd[0], buf, len, flags);

		if (r != (ssize_t)len) {
			if (r > (ssize_t)len) FAIL("send too long");
			if (!(flags & MSG_DONTWAIT)) FAIL("short blocking send");
			if (r == -1) {
				if (errno != EWOULDBLOCK) FAIL("send");
				r = 0;
			}
		}

		/*
		 * The original stops for a second at every quarter of the
		 * transfer, which lets the consumer drain the buffer
		 * completely.  Keep it: emptying the buffer is a state the
		 * producer/consumer pair otherwise rarely reaches.
		 */
		if (off / (transfer_size / 4) !=
		    (off + r) / (transfer_size / 4))
			sleep(1);

		off += r;
	}

	free(buf);

	if (close(fd[0]) != 0) FAIL("close");

	if (waitpid(pid, &status, 0) != pid) FAIL("waitpid");

	if (errs != 0)
		return 2;

	if (!WIFEXITED(status)) {
		printf("pcload: seed %ld: the consumer died\n", seed);
		return 1;
	}

	if (WEXITSTATUS(status) != 0) {
		printf("pcload: seed %ld: the consumer says %d\n", seed,
		    WEXITSTATUS(status));
		return WEXITSTATUS(status);
	}

	printf("pcload: seed %ld: %u KiB through, stream intact\n", seed,
	    (unsigned int)(transfer_size / 1024));
	return 0;
}
