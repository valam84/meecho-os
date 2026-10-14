/* Tests for job control: stop signals, SIGCONT and wait4 - for MEECHO.
 *
 * A stop signal with the default action stops the process until SIGCONT;
 * the parent hears of both through waitpid(WUNTRACED/WCONTINUED) and
 * SIGCHLD.  Upstream MINIX ended the process instead.
 *
 * "Makes no progress" is checked through a pipe: the children echo bytes,
 * and a byte written to a stopped child stays unanswered until SIGCONT.  The
 * state letter is the one ps(1) shows, from /proc/<pid>/psinfo.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#define ITERATIONS	2
#define WAIT_MS		3000	/* for what must happen */
#define QUIET_MS	300	/* for what must not happen */

static int p2c[2], c2p[2];	/* parent to child, child to parent */
static int sigfd;		/* where the child's handlers write */
static volatile int nchld;

/* The state letter of the process in /proc/<pid>/psinfo, or 0 if gone. */
static char
pstate(pid_t pid)
{
	char path[32], buf[256], name[64], type, st;
	int fd, n, ver, ep;

	snprintf(path, sizeof(path), "/proc/%d/psinfo", (int)pid);
	if ((fd = open(path, O_RDONLY)) < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';
	if (sscanf(buf, "%d %c %d %63s %c", &ver, &type, &ep, name, &st) != 5)
		return 0;
	return st;
}

/* Wait for the process to show the state letter - or, with 'not', any other.
 * MIB copies the process tables at most once a clock tick, so what psinfo
 * says may be a tick old.
 */
static int
await_state2(pid_t pid, char want, int not)
{
	int i;

	for (i = 0; i < WAIT_MS / 10; i++) {
		if ((pstate(pid) == want) != not)
			return 1;
		usleep(10000);
	}
	return 0;
}

static int
await_state(pid_t pid, char want)
{
	return await_state2(pid, want, 0);
}

/* Wait for the process to be gone from the process table. */
static int
await_gone(pid_t pid)
{
	int i;

	for (i = 0; i < WAIT_MS / 10 && kill(pid, 0) == 0; i++)
		usleep(10000);
	return kill(pid, 0) == -1 && errno == ESRCH;
}

static int
readable(int fd, int ms)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	return poll(&pfd, 1, ms) == 1;
}

/* Read one byte, which must come and must be the expected one. */
static int
get_byte(int fd, char want)
{
	char c;

	if (!readable(fd, WAIT_MS))
		return 0;
	return read(fd, &c, 1) == 1 && c == want;
}

static void
put_byte(int fd, char c)
{
	if (write(fd, &c, 1) != 1)
		e(900);
}

static void
open_pipes(void)
{
	if (pipe(p2c) != 0 || pipe(c2p) != 0)
		e(901);
}

/* In the parent: keep the parent's ends. */
static void
parent_ends(void)
{
	close(p2c[0]);
	close(c2p[1]);
}

static void
close_pipes(void)
{
	close(p2c[1]);
	close(c2p[0]);
}

/* A child that answers each byte with the same byte, and exits 0 on EOF.  A
 * caught signal may interrupt its read.
 */
static void
echo_child(void)
{
	ssize_t n;
	char c;

	close(p2c[1]);
	close(c2p[0]);
	put_byte(c2p[1], 'r');
	for (;;) {
		n = read(p2c[0], &c, 1);
		if (n == -1 && errno == EINTR)
			continue;
		if (n != 1)
			break;
		if (write(c2p[1], &c, 1) != 1)
			_exit(1);
	}
	_exit(0);
}

static pid_t
fork_echo(void (*setup)(void))
{
	pid_t pid;

	open_pipes();
	if ((pid = fork()) == -1)
		e(902);
	if (pid == 0) {
		if (setup != NULL)
			setup();
		echo_child();
	}
	parent_ends();
	if (!get_byte(c2p[0], 'r'))
		e(903);
	return pid;
}

/* Stop the child and see the stop reported. */
static void
stop_child(pid_t pid, int sig)
{
	int st;

	if (kill(pid, sig) != 0)
		e(910);
	if (waitpid(pid, &st, WUNTRACED) != pid)
		e(911);
	if (!WIFSTOPPED(st) || WSTOPSIG(st) != sig || WIFCONTINUED(st) ||
	    WIFEXITED(st) || WIFSIGNALED(st))
		e(912);
}

/* A stopped echo child neither answers nor is gone; after SIGCONT it does. */
static void
check_frozen(pid_t pid, char c)
{
	put_byte(p2c[1], c);
	if (readable(c2p[0], QUIET_MS))
		e(920);
	if (pstate(pid) != 'T')
		e(921);
	if (kill(pid, 0) != 0)
		e(922);
}

