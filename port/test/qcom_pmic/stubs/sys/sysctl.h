/*
 * What <minix/rmib.h> takes from NetBSD's <sys/sysctl.h>, for a host build
 * of pmic.c (dttest.c): the node types and flags its tables are made of.
 * The values are sys/sys/sysctl.h's; the host has no such header.
 */
#ifndef _STUB_SYS_SYSCTL_H
#define _STUB_SYS_SYSCTL_H

#include <stdbool.h>
#include <sys/types.h>

#define CTLTYPE_NODE		1
#define CTLTYPE_INT		2
#define CTLTYPE_STRING		3
#define CTLTYPE_QUAD		4
#define CTLTYPE_STRUCT		5
#define CTLTYPE_BOOL		6

#define CTLFLAG_READONLY	0x00000000
#define CTLFLAG_READWRITE	0x00000070
#define CTLFLAG_PERMANENT	0x00000200
#define CTLFLAG_IMMEDIATE	0x00000800
#define CTLFLAG_ROOT		0x00002000

#ifndef SCPVEC_NR
#define SCPVEC_NR		64
#endif

#endif /* _STUB_SYS_SYSCTL_H */
