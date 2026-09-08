/*
 * pairsel.c - socketpair(2) + select(2)/ppoll(2) без sshd.
 *
 * Зачем. На плате первая сессия ssh проходит, каждая следующая не получает
 * баннера: TCP устанавливается, а sshd молчит. Listener OpenSSH отдаёт
 * ребёнку конфигурацию только когда ppoll(2) сообщит POLLOUT на его конце
 * socketpair; ppoll у MEECHO стоит на poll, poll - на select. Значит вопрос:
 * говорит ли select() "можно писать" на конце пары uds - и продолжает ли
 * говорить это на ВТОРОЙ паре, после того как первая отработала и закрыта.
 *
 * Программа воспроизводит ровно то, что делает sshd, два раза подряд:
 *   пара -> select(writefds) -> write(конфиг) -> читатель читает -> закрыть
 * и печатает, что ответил select и ppoll на каждом шаге. Никакого sshd,
 * никакой сети: только uds, libc и VFS.
 *
 * Собирается статически против DESTDIR, как hello-world этапа 1.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int
sel_writable(int fd, int ms)
{
	fd_set wr;
	struct timeval tv;
	int r;

	FD_ZERO(&wr);
	FD_SET(fd, &wr);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	r = select(fd + 1, NULL, &wr, NULL, &tv);
	if (r < 0)
		return -errno;
	return r > 0 && FD_ISSET(fd, &wr);
}

static int
pp_writable(int fd, int ms)
{
	struct pollfd p;
	struct timespec ts;
	sigset_t mask;
	int r;

	p.fd = fd;
	p.events = POLLOUT;
	p.revents = 0;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	sigemptyset(&mask);
	r = ppoll(&p, 1, &ts, &mask);
	if (r < 0)
		return -errno;
	return r > 0 && (p.revents & POLLOUT) != 0;
}

static void
one_round(int round)
{
	int sv[2];
	char buf[4096];
	ssize_t n;
	int r;

	printf("== round %d ==\n", round);
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		printf("  socketpair: %s\n", strerror(errno));
		return;
	}
	printf("  pair: %d <-> %d\n", sv[0], sv[1]);

	r = sel_writable(sv[0], 1000);
	printf("  select(writable sv[0]) before write: %d%s\n", r,
	    r < 0 ? strerror(-r) : "");
	r = pp_writable(sv[0], 1000);
	printf("  ppoll (POLLOUT sv[0])   before write: %d%s\n", r,
	    r < 0 ? strerror(-r) : "");

	memset(buf, 'c', sizeof(buf));
	n = write(sv[0], buf, sizeof(buf));
	printf("  write 4096 -> %ld%s\n", (long)n,
	    n < 0 ? strerror(errno) : "");

	r = sel_writable(sv[0], 1000);
	printf("  select(writable sv[0]) after write, before read: %d\n", r);

	n = read(sv[1], buf, sizeof(buf));
	printf("  read  <- %ld%s\n", (long)n, n < 0 ? strerror(errno) : "");

	r = sel_writable(sv[0], 1000);
	printf("  select(writable sv[0]) after read: %d\n", r);
	r = pp_writable(sv[1], 1000);
	printf("  ppoll (POLLOUT sv[1])   after read: %d\n", r);

	close(sv[0]);
	close(sv[1]);
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	one_round(1);
	one_round(2);
	one_round(3);
	printf("done\n");
	return 0;
}