/* Close the echo child's input and see it exit 0. */
static void
end_echo(pid_t pid)
{
	int st;

	close_pipes();
	if (waitpid(pid, &st, 0) != pid)
		e(930);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		e(931);
}

/* SIGSTOP stops, SIGCONT continues; WUNTRACED and WCONTINUED tell each once. */
static void
test95a(void)
{
	pid_t pid;
	int st;

	subtest = 1;

	pid = fork_echo(NULL);
	put_byte(p2c[1], 'a');
	if (!get_byte(c2p[0], 'a')) e(1);

	stop_child(pid, SIGSTOP);
	if (waitpid(pid, &st, WNOHANG | WUNTRACED) != 0) e(3);
	check_frozen(pid, 'b');

	if (kill(pid, SIGCONT) != 0) e(4);
	if (waitpid(pid, &st, WCONTINUED) != pid) e(5);
	if (!WIFCONTINUED(st)) e(6);
	if (WIFSTOPPED(st) || WIFEXITED(st) || WIFSIGNALED(st)) e(7);
	if (waitpid(pid, &st, WNOHANG | WCONTINUED | WUNTRACED) != 0) e(8);
	if (!get_byte(c2p[0], 'b')) e(9);
	if (!await_state2(pid, 'T', 1)) e(10);

	/* The stop leaves no trace in the exit status. */
	end_echo(pid);
}

/* A stop wakes a waitpid(WUNTRACED) that sleeps; without WUNTRACED it is no
 * news.  A continue wakes a waitpid(WCONTINUED).
 */
static void
test95b(void)
{
	pid_t pid, helper;
	int st;
	char c;

	subtest = 2;

	open_pipes();
	if ((pid = fork()) == 0) {
		close(p2c[1]);
		usleep(200000);
		kill(getpid(), SIGTSTP);
		(void)read(p2c[0], &c, 1);
		_exit(3);
	}
	parent_ends();
	if (waitpid(pid, &st, WUNTRACED) != pid) e(1);
	if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGTSTP) e(2);
	if (!await_state(pid, 'T')) e(3);

	/* Someone else continues it while we wait for that. */
	if ((helper = fork()) == 0) {
		usleep(200000);
		_exit(kill(pid, SIGCONT) == 0 ? 0 : 1);
	}
	if (waitpid(pid, &st, WCONTINUED) != pid) e(4);
	if (!WIFCONTINUED(st)) e(5);
	if (waitpid(helper, &st, 0) != helper) e(6);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) e(7);

	put_byte(p2c[1], 'x');
	if (waitpid(pid, &st, 0) != pid) e(8);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 3) e(9);
	close_pipes();

	/* Without WUNTRACED a stopped child is as one still running. */
	if ((pid = fork()) == 0) {
		kill(getpid(), SIGSTOP);
		_exit(4);
	}
	if (!await_state(pid, 'T')) e(10);
	if (waitpid(pid, &st, WNOHANG) != 0) e(11);
	if (waitpid(pid, &st, WNOHANG | WUNTRACED) != pid) e(12);
	if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGSTOP) e(13);
	if (waitpid(pid, &st, WNOHANG | WUNTRACED) != 0) e(14);
	if (kill(pid, SIGCONT) != 0) e(15);
	if (waitpid(pid, &st, 0) != pid) e(16);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 4) e(17);
}

static void
on_chld(int sig)
{
	nchld++;
}

static int
await_count(int want)
{
	int i;

	for (i = 0; i < WAIT_MS / 10 && nchld < want; i++)
		usleep(10000);
	return nchld == want;
}

/* The parent gets SIGCHLD on a stop and on a continue, unless SA_NOCLDSTOP. */
static void
test95c(void)
{
	struct sigaction sa;
	pid_t pid;
	int i, st;
	char c;

	subtest = 3;

	for (i = 0; i < 2; i++) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_chld;
		sa.sa_flags = (i == 0) ? 0 : SA_NOCLDSTOP;
		if (sigaction(SIGCHLD, &sa, NULL) != 0) e(1);
		nchld = 0;

		open_pipes();
		if ((pid = fork()) == 0) {
			close(p2c[1]);
			kill(getpid(), SIGSTOP);
			(void)read(p2c[0], &c, 1);
			_exit(5);
		}
		parent_ends();
		if (!await_state(pid, 'T')) e(2);
		if (i == 0 && !await_count(1)) e(3);
		if (i == 1) {
			usleep(QUIET_MS * 1000);
			if (nchld != 0) e(4);
		}

		if (kill(pid, SIGCONT) != 0) e(5);
		if (i == 0 && !await_count(2)) e(6);
		if (i == 1) {
			usleep(QUIET_MS * 1000);
			if (nchld != 0) e(7);
		}

		put_byte(p2c[1], 'x');
		if (waitpid(pid, &st, 0) != pid) e(8);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 5) e(9);
		close_pipes();
	}
	signal(SIGCHLD, SIG_DFL);
}

