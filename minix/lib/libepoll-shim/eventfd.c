/*
 * libepoll-shim: eventfd(2).
 *
 * A counter, kept here; the descriptor is a pipe that holds one byte
 * exactly when the counter is not zero, which is what makes it readable
 * to poll(2) and to epoll.  write(2) adds to the counter, read(2) takes
 * all of it (or one, with EFD_SEMAPHORE).  The counter is the process's
 * own: a child after fork(2) has a copy, not the same counter, and
 * another process writing the pipe directly means nothing.  libwayland
 * uses one inside one process (wl_display_terminate()).
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "shim.h"
#include "sys/eventfd.h"

int eventfd(unsigned int initval, int flags)
{
	struct shim_fd *e;
	int fd;

	if (flags & ~(EFD_SEMAPHORE | EFD_CLOEXEC | EFD_NONBLOCK)) {
		errno = EINVAL;
		return -1;
	}
	if ((fd = shim_new(SK_EVENTFD, flags)) < 0)
		return -1;
	e = &shim_tab[fd];
	e->u.ev.count = initval;
	e->u.ev.semaphore = (flags & EFD_SEMAPHORE) != 0;
	if (initval != 0)
		shim_mark(e);
	return fd;
}

ssize_t shim_eventfd_read(int fd, struct shim_fd *e, void *buf, size_t n)
{
	struct pollfd p;
	uint64_t v;

	if (n < sizeof(v)) {
		errno = EINVAL;
		return -1;
	}
	if (e->u.ev.count == 0) {
		if (fcntl(fd, F_GETFL) & O_NONBLOCK) {
			errno = EAGAIN;
			return -1;
		}
		/* Blocking: nobody else in this process can add to it. */
		p.fd = fd;
		p.events = POLLIN;
		if (shim_wait(&p, 1, -1, NULL) < 0)
			return -1;
		if (e->u.ev.count == 0) {
			shim_drain(fd);		/* not ours */
			errno = EAGAIN;
			return -1;
		}
	}

	v = e->u.ev.semaphore ? 1 : e->u.ev.count;
	e->u.ev.count -= v;
	if (e->u.ev.count == 0)
		shim_drain(fd);
	memcpy(buf, &v, sizeof(v));
	return sizeof(v);
}

ssize_t shim_eventfd_write(struct shim_fd *e, const void *buf, size_t n)
{
	uint64_t v, was;

	if (n < sizeof(v)) {
		errno = EINVAL;
		return -1;
	}
	memcpy(&v, buf, sizeof(v));
	if (v == UINT64_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (UINT64_MAX - 1 - e->u.ev.count < v) {
		errno = EAGAIN;		/* it would overflow; Linux waits */
		return -1;
	}
	was = e->u.ev.count;
	e->u.ev.count += v;
	if (was == 0 && e->u.ev.count != 0)
		shim_mark(e);
	return sizeof(v);
}

int eventfd_read(int fd, eventfd_t *value)
{
	return epoll_shim_read(fd, value, sizeof(*value)) == sizeof(*value) ?
	    0 : -1;
}

int eventfd_write(int fd, eventfd_t value)
{
	return epoll_shim_write(fd, &value, sizeof(value)) == sizeof(value) ?
	    0 : -1;
}
