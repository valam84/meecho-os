#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <sys/socket.h>

/*
 * sendmmsg(2) over sendmsg(2).  On NetBSD this is a system call that hands
 * the kernel a whole vector at once; here the vector is walked in libc, one
 * message per call, which is what the interface promises and nothing about
 * the cost.  The first failure ends the batch: what was sent is reported as
 * the count, and only a failure on the first message is an error.  Same
 * rule as the kernel's.
 */
int
sendmmsg(int fd, struct mmsghdr *mmsg, unsigned int vlen, unsigned int flags)
{
	unsigned int i;
	ssize_t r;

	for (i = 0; i < vlen; i++) {
		r = sendmsg(fd, &mmsg[i].msg_hdr, (int)flags);
		if (r < 0)
			return i > 0 ? (int)i : -1;
		mmsg[i].msg_len = (unsigned int)r;
	}
	return (int)i;
}
