# Makefile for the USB mouse driver (boot protocol)
PROG=	usb_mouse
SRCS=	usb_mouse.c report.c

FILES=	${PROG}.conf
FILESNAME=	${PROG}
FILESDIR=	/etc/system.conf.d

# libsys's timer calls are built on libtimers, so it comes after.
DPADD+=	${LIBINPUTDRIVER} ${LIBUSB} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-linputdriver -lusb -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
