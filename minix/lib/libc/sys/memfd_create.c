#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <sys/mman.h>

/*
 * memfd_create(2): an anonymous memory object behind a file descriptor -
 * the thing a Wayland client fills with pixels and passes to the
 * compositor over a socket, which maps the same pages.  The name is for
 * debugging on the systems that keep one; this one does not, and takes it
 * only to be call-compatible with them.
 */
int
memfd_create(const char *name, unsigned int flags)
{
	message m;

	(void)name;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_memfd.flags = (int)flags;

	return _syscall(VFS_PROC_NR, VFS_MEMFD_CREATE, &m);
}
