# libqrtr (github.com/linux-msm/qrtr 29e36ae, 2026-09-15; BSD-3-Clause):
# QRTR sockets and the QMI message coder, for rmtfs, tqftpserv and any
# client of the modem's services (port/clover/hw-modem.md).  Reachover
# over the import in dist/, as its meson.build makes the library.
#
# Beside this file: linux/qrtr.h, what the library includes as Linux's
# header - <netqrtr/qrtr.h> and Linux's __le32.  Users build with
# -I/usr/include/libqrtr (both headers are installed there) and -lqrtr.

.include <bsd.own.mk>

QRTRDIST=	${.CURDIR}/../dist

LIB=		qrtr
.PATH:		${QRTRDIST}/lib ${QRTRDIST}/include
SRCS=		qrtr.c qmi.c logging.c

CPPFLAGS+=	-I${.CURDIR} -I${QRTRDIST}/include

INCS=		libqrtr.h
INCSDIR=	/usr/include/libqrtr

FILES=		${.CURDIR}/linux/qrtr.h
FILESDIR=	/usr/include/libqrtr/linux

.include <bsd.files.mk>
.include <bsd.lib.mk>
