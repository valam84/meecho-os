#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <fcntl.h>

/*
 * posix_fadvise(2): how a file is going to be read.  It is advice, and
 * POSIX lets an implementation take none of it; the file systems here do
 * their own read-ahead and there is nobody to pass the hint to.  So the
 * call checks what it is given and succeeds.  fontconfig (stage 11.4)
 * calls it on its cache files.  Like the call it stands for, it answers an
 * error number rather than setting errno.
 */
int
posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	(void)offset;

	if (fcntl(fd, F_GETFD) < 0)
		return EBADF;
	if (len < 0 || advice < POSIX_FADV_NORMAL ||
	    advice > POSIX_FADV_NOREUSE)
		return EINVAL;
	return 0;
}
