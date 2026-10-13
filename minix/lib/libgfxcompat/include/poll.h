/*
 * <poll.h> for the Wayland stack (libgfxcompat): the system's, with poll()
 * redirected to gfx_poll(), which lets libmthread's other threads run while
 * one waits (poll.c says how).  Before a program starts its first thread
 * gfx_poll() is poll(2) itself.
 *
 * The redirect is a function-like macro, as libepoll-shim's are, and gives
 * way to theirs: whichever of the two headers comes first, poll() ends up
 * as epoll_shim_poll() where libepoll-shim's are included - which waits in
 * gfx_poll() in its turn.  GFXCOMPAT_NO_POLL keeps the plain call.
 */
#ifndef _GFXCOMPAT_POLL_H_
#define _GFXCOMPAT_POLL_H_

#include_next <poll.h>

#ifdef __cplusplus
extern "C" {
#endif
int gfx_poll(struct pollfd *, nfds_t, int);
#ifdef __cplusplus
}
#endif

#if !defined(poll) && !defined(GFXCOMPAT_NO_POLL)
#define poll(fds, n, timeout)	gfx_poll(fds, n, timeout)
#endif

#endif /* _GFXCOMPAT_POLL_H_ */
