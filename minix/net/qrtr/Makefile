# Makefile for qrtr: Qualcomm's IPC router as AF_QIPCRTR sockets, over
# GLINK to the modem (the Mi Pad 4; port/clover/hw-modem.md).
PROG=	qrtr
SRCS=	qrtr.c router.c glink.c smem.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# SMEM is read and allocated from by the same code as qcom_mss's.
.PATH:	${.CURDIR}/../../drivers/remoteproc/qcom_mss
CPPFLAGS+=	-I${.CURDIR}/../../drivers/remoteproc/qcom_mss

DPADD+=	${LIBSOCKEVENT} ${LIBSOCKDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsockevent -lsockdriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
