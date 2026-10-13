# Makefile for the Mali GPU service (Bifrost, found in the device tree)
PROG=	mali
SRCS=	mali.c mali_find.c mali_rk.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBSYS}
LDADD+=	-lsys

WARNS?=	5

.include <minix.service.mk>
