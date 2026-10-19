# rmtfs (github.com/linux-msm/rmtfs b30a3eb, 2026-06-23; BSD-3-Clause):
# the QMI service (14) through which a Qualcomm modem reads and writes its
# own file systems - on the Mi Pad 4 the eMMC partitions modemst1,
# modemst2, fsg and fsc, which sdmmc serves read only (gptro0=) and rmtfs
# runs with -r: the modem's writes stay in memory, as postmarketOS runs it.
# Reachover over the import in dist/; port/clover/hw-modem.md.
#
# Changes in dist/ are under __minix, each with its comment: the shared
# memory through /dev/qcom_rmtfs_mem1 and its ioctl instead of udev and
# sysfs (sharedmem.c), a block device's size from DIOCGETP (storage.c).
# Beside this file: rproc_minix.c instead of dist/rproc.c (Linux's
# remoteproc sysfs; qcom_mss starts the modem here).  qmi_rmtfs.c is the
# generated coder dist/ carries.

.include <bsd.own.mk>

RMTFSDIST=	${.CURDIR}/dist

PROG=		rmtfs
MAN=
BINDIR=		/usr/sbin

.PATH:		${RMTFSDIST}
SRCS=		qmi_rmtfs.c rmtfs.c sharedmem.c storage.c util.c rproc_minix.c

CPPFLAGS+=	-I${RMTFSDIST} -I${NETBSDSRCDIR}/external/bsd/qrtr/lib \
		-I${NETBSDSRCDIR}/external/bsd/qrtr/dist/include

LDADD+=		-lqrtr
DPADD+=		${DESTDIR}/usr/lib/libqrtr.a

.include <bsd.prog.mk>
