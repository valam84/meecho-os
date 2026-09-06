/*	asyn_write()					Author: Kees J. Bot
 *								7 Jul 1997
 */
#include "asyn.h"
#include <signal.h>

ssize_t asyn_write(asynchio_t *asyn, int fd, const void *buf, size_t len)
/* Nonblocking write().  (See asyn_read()). */
{
	asynfd_t *afd;

	asyn->asyn_more++;

	if ((unsigned) fd >= FD_SETSIZE) { errno= EBADF; return -1; }
	afd= &asyn->asyn_afd[fd];

	if (!afd->afd_seen) {
		if ((afd->afd_flags= fcntl(fd, F_GETFL)) < 0) return -1;
		afd->afd_seen= 1;
	}

	if (afd->afd_state[SEL_WRITE] == PENDING) {
		/*
		 * The set to install and the place to save the old one are
		 * restrict-qualified, so they may not be the same object.
		 */
		sigset_t mask, empty;
		ssize_t result;
		int err;

		sigemptyset(&empty);
		if (sigprocmask(SIG_SETMASK, &empty, &mask) < 0) return -1;
		(void) fcntl(fd, F_SETFL, afd->afd_flags | O_NONBLOCK);

		result= write(fd, buf, len);
		err= errno;

		(void) fcntl(fd, F_SETFL, afd->afd_flags);
		(void) sigprocmask(SIG_SETMASK, &mask, nil);

		errno= err;
		if (result != -1 || errno != EAGAIN) {
			afd->afd_state[SEL_WRITE]= IDLE;
			return result;
		}
	}

	afd->afd_state[SEL_WRITE]= WAITING;
	FD_SET(fd, &asyn->asyn_fdset[SEL_WRITE]);
	errno= EAGAIN;
	asyn->asyn_more--;
	return -1;
}
