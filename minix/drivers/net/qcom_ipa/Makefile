# Makefile for qcom_ipa: the IPA v2.6L of a Qualcomm SoC, the modem's data
# path to the AP (the Mi Pad 4's SDM660; port/clover/hw-modem.md, section 9).
PROG=	qcom_ipa
SRCS=	ipa.c ipa_probe.c ipa_hw.c ipa_net.c ipa_smmu.c smmu_route.c 	adreno_pt.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The route through the apps SMMU is shared with the Wi-Fi's service
# (iommu/arm_smmu/smmu_route.c); its page tables are the Adreno's code
# (adreno_pt.c), the same LPAE stage-1 format; their log is ours.
.PATH:	${.CURDIR}/../../iommu/arm_smmu ${.CURDIR}/../../video/adreno
CPPFLAGS+=	-I${.CURDIR}/../../iommu/arm_smmu 		-I${.CURDIR}/../../video/adreno -DPT_LOG=ilog

DPADD+=	${LIBNETDRIVER} ${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lnetdriver -lchardriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
