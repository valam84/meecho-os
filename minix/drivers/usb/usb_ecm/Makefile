# Makefile for the USB CDC Ethernet (ECM) host driver
PROG=	usb_ecm
SRCS=	usb_ecm.c ecm_desc.c

FILES=	${PROG}.conf
FILESNAME=	${PROG}
FILESDIR=	/etc/system.conf.d

# libsys's timer calls are built on libtimers, so it comes after.
DPADD+=	${LIBNETDRIVER} ${LIBUSB} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lnetdriver -lusb -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
