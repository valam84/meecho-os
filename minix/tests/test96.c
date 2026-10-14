/* Tests for process groups, sessions and the controlling terminal - for
 * MEECHO.
 *
 * Upstream MINIX had no process groups apart from sessions: setpgid() was
 * setsid() under another name, and the terminal drivers knew no foreground
 * process group.  Now PM keeps both with POSIX's rules, and tty and pty ask
 * PM about the foreground (PM_JOBCTL).  What is tested:
 *
 *   1  getpgid(), getsid(), setpgid() and setsid(), their errors, and kill()
 *      and waitpid() of a process group.
 *   2  a pseudo terminal as the controlling terminal of a session:
 *      tcgetsid(), tcgetpgrp(), tcsetpgrp() and their errors; a background
 *      read stops with SIGTTIN and goes on in the foreground; SUSP stops the
 *      foreground job, whose read steps aside for the shell's; INTR ends the
 *      foreground and nothing else; TOSTOP, and tcsetattr() from the
 *      background; SIGTTIN ignored or blocked and an orphaned group give EIO.
 *   3  hangups: the session leader's exit hangs up the foreground group and
 *      not the background; the master side's close, the session leader.
 *
 * The terminal is not the test's: the test belongs to the session of the
 * shell that runs it, and that shell's terminal is not to be touched.  A
 * child makes a session of its own, with a pty for its terminal, plays the
 * shell there - SIGTTOU ignored, as a shell with job control has it - and
 * types on the master side itself.  The errors of the children come back
 * through a pipe.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "common.h"

#define ITERATIONS	2
#define WAIT_MS		5000	/* for what must happen */
#define QUIET_MS	300	/* for what must not happen */

static int rep[2];		/* the children's errors, to the test */
static int ack[2];		/* the test's go-ahead, to the session */
static int mfd;			/* the master side of the session's pty */
static int tfd;			/* the test's own fd for the slave side */
static char slave[64];		/* the slave side's name */
static pid_t tpgrp;		/* the test's process group */
static volatile int got_int;

static void
on_alrm(int sig)
{
}

static void
on_int(int sig)
{
	got_int = 1;
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

/* In a child: report an error.  0 asks the test for its own checks. */
static void
ce(int n)
{
	(void)write(rep[1], &n, sizeof(n));
}

/* waitpid() that gives up after a while: a process that does not stop or end
 * as it should must not hang the test.
 */
static int
waitfor(pid_t pid, int *st, int options)
{
	int r;

	alarm(WAIT_MS / 1000 + 1);
	r = waitpid(pid, st, options);
	alarm(0);
	return r;
}

static int
waitstop(pid_t pid, int sig)
{
	int st;

	return waitfor(pid, &st, WUNTRACED) == pid && WIFSTOPPED(st) &&
	    WSTOPSIG(st) == sig;
}

static int
waitexit(pid_t pid, int code)
{
	int st;

	return waitfor(pid, &st, 0) == pid && WIFEXITED(st) &&
	    WEXITSTATUS(st) == code;
}

static int
waitsig(pid_t pid, int sig)
{
	int st;

	return waitfor(pid, &st, 0) == pid && WIFSIGNALED(st) &&
	    WTERMSIG(st) == sig;
}

/* End a child for good and collect it. */
static void
reap(pid_t pid)
{
	int st;

	kill(pid, SIGKILL);
	(void)waitfor(pid, &st, 0);
}

static void
block(int sig)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig);
	sigprocmask(SIG_BLOCK, &set, NULL);
}

/* Check a child's exit status: 0, or the number of the check it failed. */
static void
child_ok(pid_t pid, int base)
{
	int st;

	if (waitfor(pid, &st, 0) != pid)
		e(base);
	else if (!WIFEXITED(st))
		e(base + 99);
	else if (WEXITSTATUS(st) != 0)
		e(base + WEXITSTATUS(st));
}

