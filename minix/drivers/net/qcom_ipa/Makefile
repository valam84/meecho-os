# Makefile for qcom_ipa: the IPA v2.6L of a Qualcomm SoC, the modem's data
# path to the AP (the Mi Pad 4's SDM660; port/clover/hw-modem.md, section 9).
PROG=	qcom_ipa
SRCS=	ipa.c ipa_probe.c ipa_hw.c ipa_net.c ipa_smmu.c adreno_pt.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The SMMU's page tables are the Adreno's code (adreno_pt.c): the same
# LPAE stage-1 format on an SMMU of the same kind; its log is ours.
.PATH:	${.CURDIR}/../../video/adreno
CPPFLAGS+=	-I${.CURDIR}/../../video/adreno -DPT_LOG=ilog

DPADD+=	${LIBNETDRIVER} ${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lnetdriver -lchardriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
