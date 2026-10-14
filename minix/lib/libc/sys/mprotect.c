/*
 * mprotect(2).  <sys/mman.h> declared it and nothing implemented it, so a
 * program that calls it - Mesa's Panfrost does, for two debugging aids:
 * marking decoded GPU memory read-only, and guard pages - did not link.
 *
 * MINIX's VM changes no page protections on request: a mapping is
 * writable or not from mmap() on.  So the call says so - ENOTSUP, "the
 * implementation does not support the combination of accesses requested"
 * (POSIX) - and code that only tries goes on without it.  Doing it for
 * real is VM's work: split the region, change its flags and the entries
 * of its pages.
 */
#include <sys/cdefs.h>
#include <sys/mman.h>

#include <errno.h>

int
mprotect(void *addr, size_t len, int prot)
{
	(void)addr;
	(void)len;
	(void)prot;
	errno = ENOTSUP;
	return -1;
}
