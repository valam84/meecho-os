# Makefile for qcom_rmtfs_mem: the memory a Qualcomm modem shares with
# rmtfs, as /dev/qcom_rmtfs_mem1 (port/clover/hw-modem.md).
PROG=	qcom_rmtfs_mem
SRCS=	rmtfs_mem.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBCHARDRIVER} ${LIBSYS}
LDADD+=	-lchardriver -lsys

WARNS?=	5

.include <minix.service.mk>
