/*
 * libepoll-shim: signalfd(2).
 *
 * A signal handler that writes a record into the pipe of every signalfd
 * whose mask holds the signal - the self-pipe trick, behind the
 * descriptor Linux would give.  The program blocks the signals it reads
 * through a signalfd (that is how signalfd(2) is used: libwayland does it
 * in wl_event_loop_add_signal()), so a blocked signal would never reach
 * the handler; the library lets the signalfds' signals in for the time it
 * waits (fdtab.c) and while a signalfd is read, and only then.
 *
 * The record has the signal number and nothing else.  The previous action
 * of a signal is put back when no signalfd wants it any more.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "shim.h"
#include "sys/signalfd.h"

static struct sigaction old_action[NSIG];
static int installed[NSIG];

static void handler(int signo)
{
	struct signalfd_siginfo rec;
	struct shim_fd *e;
	int fd, saved = errno;

	memset(&rec, 0, sizeof(rec));
	rec.ssi_signo = (uint32_t)signo;
	for (fd = 0; fd < SHIM_MAX_FD; fd++) {
		e = &shim_tab[fd];
		if (e->kind == SK_SIGNALFD && sigismember(&e->u.sg.mask, signo))
			(void)write(e->wr, &rec, sizeof(rec));
	}
	shim_signalled = 1;
	errno = saved;
}

/* The union of every signalfd's mask. */
void shim_signals_mask(sigset_t *set)
{
	struct shim_fd *e;
	int fd, i;

	sigemptyset(set);
	for (fd = 0; fd < SHIM_MAX_FD; fd++) {
		e = &shim_tab[fd];
		if (e->kind != SK_SIGNALFD)
			continue;
		for (i = 1; i < NSIG; i++)
			if (sigismember(&e->u.sg.mask, i))
				sigaddset(set, i);
	}
}

/* Handlers in for what is wanted now, out for what no longer is. */
static void sync_handlers(void)
{
	struct sigaction sa;
	sigset_t want;
	int i;

	shim_signals_mask(&want);
	for (i = 1; i < NSIG; i++) {
		if (sigismember(&want, i) && !installed[i]) {
			memset(&sa, 0, sizeof(sa));
			sa.sa_handler = handler;
			sigfillset(&sa.sa_mask);
			if (sigaction(i, &sa, &old_action[i]) == 0)
				installed[i] = 1;
		} else if (!sigismember(&want, i) && installed[i]) {
			(void)sigaction(i, &old_action[i], NULL);
			installed[i] = 0;
		}
	}
}

int signalfd(int fd, const sigset_t *mask, int flags)
{
	struct shim_fd *e;

	if (mask == NULL || (flags & ~(SFD_CLOEXEC | SFD_NONBLOCK))) {
		errno = EINVAL;
		return -1;
	}
	if (fd == -1) {
		if ((fd = shim_new(SK_SIGNALFD, flags)) < 0)
			return -1;
		e = &shim_tab[fd];
	} else if ((e = shim_get(fd, SK_SIGNALFD)) == NULL) {
		errno = fcntl(fd, F_GETFD) < 0 ? EBADF : EINVAL;
		return -1;
	}
	e->u.sg.mask = *mask;
	sigdelset(&e->u.sg.mask, SIGKILL);
	sigdelset(&e->u.sg.mask, SIGSTOP);
	sync_handlers();
	return fd;
}

void shim_signalfd_forget(struct shim_fd *e)
{
	sigemptyset(&e->u.sg.mask);
	e->kind = SK_NONE;		/* out of the union first */
	sync_handlers();
	e->kind = SK_SIGNALFD;		/* shim_forget() finishes */
}

ssize_t shim_signalfd_read(int fd, struct shim_fd *e, void *buf, size_t n)
{
	size_t want = n - n % sizeof(struct signalfd_siginfo);
	sigset_t old;
	ssize_t r;
	int err;

	if (want == 0) {
		errno = EINVAL;
		return -1;
	}
	/* Pending signals come in now; a blocking read waits for more. */
	sigprocmask(SIG_UNBLOCK, &e->u.sg.mask, &old);
	for (;;) {
		shim_signalled = 0;
		r = read(fd, buf, want);
		/* Interrupted by one of ours: its record is there now. */
		if (r >= 0 || errno != EINTR || !shim_signalled)
			break;
	}
	err = errno;
	sigprocmask(SIG_SETMASK, &old, NULL);
	errno = err;
	return r;
}
