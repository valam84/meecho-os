#ifndef _SHIM_MINIX_SYSLIB_H
#define _SHIM_MINIX_SYSLIB_H

#include <minix/drivers.h>

int sys_cachectl(int op, void *addr, size_t len);

#endif /* _SHIM_MINIX_SYSLIB_H */
