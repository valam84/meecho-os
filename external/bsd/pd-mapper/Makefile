# pd-mapper (github.com/linux-msm/pd-mapper 5ecd2fe, 2025-12-30;
# BSD-3-Clause): the service locator (servreg, QMI service 64) a Qualcomm
# modem asks for its protection domains - the maps are the firmware's own
# *.jsn files.  postmarketOS has Linux's in-kernel qcom_pd_mapper; without
# one the Mi Pad 4's modem ran for a minute and then reported "Task
# starvation: diag" (port/clover/hw-modem.md, section 8).  Reachover over
# the import in dist/, as its Makefile builds it.
#
# The change in dist/ is under __minix, with its comment (pd-mapper.c):
# the maps come from one directory, the modem's firmware, since there is
# no remoteproc in sysfs to name it.

.include <bsd.own.mk>

PDDIST=		${.CURDIR}/dist

PROG=		pd-mapper
MAN=
BINDIR=		/usr/sbin

.PATH:		${PDDIST}
SRCS=		pd-mapper.c assoc.c json.c servreg_loc.c lzma_decomp.c

CPPFLAGS+=	-I${PDDIST} -I${NETBSDSRCDIR}/external/bsd/qrtr/lib \
		-I${NETBSDSRCDIR}/external/bsd/qrtr/dist/include
CPPFLAGS+=	-D_NETBSD_SOURCE

LDADD+=		-lqrtr -llzma
DPADD+=		${DESTDIR}/usr/lib/libqrtr.a ${DESTDIR}/usr/lib/liblzma.a

.include <bsd.prog.mk>
