/*
 * mmap(2) of a character device: the driver's half of the question
 * (<sys/ioc_mmap.h>).  VM maps files, block devices and memory objects; a
 * character device's memory only its driver can name, so when VM says
 * ENODEV - VFS's answer for a character device - the same question goes to
 * the driver on the same descriptor.  A driver that does not know the
 * request says ENOTTY, and mmap(2) fails with ENODEV, as it always did;
 * anything else it says is its answer about this mapping.
 *
 * Apart from mmap.c, which is built with _SYSTEM: there the error codes
 * are the servers' negative ones, and errno after a call is positive, so
 * "errno == ENODEV" could never be true - which is how the first version
 * of this, in mmap.c, never asked a driver at all (found on the board,
 * stage 11.6).  Here they are the user's, as errno is.
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ioc_mmap.h>
#include <sys/mman.h>

#include <errno.h>
#include <string.h>

void *__minix_mmap_cdev(void *, size_t, int, int, int, off_t);

void *
__minix_mmap_cdev(void *addr, size_t len, int prot, int flags, int fd,
	off_t offset)
{
	struct cdev_mmap cm;

	if (errno != ENODEV)
		return MAP_FAILED;

	memset(&cm, 0, sizeof(cm));
	cm.cm_addr = addr;
	cm.cm_len = len;
	cm.cm_prot = prot;
	cm.cm_flags = flags;
	cm.cm_offset = offset;

	if (ioctl(fd, MIOCMMAP, &cm) != 0) {
		if (errno == ENOTTY)
			errno = ENODEV;
		return MAP_FAILED;
	}
	return cm.cm_ret;
}