static void
on_usr1(int sig)
{
	put_byte(sigfd, 'u');
}

static void
usr1_child(void)
{
	close(p2c[1]);
	close(c2p[0]);
	sigfd = c2p[1];
	signal(SIGUSR1, on_usr1);
	put_byte(c2p[1], 'r');
	for (;;)
		pause();
}

static pid_t
fork_usr1(void)
{
	pid_t pid;

	open_pipes();
	if ((pid = fork()) == 0)
		usr1_child();
	parent_ends();
	if (!get_byte(c2p[0], 'r')) e(940);
	return pid;
}

/* Signals sent to a stopped process wait for SIGCONT; SIGKILL does not. */
static void
test95d(void)
{
	pid_t pid;
	int st;

	subtest = 4;

	pid = fork_usr1();
	stop_child(pid, SIGSTOP);
	if (kill(pid, SIGUSR1) != 0) e(1);
	if (readable(c2p[0], QUIET_MS)) e(2);
	if (pstate(pid) != 'T') e(3);
	if (kill(pid, SIGCONT) != 0) e(4);
	if (!get_byte(c2p[0], 'u')) e(5);

	/* SIGKILL ends a stopped process. */
	stop_child(pid, SIGSTOP);
	if (kill(pid, SIGKILL) != 0) e(6);
	if (waitpid(pid, &st, 0) != pid) e(7);
	if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGKILL) e(8);
	/* The reaped process leaves PM's table once VFS is done with it. */
	if (!await_gone(pid)) e(9);
	close_pipes();

	/* SIGTERM waits, and ends the process once it is continued. */
	pid = fork_usr1();
	stop_child(pid, SIGSTOP);
	if (kill(pid, SIGTERM) != 0) e(10);
	usleep(QUIET_MS * 1000);
	if (kill(pid, 0) != 0) e(11);
	if (pstate(pid) != 'T') e(12);
	if (waitpid(pid, &st, WNOHANG) != 0) e(13);
	if (kill(pid, SIGCONT) != 0) e(14);
	if (waitpid(pid, &st, 0) != pid) e(15);
	if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGTERM) e(16);
	close_pipes();
}

static void
on_cont(int sig)
{
	put_byte(sigfd, 'c');
}

static void
catch_cont(void)
{
	sigfd = c2p[1];
	signal(SIGCONT, on_cont);
}

static void
ignore_cont(void)
{
	signal(SIGCONT, SIG_IGN);
}

static void
block_cont(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGCONT);
	sigprocmask(SIG_BLOCK, &set, NULL);
}

/* SIGCONT continues whatever the process does with it, and a handler for it
 * runs once the process runs.
 */
static void
test95e(void)
{
	pid_t pid;

	subtest = 5;

	pid = fork_echo(catch_cont);
	stop_child(pid, SIGSTOP);
	check_frozen(pid, 'a');
	if (kill(pid, SIGCONT) != 0) e(1);
	/* The handler runs before the read returns, or after; both arrive. */
	if (!readable(c2p[0], WAIT_MS)) e(2);
	{
		char b[2];
		int n = 0;

		while (n < 2 && readable(c2p[0], WAIT_MS))
			if (read(c2p[0], &b[n], 1) == 1)
				n++;
		if (n != 2 || !((b[0] == 'a' && b[1] == 'c') ||
		    (b[0] == 'c' && b[1] == 'a'))) e(3);
	}
	end_echo(pid);

	pid = fork_echo(ignore_cont);
	stop_child(pid, SIGSTOP);
	check_frozen(pid, 'b');
	if (kill(pid, SIGCONT) != 0) e(4);
	if (!get_byte(c2p[0], 'b')) e(5);
	end_echo(pid);

	pid = fork_echo(block_cont);
	stop_child(pid, SIGSTOP);
	check_frozen(pid, 'c');
	if (kill(pid, SIGCONT) != 0) e(6);
	if (!get_byte(c2p[0], 'c')) e(7);
	end_echo(pid);
}

/* SIGCONT discards a stop signal that waits blocked. */
static void
test95f(void)
{
	sigset_t set;
	pid_t pid;
	int st;
	char c;

	subtest = 6;

	open_pipes();
	if ((pid = fork()) == 0) {
		close(p2c[1]);
		close(c2p[0]);
		sigemptyset(&set);
		sigaddset(&set, SIGTSTP);
		sigprocmask(SIG_BLOCK, &set, NULL);
		put_byte(c2p[1], 'r');
		(void)read(p2c[0], &c, 1);
		sigprocmask(SIG_UNBLOCK, &set, NULL);
		put_byte(c2p[1], 'd');
		(void)read(p2c[0], &c, 1);
		_exit(6);
	}
	parent_ends();
	if (!get_byte(c2p[0], 'r')) e(1);
	if (kill(pid, SIGTSTP) != 0) e(2);
	if (kill(pid, SIGCONT) != 0) e(3);
	put_byte(p2c[1], 'x');
	if (!get_byte(c2p[0], 'd')) e(4);
	if (waitpid(pid, &st, WNOHANG | WUNTRACED) != 0) e(5);
	close_pipes();
	if (waitpid(pid, &st, 0) != pid) e(6);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 6) e(7);
}