/* Process groups and sessions, without a terminal. */
static void
test96a(void)
{
	pid_t me, sid, a, b, c, g1, g2;
	int i, st, go[2], ex[2], rd[2];
	char ch;

	subtest = 1;
	me = getpid();
	sid = getsid(0);

	if (getpgid(0) != tpgrp) e(1);
	if (getpgid(me) != tpgrp) e(2);
	if (sid <= 0 || getsid(me) != sid) e(3);
	if (setpgid(0, -1) != -1 || errno != EINVAL) e(4);
	if (setpgid(1, 1) != -1 || errno != ESRCH) e(5);	/* not our child */

	/* A child that has not exec()ed: the parent moves it to a group of
	 * its own and back; once it has, no more.  The exec closes a pipe.
	 */
	if (pipe(go) != 0 || pipe(ex) != 0) e(6);
	if ((a = fork()) == 0) {
		close(go[1]);
		close(ex[0]);
		fcntl(ex[1], F_SETFD, FD_CLOEXEC);
		if (read(go[0], &ch, 1) != 1)
			_exit(1);
		dup2(go[0], 0);
		execl("/bin/cat", "cat", (char *)NULL);
		_exit(2);
	}
	close(go[0]);
	close(ex[1]);
	if (setpgid(a, a) != 0) e(7);
	if (getpgid(a) != a) e(8);
	if (getsid(a) != sid) e(9);
	if (setpgid(a, tpgrp) != 0 || getpgid(a) != tpgrp) e(10);
	if (setpgid(a, 0) != 0 || getpgid(a) != a) e(11);
	if (write(go[1], "g", 1) != 1) e(12);
	if (read(ex[0], &ch, 1) != 0) e(13);	/* EOF: exec()ed */
	if (setpgid(a, tpgrp) != -1 || errno != EACCES) e(14);
	close(go[1]);			/* cat reads EOF */
	close(ex[0]);
	if (!waitexit(a, 0)) e(15);

	/* A session leader stays the leader of its group, and its session is
	 * not ours; a process of another session cannot join our group.
	 */
	if (pipe(rd) != 0 || pipe(go) != 0) e(20);
	if ((b = fork()) == 0) {
		close(rd[0]);
		close(go[1]);
		if (setsid() != getpid()) _exit(1);
		if (getsid(0) != getpid() || getpgrp() != getpid()) _exit(2);
		if (setpgid(0, 0) != -1 || errno != EPERM) _exit(3);
		if ((c = fork()) == 0) {
			if (setpgid(0, tpgrp) != -1 || errno != EPERM)
				_exit(1);
			_exit(0);
		}
		if (!waitexit(c, 0)) _exit(4);
		if (write(rd[1], "r", 1) != 1) _exit(5);
		(void)read(go[0], &ch, 1);
		_exit(0);
	}
	close(rd[1]);
	close(go[0]);
	if (!readable(rd[0], WAIT_MS) || read(rd[0], &ch, 1) != 1) e(21);
	if (getsid(b) != b || getpgid(b) != b) e(22);
	if (setpgid(b, b) != -1 || errno != EPERM) e(23);
	close(go[1]);
	close(rd[0]);
	child_ok(b, 30);

	/* A group leader cannot make a session; nor can a process whose pid
	 * still names a group; once the group is gone, it can.
	 */
	if ((c = fork()) == 0) {
		pid_t d;

		if (setpgid(0, 0) != 0) _exit(1);
		if (setsid() != -1 || errno != EPERM) _exit(2);
		if ((d = fork()) == 0) {
			pause();
			_exit(0);
		}
		if (setpgid(0, tpgrp) != 0) _exit(3);
		if (getpgid(d) != getpid()) _exit(4);
		if (setsid() != -1 || errno != EPERM) _exit(5);
		/* The group is in the session: back into it and out. */
		if (setpgid(0, getpid()) != 0) _exit(6);
		if (setpgid(0, tpgrp) != 0) _exit(7);
		kill(d, SIGKILL);
		if (waitpid(d, &st, 0) != d) _exit(8);
		if (setsid() != getpid()) _exit(9);
		_exit(0);
	}
	child_ok(c, 40);

	/* kill() and waitpid() of a process group. */
	if (pipe(rd) != 0) e(50);
	if ((g1 = fork()) == 0) {
		if (setpgid(0, 0) != 0) _exit(1);
		(void)write(rd[1], "1", 1);
		pause();
		_exit(0);
	}
	if (!readable(rd[0], WAIT_MS) || read(rd[0], &ch, 1) != 1) e(51);
	if ((g2 = fork()) == 0) {
		if (setpgid(0, g1) != 0) _exit(1);
		(void)write(rd[1], "2", 1);
		pause();
		_exit(0);
	}
	if (!readable(rd[0], WAIT_MS) || read(rd[0], &ch, 1) != 1) e(52);
	close(rd[0]);
	close(rd[1]);
	if (getpgid(g2) != g1) e(53);
	if (waitpid(-g1, &st, WNOHANG) != 0) e(54);
	if (kill(-g1, SIGTERM) != 0) e(55);
	a = waitfor(-g1, &st, 0);
	if ((a != g1 && a != g2) || !WIFSIGNALED(st) || WTERMSIG(st) != SIGTERM)
		e(56);
	b = waitfor(-g1, &st, 0);
	if ((b != g1 && b != g2) || b == a || !WIFSIGNALED(st)) e(57);
	if (waitpid(-g1, &st, WNOHANG) != -1 || errno != ECHILD) e(58);
	/* PM lets the processes go once VFS is done with them. */
	for (i = 0; i < WAIT_MS / 10 && kill(-g1, 0) == 0; i++)
		usleep(10000);
	if (kill(-g1, 0) != -1 || errno != ESRCH) e(59);
}

