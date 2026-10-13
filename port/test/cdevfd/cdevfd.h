/*
 * The test driver's ioctls (cdevfdtest.c) and its client's (cdevfdt.c):
 * the cdevfd(2) back-call of VFS, without a GPU.  Stage 11.6.
 */
#ifndef CDEVFD_TEST_H
#define CDEVFD_TEST_H

#include <sys/ioccom.h>
#include <stdint.h>

#define CFT_MAJOR	17		/* HELLO_MAJOR: free on this machine */
#define CFT_OBJ		100		/* the first object minor */
#define CFT_NOBJ	256

struct cft {
	int32_t fd;			/* NEW: out; GET: in */
	int32_t minor;			/* NEW, OPENS: in; GET, WHO: out */
	int32_t flags;			/* NEW: O_RDONLY/O_RDWR, O_CLOEXEC */
	int32_t result;			/* what cdevfd_*() said, or a count */
	uint64_t size;			/* NEW: for lseek(SEEK_END) */
	int32_t r[8];			/* PROBE: the refusals */
};

#define CFT_NEW		_IOWR('Z', 1, struct cft)
#define CFT_GET		_IOWR('Z', 2, struct cft)
#define CFT_OPENS	_IOWR('Z', 3, struct cft)
#define CFT_WHO		_IOWR('Z', 4, struct cft)
#define CFT_PROBE	_IOWR('Z', 5, struct cft)

#endif