static void
on_tstp(int sig)
{
	put_byte(sigfd, 't');
}

static void
catch_tstp(void)
{
	struct sigaction sa;

	sigfd = c2p[1];
	signal(SIGTSTP, on_tstp);
	/* SIGSTOP can be neither caught nor ignored. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_tstp;
	(void)sigaction(SIGSTOP, &sa, NULL);
	(void)signal(SIGSTOP, SIG_IGN);
}

/* SIGTSTP, SIGTTIN and SIGTTOU stop by default, and report themselves; a
 * caught SIGTSTP runs its handler, and SIGSTOP cannot be caught.
 */
static void
test95g(void)
{
	static const int sigs[] = { SIGTSTP, SIGTTIN, SIGTTOU };
	pid_t pid;
	int i, st;

	subtest = 7;

	for (i = 0; i < 3; i++) {
		pid = fork_echo(NULL);
		stop_child(pid, sigs[i]);
		check_frozen(pid, 'a' + i);
		if (kill(pid, SIGCONT) != 0) e(1);
		if (!get_byte(c2p[0], 'a' + i)) e(2);
		end_echo(pid);
	}

	pid = fork_echo(catch_tstp);
	if (kill(pid, SIGTSTP) != 0) e(3);
	if (!get_byte(c2p[0], 't')) e(4);
	if (waitpid(pid, &st, WNOHANG | WUNTRACED) != 0) e(5);
	if (pstate(pid) == 'T') e(6);
	stop_child(pid, SIGSTOP);
	check_frozen(pid, 's');
	if (kill(pid, SIGCONT) != 0) e(7);
	if (!get_byte(c2p[0], 's')) e(8);
	end_echo(pid);
}

/* A stopped process whose parent exits gets SIGHUP and SIGCONT: it dies of
 * the hangup by default, and runs on if it ignores it.
 */
static void
test95h(void)
{
	pid_t mid, pid;
	int st, ign;
	char c;

	subtest = 8;

	for (ign = 0; ign < 2; ign++) {
		open_pipes();
		if ((mid = fork()) == 0) {
			close(p2c[1]);
			close(c2p[0]);
			if ((pid = fork()) == 0) {
				if (ign)
					signal(SIGHUP, SIG_IGN);
				kill(getpid(), SIGSTOP);
				put_byte(c2p[1], 'g');
				(void)read(p2c[0], &c, 1);
				_exit(0);
			}
			if (waitpid(pid, &st, WUNTRACED) != pid ||
			    !WIFSTOPPED(st))
				_exit(1);
			if (write(c2p[1], &pid, sizeof(pid)) != sizeof(pid))
				_exit(2);
			_exit(0);
		}
		parent_ends();
		if (!readable(c2p[0], WAIT_MS) ||
		    read(c2p[0], &pid, sizeof(pid)) != sizeof(pid)) {
			e(1);
			break;
		}
		if (waitpid(mid, &st, 0) != mid) e(2);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) e(3);

		if (!ign) {
			/* INIT collects it once the hangup has ended it. */
			if (!await_gone(pid)) e(4);
		} else {
			if (!get_byte(c2p[0], 'g')) e(5);
			put_byte(p2c[1], 'x');
		}
		close_pipes();
	}
}

int
main(int argc, char **argv)
{
	static const int sigs[] = { SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT, SIGCHLD,
	    SIGHUP, SIGUSR1, SIGTERM };
	sigset_t set;
	int i, m = 0xFF;

	/* Whatever the shell left ignored or blocked, the children start from
	 * the defaults.
	 */
	for (i = 0; i < (int)(sizeof(sigs) / sizeof(sigs[0])); i++)
		signal(sigs[i], SIG_DFL);
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);

	/* A child that dies early must show up as an error, not end the test
	 * with SIGPIPE and the errors still in the stdio buffer.
	 */
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);

	start(95);

	if (argc == 2)
		m = atoi(argv[1]);

	for (i = 0; i < ITERATIONS; i++) {
		if (m & 0x01) test95a();
		if (m & 0x02) test95b();
		if (m & 0x04) test95c();
		if (m & 0x08) test95d();
		if (m & 0x10) test95e();
		if (m & 0x20) test95f();
		if (m & 0x40) test95g();
		if (m & 0x80) test95h();
	}

	quit();
	return 0;
}
