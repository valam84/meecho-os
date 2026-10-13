/*
 * The calls that libepoll-shim has to see on its own descriptors.
 *
 * An eventfd, a timerfd or a signalfd here is the reading end of a pipe,
 * and the meaning - a counter, a count of expirations, signal records - is
 * kept by the library.  So read(2), write(2) and close(2) on them must go
 * through the library, and so must poll(2), which is where a timer expires
 * and where blocked signals are let in.  As in epoll-shim, the code that
 * includes one of the shim's headers has these calls redirected by
 * function-like macros; on any other descriptor the library passes the
 * call straight on.  Code that does not include them is untouched.
 *
 * The macros come after the system's own declarations, which they would
 * otherwise rename: <unistd.h> and <poll.h> are included first.  A later
 * redeclaration of read() turns into a redeclaration of epoll_shim_read()
 * with the same type, which is harmless.
 */
#ifndef _EPOLL_SHIM_DETAIL_INTERPOSE_H_
#define _EPOLL_SHIM_DETAIL_INTERPOSE_H_

#include <sys/types.h>
#include <poll.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t epoll_shim_read(int, void *, size_t);
ssize_t epoll_shim_write(int, const void *, size_t);
int epoll_shim_close(int);
int epoll_shim_poll(struct pollfd *, nfds_t, int);

#ifdef __cplusplus
}
#endif

#ifndef EPOLL_SHIM_NO_INTERPOSE
#define read(fd, buf, n)	epoll_shim_read(fd, buf, n)
#define write(fd, buf, n)	epoll_shim_write(fd, buf, n)
#define close(fd)		epoll_shim_close(fd)
#define poll(fds, n, timeout)	epoll_shim_poll(fds, n, timeout)
#endif

#endif /* _EPOLL_SHIM_DETAIL_INTERPOSE_H_ */