/* Type on the terminal: write to the master side. */
static void
type(const char *s)
{
	if (write(mfd, s, strlen(s)) != (ssize_t)strlen(s))
		ce(199);
}

/* A child of the session, in a process group of its own, in the background
 * unless the session puts it into the foreground.  The session moves it too,
 * as a shell does, so that whichever runs first, the group is there.
 */
static pid_t
job(void)
{
	pid_t pid;

	if ((pid = fork()) == 0) {
		/* As a shell's job: the signals the shell keeps for itself
		 * are back to their defaults.
		 */
		signal(SIGTTOU, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		if (setpgid(0, 0) != 0)
			_exit(98);
		return 0;
	}
	if (pid == -1)
		ce(198);
	else
		(void)setpgid(pid, pid);
	return pid;
}

/* The session: its leader plays the shell on the pty. */
static void
session(void)
{
	struct termios tio;
	pid_t me, j, m;
	char buf[16], ch;
	ssize_t n;
	int fd, st, done[2];

	close(tfd);
	if (setsid() == -1) {
		ce(100);
		return;
	}
	me = getpid();
	if ((fd = open(slave, O_RDWR)) == -1) {
		ce(101);
		return;
	}
	if (tcgetsid(fd) != me) ce(102);
	if (tcgetpgrp(fd) != me) ce(103);

	/* No echo: nobody reads what the master side gets. */
	if (tcgetattr(fd, &tio) != 0) ce(104);
	tio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | TOSTOP);
	if (tcsetattr(fd, TCSANOW, &tio) != 0) ce(105);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGINT, on_int);

	/* The test checks that the terminal is not its own. */
	ce(0);
	if (read(ack[0], &ch, 1) != 1) ce(106);

	/* tcsetpgrp(): a group of this session only. */
	if (tcsetpgrp(fd, -1) != -1 || errno != EINVAL) ce(110);
	if (tcsetpgrp(fd, tpgrp) != -1 || errno != EPERM) ce(111);
	if (tcgetpgrp(fd) != me) ce(112);

	/* A background read stops with SIGTTIN; in the foreground, it reads. */
	if ((j = job()) == 0) {
		n = read(fd, buf, sizeof(buf));
		_exit(n == 6 && memcmp(buf, "hello\n", 6) == 0 ? 0 : 1);
	}
	if (!waitstop(j, SIGTTIN)) ce(120);
	if (tcsetpgrp(fd, j) != 0) ce(121);
	if (tcgetpgrp(fd) != j) ce(122);
	if (kill(-j, SIGCONT) != 0) ce(123);
	type("hello\n");
	if (!waitexit(j, 0)) ce(124);
	/* From the background, with SIGTTOU ignored, like a shell. */
	if (tcsetpgrp(fd, me) != 0) ce(125);
	if (tcgetpgrp(fd) != me) ce(126);

	/* SUSP stops the foreground job inside its read.  The leader takes the
	 * terminal back, and reads: the job's read has stepped aside.
	 * Continued in the background, the job's read stops it again; in the
	 * foreground, it reads.
	 */
	if ((j = job()) == 0) {
		n = read(fd, buf, sizeof(buf));
		_exit(n == 6 && memcmp(buf, "world\n", 6) == 0 ? 0 : 1);
	}
	if (tcsetpgrp(fd, j) != 0) ce(130);
	usleep(300000);			/* let it get into its read */
	type("\032");			/* ^Z */
	if (!waitstop(j, SIGTSTP)) ce(131);
	if (tcsetpgrp(fd, me) != 0) ce(132);
	type("line\n");
	alarm(WAIT_MS / 1000 + 1);
	n = read(fd, buf, sizeof(buf));
	alarm(0);
	if (n != 5 || memcmp(buf, "line\n", 5) != 0) ce(133);
	if (kill(-j, SIGCONT) != 0) ce(134);		/* bg */
	if (!waitstop(j, SIGTTIN)) ce(135);
	if (tcsetpgrp(fd, j) != 0) ce(136);		/* fg */
	if (kill(-j, SIGCONT) != 0) ce(137);
	type("world\n");
	if (!waitexit(j, 0)) ce(138);
	if (tcsetpgrp(fd, me) != 0) ce(139);

	/* INTR ends the foreground job, and not the background or the
	 * leader.
	 */
	if ((j = job()) == 0) {
		pause();
		_exit(1);
	}
	if ((m = job()) == 0) {
		pause();
		_exit(1);
	}
	if (tcsetpgrp(fd, j) != 0) ce(140);
	usleep(100000);
	got_int = 0;
	type("\003");			/* ^C */
	if (!waitsig(j, SIGINT)) ce(141);
	usleep(QUIET_MS * 1000);
	if (waitpid(m, &st, WNOHANG) != 0) ce(142);
	if (got_int) ce(143);
	reap(m);
	if (tcsetpgrp(fd, me) != 0) ce(144);

	/* TOSTOP: a background write stops with SIGTTOU - unless SIGTTOU is
	 * ignored or blocked, when it goes ahead.  Without TOSTOP it goes
	 * ahead anyway; a background tcsetattr() stops either way.
	 */
	tio.c_lflag |= TOSTOP;
	if (tcsetattr(fd, TCSANOW, &tio) != 0) ce(150);
	if ((j = job()) == 0) {
		(void)write(fd, "x", 1);
		_exit(0);
	}
	if (!waitstop(j, SIGTTOU)) ce(151);
	reap(j);
	if ((j = job()) == 0) {
		signal(SIGTTOU, SIG_IGN);
		_exit(write(fd, "y", 1) == 1 ? 0 : 1);
	}
	if (!waitexit(j, 0)) ce(152);
	if ((j = job()) == 0) {
		block(SIGTTOU);
		_exit(write(fd, "y", 1) == 1 ? 0 : 1);
	}
	if (!waitexit(j, 0)) ce(153);
	tio.c_lflag &= ~TOSTOP;
	if (tcsetattr(fd, TCSANOW, &tio) != 0) ce(154);
	if ((j = job()) == 0)
		_exit(write(fd, "z", 1) == 1 ? 0 : 1);
	if (!waitexit(j, 0)) ce(155);
	if ((j = job()) == 0) {
		(void)tcsetattr(fd, TCSANOW, &tio);
		_exit(0);
	}
	if (!waitstop(j, SIGTTOU)) ce(156);
	reap(j);

	/* A background tcsetpgrp() is a write too; blocked SIGTTOU lets it. */
	if ((j = job()) == 0) {
		(void)tcsetpgrp(fd, getpgrp());
		_exit(0);
	}
	if (!waitstop(j, SIGTTOU)) ce(160);
	reap(j);
	if ((j = job()) == 0) {
		block(SIGTTOU);
		_exit(tcsetpgrp(fd, getpgrp()) == 0 ? 0 : 1);
	}
	if (!waitexit(j, 0)) ce(161);
	if (tcgetpgrp(fd) != j) ce(162);
	if (tcsetpgrp(fd, me) != 0) ce(163);

	/* A background read with SIGTTIN ignored or blocked fails with EIO,
	 * and so does one in an orphaned group: nobody would continue it.
	 */
	if ((j = job()) == 0) {
		signal(SIGTTIN, SIG_IGN);
		n = read(fd, buf, 1);
		_exit(n == -1 && errno == EIO ? 0 : 1);
	}
	if (!waitexit(j, 0)) ce(170);
	if ((j = job()) == 0) {
		block(SIGTTIN);
		n = read(fd, buf, 1);
		_exit(n == -1 && errno == EIO ? 0 : 1);
	}
	if (!waitexit(j, 0)) ce(171);
	if (pipe(done) != 0) ce(172);
	if ((m = fork()) == 0) {
		if ((j = job()) == 0) {
			int i;

			/* The middle process exits: INIT is the parent, of
			 * another session, and the group is orphaned.
			 */
			for (i = 0; i < WAIT_MS / 10 && getppid() != 1; i++)
				usleep(10000);
			if (getppid() != 1) ce(173);
			n = read(fd, buf, 1);
			if (n != -1 || errno != EIO) ce(174);
			(void)write(done[1], "d", 1);
			_exit(0);
		}
		_exit(0);
	}
	close(done[1]);
	if (!waitexit(m, 0)) ce(175);
	if (!readable(done[0], WAIT_MS) || read(done[0], &ch, 1) != 1) ce(176);
	close(done[0]);
	close(fd);
}

