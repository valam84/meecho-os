/*	$NetBSD: poll.c,v 1.3 2008/04/29 05:46:08 martin Exp $	*/

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles Blundell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

/*
 * MEECHO: ppoll(2) нужен OpenSSH, а в libc его не было. Реализуется тем же
 * способом, что и poll(2) выше - через select(2), потому что другого
 * механизма ожидания у системы нет.
 *
 * Маска сигналов накладывается НЕ атомарно: sigprocmask() и select() - два
 * вызова, и сигнал, пришедший между ними, будет обработан до того, как мы
 * заснём, то есть его обработчик выставит флаг, а мы всё равно уснём. Чтобы
 * это не превращалось в вечное ожидание, при заданной маске время ожидания
 * ограничивается PPOLL_MASK_CAP_MS: вызывающий получит 0 (таймаут) и
 * перечитает свои флаги. Настоящая атомарность требует передачи маски в
 * VFS вместе с SELECT и согласования с PM - это работа не в libc.
 */
#define PPOLL_MASK_CAP_MS	1000

static int
poll_common(struct pollfd *p, nfds_t nfds, int timout)
{
	fd_set rd, wr, except;
	struct timeval tv;
	nfds_t i;
	int highfd, rval;

	/*
	 * select cannot tell us much wrt POLL*BAND, POLLPRI, POLLHUP or
	 * POLLNVAL.
	 */
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_ZERO(&except);

	highfd = -1;
	for (i = 0; i < nfds; i++) {
		p[i].revents = 0;
		if (p[i].fd < 0)
			continue;
		if (p[i].fd >= FD_SETSIZE) {
			errno = EINVAL;
			return -1;
		}
		if (p[i].fd > highfd)
			highfd = p[i].fd;

		if (p[i].events & (POLLIN|POLLRDNORM))
			FD_SET(p[i].fd, &rd);
		if (p[i].events & (POLLOUT|POLLWRNORM|POLLWRBAND))
			FD_SET(p[i].fd, &wr);
		if (p[i].events & (POLLRDBAND|POLLPRI))
			FD_SET(p[i].fd, &except);
	}

	tv.tv_sec = timout / 1000;
	tv.tv_usec = (timout % 1000) * 1000;

	rval = select(highfd + 1, &rd, &wr, &except,
		timout == -1 ? NULL : &tv);
	if (rval <= 0)
		return rval;

	rval = 0;
	for (i = 0; i < nfds; i++) {
		if (p[i].fd < 0)
			continue;
		if (FD_ISSET(p[i].fd, &rd))
			p[i].revents |= p[i].events & (POLLIN|POLLRDNORM);
		if (FD_ISSET(p[i].fd, &wr))
			p[i].revents |=
			    p[i].events & (POLLOUT|POLLWRNORM|POLLWRBAND);
		if (FD_ISSET(p[i].fd, &except))
			p[i].revents |= p[i].events & (POLLRDBAND|POLLPRI);
		/* XXX: POLLERR/POLLHUP/POLLNVAL? */
		if (p[i].revents != 0)
			rval++;
	}
	return rval;
}

int
poll(struct pollfd *p, nfds_t nfds, int timout)
{
	return poll_common(p, nfds, timout);
}

int
ppoll(struct pollfd * __restrict p, nfds_t nfds,
	const struct timespec * __restrict ts,
	const sigset_t * __restrict sigmask)
{
	sigset_t omask;
	int timout, rval, saved_errno;

	if (ts == NULL)
		timout = -1;
	else {
		if (ts->tv_sec < 0 || ts->tv_nsec < 0 ||
		    ts->tv_nsec >= 1000000000L) {
			errno = EINVAL;
			return -1;
		}
		if (ts->tv_sec > (INT_MAX - 1) / 1000)
			timout = INT_MAX;
		else {
			timout = (int)(ts->tv_sec * 1000 +
			    (ts->tv_nsec + 999999L) / 1000000L);
		}
	}

	if (sigmask == NULL)
		return poll_common(p, nfds, timout);

	/* См. комментарий выше: маска не атомарна, поэтому сон ограничен. */
	if (timout < 0 || timout > PPOLL_MASK_CAP_MS)
		timout = PPOLL_MASK_CAP_MS;

	if (sigprocmask(SIG_SETMASK, sigmask, &omask) == -1)
		return -1;
	rval = poll_common(p, nfds, timout);
	saved_errno = errno;
	(void)sigprocmask(SIG_SETMASK, &omask, NULL);
	errno = saved_errno;

	return rval;
}
