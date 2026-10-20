# Makefile for wcn: the WCN3990 Wi-Fi of a Qualcomm SoC (the Mi Pad 4's
# SDM660; port/clover/hw-wifi.md).  The firmware runs in the modem; the
# QMI half of its bring-up is a user daemon's, this is the half that needs
# rights: the MSA given through TrustZone, then the copy engines.
PROG=	wcn
SRCS=	wcn.c wcn_hw.c wcn_ce.c wcn_htc.c smmu_route.c adreno_pt.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The route through the apps SMMU, shared with the IPA's service
# (iommu/arm_smmu/smmu_route.c), over the Adreno's LPAE tables
# (adreno_pt.c); their log is ours.
.PATH:	${.CURDIR}/../../iommu/arm_smmu ${.CURDIR}/../../video/adreno
CPPFLAGS+=	-I${.CURDIR}/../../iommu/arm_smmu \
		-I${.CURDIR}/../../video/adreno -DPT_LOG=wlog

DPADD+=	${LIBNETDRIVER} ${LIBCHARDRIVER} ${LIBSYS}
LDADD+=	-lnetdriver -lchardriver -lsys

WARNS?=	5

.include <minix.service.mk>
