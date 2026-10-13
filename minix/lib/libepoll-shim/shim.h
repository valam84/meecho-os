/*
 * libepoll-shim internals: the table of the library's own descriptors.
 *
 * Every descriptor the library makes is the reading end of a pipe, kept
 * in a table indexed by its number together with the pipe's writing end
 * and the meaning the library gives it.  The pipe is what makes the
 * descriptor ready to poll(2): the library puts a byte in when there is
 * something to read (an eventfd's counter is not zero, a timer expired)
 * or a record (a signal came).  An epoll descriptor's pipe is never
 * written: polling an epoll instance from outside is not supported.
 *
 * A number is checked before it is believed: the pipe's (st_dev, st_ino)
 * are recorded when it is made, and a descriptor closed behind the
 * library's back - by code that does not include its headers - and
 * reused for something else is told apart by them and forgotten.
 */
#ifndef _EPOLL_SHIM_SHIM_H_
#define _EPOLL_SHIM_SHIM_H_

#define EPOLL_SHIM_NO_INTERPOSE		/* the real calls, in here */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>

#include "sys/epoll.h"

#define SHIM_MAX_FD	256

enum shim_kind {
	SK_NONE = 0,
	SK_EPOLL,
	SK_EVENTFD,
	SK_TIMERFD,
	SK_SIGNALFD
};

struct shim_item {			/* one descriptor an epoll watches */
	int fd;
	uint32_t events;
	epoll_data_t data;
	int disabled;			/* EPOLLONESHOT fired */
};

struct shim_fd {
	enum shim_kind kind;
	dev_t dev;
	ino_t ino;
	int wr;				/* the pipe's writing end */
	union {
		struct {
			struct shim_item *items;
			int n, max;
		} ep;
		struct {
			uint64_t count;
			int semaphore;
		} ev;
		struct {
			int armed;
			clockid_t clock;
			struct timespec deadline;	/* CLOCK_MONOTONIC */
			struct timespec interval;
			uint64_t expirations;
		} tm;
		struct {
			sigset_t mask;
		} sg;
	} u;
};

extern struct shim_fd shim_tab[SHIM_MAX_FD];
extern volatile sig_atomic_t shim_signalled;

/* fdtab.c */
int shim_new(enum shim_kind kind, int flags);
struct shim_fd *shim_get(int fd, enum shim_kind kind);
void shim_forget(int fd);
void shim_mark(struct shim_fd *e);
void shim_drain(int fd);
int shim_wait(struct pollfd *fds, nfds_t n, int timeout,
	const sigset_t *mask);

/* timerfd.c */
void shim_now(struct timespec *ts);
int shim_timers_run(void);
int shim_timers_next_ms(void);
ssize_t shim_timerfd_read(int fd, struct shim_fd *e, void *buf, size_t n);

/* eventfd.c */
ssize_t shim_eventfd_read(int fd, struct shim_fd *e, void *buf, size_t n);
ssize_t shim_eventfd_write(struct shim_fd *e, const void *buf, size_t n);

/* signalfd.c */
void shim_signals_mask(sigset_t *set);
ssize_t shim_signalfd_read(int fd, struct shim_fd *e, void *buf, size_t n);
void shim_signalfd_forget(struct shim_fd *e);

#endif /* _EPOLL_SHIM_SHIM_H_ */
