#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>

/*
 * recvmmsg(2) over recvmsg(2); see sendmmsg.c for why it lives in libc.
 *
 * MSG_WAITFORONE is the one flag with batch semantics: block for the first
 * message, then take only what is already there.  It is done the way the
 * kernel does it, by turning on MSG_DONTWAIT after the first call.  The
 * timeout is not honoured -- without a kernel loop there is nothing to
 * apply it to between messages -- so a caller that passes one gets the
 * MSG_WAITFORONE behaviour at best.  Callers that need a real deadline
 * poll(2) first; the ones in the tree (libcrypto's datagram BIO) do.
 */
int
recvmmsg(int fd, struct mmsghdr *mmsg, unsigned int vlen, unsigned int flags,
	struct timespec *timeout)
{
	unsigned int i;
	ssize_t r;

	for (i = 0; i < vlen; i++) {
		r = recvmsg(fd, &mmsg[i].msg_hdr, (int)flags);
		if (r < 0) {
			if (i > 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				break;
			return i > 0 ? (int)i : -1;
		}
		mmsg[i].msg_len = (unsigned int)r;
		if (flags & MSG_WAITFORONE)
			flags |= MSG_DONTWAIT;
	}
	return (int)i;
}