/* The test's own checks while the session runs: the session's terminal is
 * not the test's controlling terminal.
 */
static void
not_ours(void)
{
	if (tcgetpgrp(tfd) != -1 || errno != ENOTTY) e(10);
	if (tcsetpgrp(tfd, tpgrp) != -1 || errno != ENOTTY) e(11);
	if (tcgetsid(tfd) != -1 || errno != ENOTTY) e(12);
	if (write(ack[1], "a", 1) != 1) e(13);
}

/* Count the errors the children report, until they are all gone. */
static void
collect(void)
{
	int n;

	for (;;) {
		if (!readable(rep[0], 30000)) {
			e(990);
			break;
		}
		if (read(rep[0], &n, sizeof(n)) != sizeof(n))
			break;
		if (n == 0)
			not_ours();
		else
			e(n);
	}
}

/* A session with a pty for its terminal: see session(). */
static void
test96b(void)
{
	pid_t s;
	int st;

	subtest = 2;

	if (openpty(&mfd, &tfd, slave, NULL, NULL) != 0) {
		e(1);
		return;
	}
	if (pipe(rep) != 0 || pipe(ack) != 0) e(2);
	if ((s = fork()) == 0) {
		close(rep[0]);
		close(ack[1]);
		session();
		_exit(0);
	}
	close(rep[1]);
	close(ack[0]);
	collect();
	if (waitfor(s, &st, 0) != s) {
		e(3);
		reap(s);
	}
	close(rep[0]);
	close(ack[1]);
	close(mfd);
	close(tfd);
}

