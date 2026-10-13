#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/*
 * msync(2): write the pages of a shared mapping back to its file.  Here
 * there is nothing to write back, ever: VM maps a file shared only
 * read-only (a writable shared mapping of a file is refused, see
 * servers/vm/mmap.c), and a memory object (memfd_create(2)) has no file
 * under it - its pages are the object.  So the call checks what it is
 * given and succeeds, which is what it would do after writing nothing.
 * libwayland calls it before it moves a shared pool (wl_os_mremap_maymove).
 */
int
__msync13(void *addr, size_t len, int flags)
{
	(void)len;

	if (((uintptr_t)addr & (uintptr_t)(getpagesize() - 1)) != 0 ||
	    (flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
	    (flags & (MS_ASYNC | MS_SYNC)) == (MS_ASYNC | MS_SYNC)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
