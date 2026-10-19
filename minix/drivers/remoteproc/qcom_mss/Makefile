# Makefile for qcom_mss: the modem of a Qualcomm SoC loaded and started
# (the Mi Pad 4's SDM660; port/clover/hw-modem.md).
PROG=	qcom_mss
SRCS=	mss.c mss_find.c mss_boot.c mss_fw.c smem.c smp2p.c \
	qcom_soc.c adreno_mdt.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The GCC's tables are qcom_touch's (qcom_soc.c), the MDT reader the
# Adreno's (adreno_mdt.c, here with room for the modem's thirty program
# headers).
.PATH:	${.CURDIR}/../../hid/qcom_touch ${.CURDIR}/../../video/adreno
CPPFLAGS+=	-I${.CURDIR}/../../hid/qcom_touch \
		-I${.CURDIR}/../../video/adreno -DMDT_MAXPH=64

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
