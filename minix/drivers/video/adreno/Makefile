# Makefile for the Adreno GPU service (Qualcomm SDM660: the Mi Pad 4's
# Adreno 512, found in the device tree; port/clover/hw-gpu.md)
PROG=	adreno
SRCS=	adreno.c adreno_find.c adreno_run.c adreno_cc.c adreno_hw.c \
	adreno_tz.c qcom_soc.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The GCC's tables and looks are qcom_touch's (qcom_soc.c, sdm660_gcc.h),
# and the GPUCC's table lies beside them (sdm660_gpucc.h); all generated
# from the Linux sources by port/clover/gen-sdm660-tables.py.
.PATH:	${.CURDIR}/../../hid/qcom_touch
CPPFLAGS+=	-I${.CURDIR}/../../hid/qcom_touch

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
