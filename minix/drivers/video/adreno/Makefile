# Makefile for the Adreno GPU service (Qualcomm SDM660: the Mi Pad 4's
# Adreno 512, found in the device tree; port/clover/hw-gpu.md)
PROG=	adreno
SRCS=	adreno.c adreno_find.c adreno_run.c adreno_cc.c adreno_hw.c \
	adreno_smmu.c adreno_pt.c adreno_tz.c adreno_fw.c adreno_mdt.c \
	adreno_zap.c adreno_cp.c adreno_gpu.c adreno_drm.c qcom_soc.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The GCC's tables and looks are qcom_touch's (qcom_soc.c, sdm660_gcc.h),
# and the GPUCC's table lies beside them (sdm660_gpucc.h); all generated
# from the Linux sources by port/clover/gen-sdm660-tables.py.
.PATH:	${.CURDIR}/../../hid/qcom_touch
CPPFLAGS+=	-I${.CURDIR}/../../hid/qcom_touch

# The render node speaks msm DRM: msm_drm.h of the libdrm reachover (the
# kernel's, as Mesa carries it - found before libdrm's own old copy) and
# drm.h; dma-bufs and sync files are libdrmdriver's, its headers
# MEECHO's own.
CPPFLAGS+=	-I${NETBSDSRCDIR}/external/mit/libdrm/lib/libdrm/drm \
		-I${NETBSDSRCDIR}/external/mit/libdrm/dist/include/drm \
		-I${NETBSDSRCDIR}/minix/lib/libgfxcompat

DPADD+=	${LIBDRMDRIVER} ${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-ldrmdriver -lchardriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
