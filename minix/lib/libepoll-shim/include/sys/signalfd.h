/* <sys/signalfd.h> of libepoll-shim; see <sys/epoll.h> and signalfd.c. */
#ifndef _LIBEPOLL_SHIM_SYS_SIGNALFD_H_
#define _LIBEPOLL_SHIM_SYS_SIGNALFD_H_

#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>

#include <epoll-shim/detail/interpose.h>

#define SFD_CLOEXEC	O_CLOEXEC
#define SFD_NONBLOCK	O_NONBLOCK

/* Linux's record, 128 bytes; only signo, code, pid and uid are filled. */
struct signalfd_siginfo {
	uint32_t ssi_signo;
	int32_t  ssi_errno;
	int32_t  ssi_code;
	uint32_t ssi_pid;
	uint32_t ssi_uid;
	int32_t  ssi_fd;
	uint32_t ssi_tid;
	uint32_t ssi_band;
	uint32_t ssi_overrun;
	uint32_t ssi_trapno;
	int32_t  ssi_status;
	int32_t  ssi_int;
	uint64_t ssi_ptr;
	uint64_t ssi_utime;
	uint64_t ssi_stime;
	uint64_t ssi_addr;
	uint16_t ssi_addr_lsb;
	uint16_t __pad2;
	int32_t  ssi_syscall;
	uint64_t ssi_call_addr;
	uint32_t ssi_arch;
	uint8_t  __pad[28];
};

#ifdef __cplusplus
extern "C" {
#endif

int signalfd(int, const sigset_t *, int);

#ifdef __cplusplus
}
#endif

#endif /* _LIBEPOLL_SHIM_SYS_SIGNALFD_H_ */
