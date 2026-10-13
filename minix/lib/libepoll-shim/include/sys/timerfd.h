/* <sys/timerfd.h> of libepoll-shim; see <sys/epoll.h> and timerfd.c. */
#ifndef _LIBEPOLL_SHIM_SYS_TIMERFD_H_
#define _LIBEPOLL_SHIM_SYS_TIMERFD_H_

#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#include <epoll-shim/detail/interpose.h>

#define TFD_CLOEXEC		O_CLOEXEC
#define TFD_NONBLOCK		O_NONBLOCK
#define TFD_TIMER_ABSTIME	1
#define TFD_TIMER_CANCEL_ON_SET	2

#ifdef __cplusplus
extern "C" {
#endif

int timerfd_create(int, int);
int timerfd_settime(int, int, const struct itimerspec *, struct itimerspec *);
int timerfd_gettime(int, struct itimerspec *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBEPOLL_SHIM_SYS_TIMERFD_H_ */