static void
on_hup(int sig)
{
	(void)write(rep[1], "h", 1);
}

/* Hangups.  The leader of a session with a controlling terminal exits: its
 * terminal's foreground group gets SIGHUP, a background group does not.  The
 * master side of the pty is closed: the session leader gets SIGHUP.
 */
static void
test96c(void)
{
	pid_t s, fg, bg;
	char ch, c2;
	int fd, st;

	subtest = 3;

	if (openpty(&mfd, &tfd, slave, NULL, NULL) != 0) {
		e(1);
		return;
	}
	if (pipe(rep) != 0) e(2);
	if ((s = fork()) == 0) {
		close(rep[0]);
		close(tfd);
		if (setsid() == -1 || (fd = open(slave, O_RDWR)) == -1)
			_exit(1);
		signal(SIGTTOU, SIG_IGN);
		/* Each reports its pid, and then 'h' for a SIGHUP. */
		if ((fg = fork()) == 0) {
			signal(SIGHUP, on_hup);
			for (;;)
				pause();
		}
		(void)setpgid(fg, fg);
		if ((bg = fork()) == 0) {
			signal(SIGHUP, on_hup);
			for (;;)
				pause();
		}
		(void)setpgid(bg, bg);
		if (tcsetpgrp(fd, fg) != 0)
			_exit(2);
		if (write(rep[1], &fg, sizeof(fg)) != sizeof(fg) ||
		    write(rep[1], &bg, sizeof(bg)) != sizeof(bg))
			_exit(3);
		_exit(0);
	}
	close(rep[1]);
	if (!readable(rep[0], WAIT_MS) || read(rep[0], &fg, sizeof(fg)) !=
	    sizeof(fg) || read(rep[0], &bg, sizeof(bg)) != sizeof(bg)) {
		e(3);
		fg = bg = -1;
	}
	if (!waitexit(s, 0)) e(4);
	if (!readable(rep[0], WAIT_MS) || read(rep[0], &ch, 1) != 1 ||
	    ch != 'h') e(5);
	/* Only one hangup: the background group's process did not get one. */
	if (readable(rep[0], QUIET_MS)) e(6);
	if (fg > 0) kill(fg, SIGKILL);
	if (bg > 0) kill(bg, SIGKILL);
	close(rep[0]);
	close(mfd);
	close(tfd);

	/* The master side goes: the session leader gets SIGHUP. */
	if (openpty(&mfd, &tfd, slave, NULL, NULL) != 0) {
		e(10);
		return;
	}
	if (pipe(rep) != 0 || pipe(ack) != 0) e(11);
	if ((s = fork()) == 0) {
		close(rep[0]);
		close(ack[0]);
		close(mfd);
		close(tfd);
		if (setsid() == -1 || (fd = open(slave, O_RDWR)) == -1)
			_exit(1);
		signal(SIGHUP, on_hup);
		(void)write(ack[1], "r", 1);
		alarm(WAIT_MS / 1000 + 1);
		pause();
		_exit(0);
	}
	close(rep[1]);
	close(ack[1]);
	if (!readable(ack[0], WAIT_MS) || read(ack[0], &c2, 1) != 1) e(12);
	close(mfd);
	close(tfd);
	if (!readable(rep[0], WAIT_MS) || read(rep[0], &ch, 1) != 1 ||
	    ch != 'h') e(13);
	if (!waitexit(s, 0)) e(14);
	close(rep[0]);
	close(ack[0]);
}

int
main(int argc, char **argv)
{
	static const int sigs[] = { SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT, SIGCHLD,
	    SIGHUP, SIGINT, SIGTERM };
	struct sigaction sa;
	sigset_t set;
	int i, m = 0x7;

	/* Whatever the shell left ignored or blocked, the children start from
	 * the defaults.
	 */
	for (i = 0; i < (int)(sizeof(sigs) / sizeof(sigs[0])); i++)
		signal(sigs[i], SIG_DFL);
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alrm;		/* no SA_RESTART: EINTR */
	sigaction(SIGALRM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);

	start(96);

	if (argc == 2)
		m = atoi(argv[1]);
	tpgrp = getpgrp();

	for (i = 0; i < ITERATIONS; i++) {
		if (m & 0x01) test96a();
		if (m & 0x02) test96b();
		if (m & 0x04) test96c();
	}

	quit();
	return 0;
}
