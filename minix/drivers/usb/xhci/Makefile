# Makefile for the xHCI host controller driver
PROG=	xhci
SRCS=	xhci.c xhci_find.c xhci_rk.c xhci_ring.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBSYS}
LDADD+=	-lsys

WARNS?=	5

.include <minix.service.mk>
