/* dwc3_ecm stand: the sdmmc stand's <minix/syslib.h>, and sys_cachectl(). */
#ifndef _STUB_DWC3_SYSLIB_H
#define _STUB_DWC3_SYSLIB_H
#include_next <minix/syslib.h>
int sys_cachectl(int op, void *addr, size_t len);
#endif
