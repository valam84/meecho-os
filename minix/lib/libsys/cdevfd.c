#include "syslib.h"

#include <string.h>

/*
 * File descriptors of a character driver's own device, for a process that
 * is waiting on a request to that driver: the back-call a driver makes to
 * give a process a descriptor of something it made (a buffer, a fence), or
 * to learn which of its things a descriptor the process holds refers to.
 * DRM's PRIME and sync files are built on it (the mali service).  VFS
 * checks that the process is blocked on a request to the caller and that
 * the descriptor is of the caller's device; see do_cdevfd() there.
 */

/*
 * Give process 'endpt' a new descriptor of minor 'minor' of the device it is
 * waiting on, with access and close-on-exec from 'flags' (O_RDONLY, O_RDWR,
 * O_CLOEXEC); lseek(2) to its end says 'size'.  VFS sends no open: the
 * driver counts this as an open of the minor, and the last close of the
 * descriptor's file comes to it as usual.  The descriptor, or an error.
 */
int
cdevfd_new(endpoint_t endpt, devminor_t minor, int flags, uint64_t size)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vfs_cdevfd.endpt = endpt;
	m.m_lsys_vfs_cdevfd.what = CDEVFD_NEW;
	m.m_lsys_vfs_cdevfd.flags = flags;
	m.m_lsys_vfs_cdevfd.minor = minor;
	m.m_lsys_vfs_cdevfd.size = size;

	return _taskcall(VFS_PROC_NR, VFS_CDEVFD, &m);
}

/*
 * Which minor of the caller's device descriptor 'fd' of process 'endpt'
 * refers to: the minor, or an error - EBADF if the process has no such
 * descriptor, EINVAL if it is not of the device the process is waiting on.
 */
int
cdevfd_get(endpoint_t endpt, int fd)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vfs_cdevfd.endpt = endpt;
	m.m_lsys_vfs_cdevfd.what = CDEVFD_GET;
	m.m_lsys_vfs_cdevfd.fd = fd;

	return _taskcall(VFS_PROC_NR, VFS_CDEVFD, &m);
}
