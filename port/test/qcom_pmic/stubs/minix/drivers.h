/*
 * Host build of pmic.c (dttest.c): qcom_touch's <minix/drivers.h> stand-in,
 * plus what pmic.c took on with the sysctl node and the MIB messages - a
 * message to look at the sender of, the boot parameter call, and the two
 * NetBSD macros the node tables are written with.
 */
#ifndef _STUB_PMIC_MINIX_DRIVERS_H
#define _STUB_PMIC_MINIX_DRIVERS_H

#include_next <minix/drivers.h>

#include <sys/types.h>

typedef struct {
	int m_source;
	int m_type;
} message;

typedef struct {
	unsigned long iov_addr;
	size_t iov_size;
} iovec_t;

#define MIB_PROC_NR	(-100)

int env_get_param(const char *key, char *value, int max_size);

#ifndef __arraycount
#define __arraycount(a)	(sizeof(a) / sizeof((a)[0]))
#endif
#ifndef __UNCONST
#define __UNCONST(a)	((void *)(unsigned long)(const void *)(a))
#endif

#endif /* _STUB_PMIC_MINIX_DRIVERS_H */
