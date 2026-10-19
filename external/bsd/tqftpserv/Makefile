# tqftpserv (github.com/linux-msm/tqftpserv b31de6d, 2026-10-15;
# BSD-3-Clause): the TFTP server a Qualcomm modem reads its configuration
# files from and writes small ones to, over QRTR.  Reachover over the
# import in dist/, as its meson.build makes it without liblzma and libzstd
# (the modem's files are not compressed); port/clover/hw-modem.md.
#
# Changes in dist/ are under __minix, each with its comment (translate.c):
# the read-only files come from one directory - the modem's firmware, where
# qcom_mss loads it from, or $TQFTPSERV_FW - since there is no remoteproc
# in sysfs to ask; the persistent ones go to /var/db/tqftpserv.

.include <bsd.own.mk>

TQFTPDIST=	${.CURDIR}/dist

PROG=		tqftpserv
MAN=
BINDIR=		/usr/sbin

.PATH:		${TQFTPDIST}
SRCS=		tqftpserv.c translate.c

CPPFLAGS+=	-I${TQFTPDIST} -I${NETBSDSRCDIR}/external/bsd/qrtr/lib \
		-I${NETBSDSRCDIR}/external/bsd/qrtr/dist/include
CPPFLAGS+=	-D_NETBSD_SOURCE

LDADD+=		-lqrtr
DPADD+=		${DESTDIR}/usr/lib/libqrtr.a

.include <bsd.prog.mk>
