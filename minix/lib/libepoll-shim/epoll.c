/*
 * libepoll-shim: epoll(7) over poll(2).
 *
 * An epoll instance is a list of descriptors with the events wanted and
 * the caller's data for each; epoll_wait() polls them all and reports what
 * is ready.  That costs a look at every descriptor on every wait, where
 * Linux pays only for what became ready - the price measured in
 * port/test/evloop (pollbench) before this was chosen over kqueue in VFS;
 * see PORTING-LOG.md, "Этап 11.3".
 *
 * Kept: level-triggered events, EPOLLONESHOT, epoll_pwait()'s mask, a
 * descriptor closed while watched dropping out by itself (poll(2) says
 * POLLNVAL for it - libc's poll on MINIX learned to, see poll.c there),
 * EPOLLHUP and EPOLLRDHUP for a stream socket whose peer has gone (see
 * sock_eof()).  A pipe whose writer has gone shows as readable only.
 * Not kept: EPOLLET (taken as level-triggered - a
 * program that relies on edges sees the same event again until it reads),
 * EPOLLEXCLUSIVE and EPOLLWAKEUP (ignored), and polling an epoll
 * descriptor from outside (it never becomes ready).  A descriptor closed
 * and reused before the next wait is watched as the new file; Linux would
 * have dropped it at the close.  libwayland removes before it closes.
 */
#include <sys/socket.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

int epoll_create(int size)
{
	if (size <= 0) {
		errno = EINVAL;
		return -1;
	}
	return epoll_create1(0);
}

int epoll_create1(int flags)
{
	if (flags & ~EPOLL_CLOEXEC) {
		errno = EINVAL;
		return -1;
	}
	return shim_new(SK_EPOLL, flags & O_CLOEXEC);
}

static struct shim_fd *ep_get(int epfd)
{
	struct shim_fd *e;

	if ((e = shim_get(epfd, SK_EPOLL)) == NULL)
		errno = fcntl(epfd, F_GETFD) < 0 ? EBADF : EINVAL;
	return e;
}

static struct shim_item *ep_find(struct shim_fd *e, int fd)
{
	int i;

