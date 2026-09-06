/*
 * fdt_fetch(): a process's own copy of the device tree the kernel booted
 * with.
 *
 * Kept apart from fdt.c on purpose. The reader itself is linked into the
 * kernel, and the kernel cannot make a kernel call to itself; this file is
 * the one that does, so the reader's object stays clean of it.
 *
 * The kernel answers GET_DTB with the whole blob and E2BIG when the buffer
 * offered is too small, and there is no request for the size alone: a caller
 * that only wanted the size would have to make a second call for the data
 * anyway. So this grows the buffer until the blob fits. Trees are a few tens
 * of kilobytes, so the first try is usually the last; the ceiling is there
 * for a blob that is not a device tree at all.
 */

#include <errno.h>
#include <stdlib.h>

#include <minix/syslib.h>
#include <minix/fdt.h>

#define FDT_FETCH_FIRST		(16 * 1024)
#define FDT_FETCH_CEILING	(2 * 1024 * 1024)

void *
fdt_fetch(void)
{
	size_t len = FDT_FETCH_FIRST;
	void *buf;
	int r;

	for (;;) {
		if ((buf = malloc(len)) == NULL) {
			errno = ENOMEM;
			return NULL;
		}

		r = sys_getdtb(buf, (int)len);
		if (r == OK) {
			if (!fdt_valid(buf)) {
				/* The kernel handed over something else. */
				free(buf);
				errno = EINVAL;
				return NULL;
			}
			return buf;
		}

		free(buf);

		if (r != E2BIG || len >= FDT_FETCH_CEILING) {
			/*
			 * EINVAL is what an architecture without the request
			 * answers, and ENOENT what one with the request but
			 * no tree answers. Both mean the same to the caller.
			 */
			errno = (r == E2BIG) ? EFBIG : ENOENT;
			return NULL;
		}

		len *= 2;
	}
}
