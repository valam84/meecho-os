/*
 * <sys/epoll.h> of libepoll-shim: the Linux event interface, over poll(2).
 *
 * The layout and names are those of the epoll-shim project, which the BSDs
 * use to build Wayland software: headers under include/libepoll-shim, a
 * library called epoll-shim, a pkg-config file of the same name.  A port
 * that looks for dependency('epoll-shim') - libwayland does - finds this
 * one.  The code is MEECHO's own; see minix/lib/libepoll-shim/epoll.c for
 * how it works and what it does not do.
 */
#ifndef _LIBEPOLL_SHIM_SYS_EPOLL_H_
#define _LIBEPOLL_SHIM_SYS_EPOLL_H_

#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>

#include <epoll-shim/detail/interpose.h>

#define EPOLL_CLOEXEC	O_CLOEXEC

#define EPOLLIN		0x001
#define EPOLLPRI	0x002
#define EPOLLOUT	0x004
#define EPOLLERR	0x008
#define EPOLLHUP	0x010
#define EPOLLRDNORM	0x040
#define EPOLLRDBAND	0x080
#define EPOLLWRNORM	0x100
#define EPOLLWRBAND	0x200
#define EPOLLMSG	0x400
#define EPOLLRDHUP	0x2000
#define EPOLLEXCLUSIVE	(1U << 28)
#define EPOLLWAKEUP	(1U << 29)
#define EPOLLONESHOT	(1U << 30)
#define EPOLLET		(1U << 31)

#define EPOLL_CTL_ADD	1
#define EPOLL_CTL_DEL	2
#define EPOLL_CTL_MOD	3

typedef union epoll_data {
	void *ptr;
	int fd;
	uint32_t u32;
	uint64_t u64;
} epoll_data_t;

struct epoll_event {
	uint32_t events;
	epoll_data_t data;
};

#ifdef __cplusplus
extern "C" {
#endif

int epoll_create(int);
int epoll_create1(int);
int epoll_ctl(int, int, int, struct epoll_event *);
int epoll_wait(int, struct epoll_event *, int, int);
int epoll_pwait(int, struct epoll_event *, int, int, const sigset_t *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBEPOLL_SHIM_SYS_EPOLL_H_ */
