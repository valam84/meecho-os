#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <dirent.h>

/* <dirent.h> declares getdents(2) as returning int, and that is what
 * _syscall() hands back. It used to say ssize_t here, which passed unnoticed
 * as long as every MINIX architecture was ILP32 and the two types were the
 * same; on LP64 the compiler rejects the mismatch. */
int getdents(int fd, char *buffer, size_t nbytes)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readwrite.fd = fd;
  m.m_lc_vfs_readwrite.len = nbytes;
  m.m_lc_vfs_readwrite.buf = (vir_bytes)buffer;
  m.m_lc_vfs_readwrite.cum_io = 0;
  return _syscall(VFS_PROC_NR, VFS_GETDENTS, &m);
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(getdents, __getdents30)
#endif
