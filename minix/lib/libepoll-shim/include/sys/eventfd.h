/* <sys/eventfd.h> of libepoll-shim; see <sys/epoll.h> and eventfd.c. */
#ifndef _LIBEPOLL_SHIM_SYS_EVENTFD_H_
#define _LIBEPOLL_SHIM_SYS_EVENTFD_H_

#include <sys/types.h>
#include <fcntl.h>
#include <stdint.h>

#include <epoll-shim/detail/interpose.h>

typedef uint64_t eventfd_t;

#define EFD_SEMAPHORE	1
#define EFD_CLOEXEC	O_CLOEXEC
#define EFD_NONBLOCK	O_NONBLOCK

#ifdef __cplusplus
extern "C" {
#endif

int eventfd(unsigned int, int);
int eventfd_read(int, eventfd_t *);
int eventfd_write(int, eventfd_t);

#ifdef __cplusplus
}
#endif

#endif /* _LIBEPOLL_SHIM_SYS_EVENTFD_H_ */