	for (i = 0; i < e->u.ep.n; i++)
		if (e->u.ep.items[i].fd == fd)
			return &e->u.ep.items[i];
	return NULL;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *ev)
{
	struct shim_fd *e;
	struct shim_item *it, *n;

	if ((e = ep_get(epfd)) == NULL)
		return -1;
	if (fcntl(fd, F_GETFD) < 0) {
		errno = EBADF;
		return -1;
	}
	if (fd == epfd || (op != EPOLL_CTL_DEL && ev == NULL)) {
		errno = EINVAL;
		return -1;
	}
	it = ep_find(e, fd);

	switch (op) {
	case EPOLL_CTL_ADD:
		if (it != NULL) {
			errno = EEXIST;
			return -1;
		}
		if (e->u.ep.n == e->u.ep.max) {
			int max = e->u.ep.max ? e->u.ep.max * 2 : 16;

			if ((n = realloc(e->u.ep.items, max * sizeof(*n))) ==
			    NULL)
				return -1;
			e->u.ep.items = n;
			e->u.ep.max = max;
		}
		it = &e->u.ep.items[e->u.ep.n++];
		it->fd = fd;
		/* FALLTHROUGH */
	case EPOLL_CTL_MOD:
		if (it == NULL) {
			errno = ENOENT;
			return -1;
		}
		it->events = ev->events;
		it->data = ev->data;
		it->disabled = 0;
		return 0;
	case EPOLL_CTL_DEL:
		if (it == NULL) {
			errno = ENOENT;
			return -1;
		}
		*it = e->u.ep.items[--e->u.ep.n];
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

static short to_poll(uint32_t ev)
{
	short p = 0;

	if (ev & (EPOLLIN | EPOLLRDNORM))
		p |= POLLIN;
	if (ev & EPOLLRDBAND)
		p |= POLLRDBAND;
	if (ev & EPOLLPRI)
		p |= POLLPRI;
	if (ev & (EPOLLOUT | EPOLLWRNORM))
		p |= POLLOUT;
	if (ev & EPOLLWRBAND)
		p |= POLLWRBAND;
	return p;
}

/* What poll(2) said, as epoll would say it: only what was asked, plus
 * errors and hangups, which epoll always reports. */
static uint32_t to_epoll(short p, uint32_t want)
{
	uint32_t ev = 0;

	if (p & POLLIN)
		ev |= want & (EPOLLIN | EPOLLRDNORM);
	if (p & POLLRDBAND)
		ev |= want & EPOLLRDBAND;
	if (p & POLLPRI)
		ev |= want & EPOLLPRI;
	if (p & POLLOUT)
		ev |= want & (EPOLLOUT | EPOLLWRNORM);
	if (p & POLLWRBAND)
		ev |= want & EPOLLWRBAND;
	if (p & POLLERR)
		ev |= EPOLLERR;
	if (p & POLLHUP)
		ev |= EPOLLHUP | (want & EPOLLRDHUP);
	return ev;
}

/*
 * Has the other end of this socket gone, with nothing left to read?
 *
 * Linux says EPOLLHUP then; poll(2) here is built on select(2), which can
 * say only "readable", and that is all a reader of a socket whose peer
 * closed learns from it.  Most code reads and sees the end (libwayland
 * does), but code that waits for the hangup alone - events 0, or only
 * EPOLLRDHUP - would wait for ever.  So a readable stream socket is
 * peeked at: a zero-byte peek is the end.  Pipes cannot be peeked and
 * stay "readable" at their end.
 */
static int sock_eof(int fd)
{
	struct stat st;
	socklen_t len;
	int type;
	char c;

	if (fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode))
		return 0;
	if (recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT) != 0)
		return 0;
	len = sizeof(type);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 ||
	    type == SOCK_DGRAM)
		return 0;		/* an empty datagram is not an end */
	return 1;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
	int timeout, const sigset_t *mask)
{
	static struct pollfd *pfd;
	static int *which, pmax;
	struct shim_fd *e;
	struct shim_item *it;
	int i, n, r, k;

	if (maxevents <= 0 || events == NULL) {
		errno = EINVAL;
		return -1;
	}

	for (;;) {
		if ((e = ep_get(epfd)) == NULL)
			return -1;

		if (e->u.ep.n > pmax) {
			struct pollfd *np;
			int *nw;

			if ((np = realloc(pfd, e->u.ep.n * sizeof(*np))) == NULL)
				return -1;
			pfd = np;
			if ((nw = realloc(which, e->u.ep.n * sizeof(*nw))) == NULL)
				return -1;
			which = nw;
			pmax = e->u.ep.n;
		}
		for (i = n = 0; i < e->u.ep.n; i++) {
			it = &e->u.ep.items[i];
			if (it->disabled)
				continue;
			pfd[n].fd = it->fd;
			pfd[n].events = to_poll(it->events);
			pfd[n].revents = 0;
			which[n++] = it->fd;
		}

		if ((r = shim_wait(pfd, (nfds_t)n, timeout, mask)) <= 0)
			return r;

		/* The list may have changed under a handler; go by fd. */
		e = &shim_tab[epfd];
		for (i = k = 0; i < n && k < maxevents; i++) {
			if (pfd[i].revents == 0 ||
			    (it = ep_find(e, which[i])) == NULL)
				continue;
			if (pfd[i].revents & POLLNVAL) {
				*it = e->u.ep.items[--e->u.ep.n];
				continue;
			}
			if ((pfd[i].revents & POLLIN) &&
			    !(pfd[i].revents & POLLHUP) && sock_eof(which[i]))
				pfd[i].revents |= POLLHUP;
			events[k].events = to_epoll(pfd[i].revents,
			    it->events);
			if (events[k].events == 0)
				continue;
			events[k].data = it->data;
			if (it->events & EPOLLONESHOT)
				it->disabled = 1;
			k++;
		}
		if (k > 0 || timeout == 0)
			return k;
		/* Only closed descriptors were "ready": wait again. */
	}
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents,
	int timeout)
{
	return epoll_pwait(epfd, events, maxevents, timeout, NULL);
}
