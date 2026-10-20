# Makefile for wcn: the WCN3990 Wi-Fi of a Qualcomm SoC (the Mi Pad 4's
# SDM660; port/clover/hw-wifi.md).  The firmware runs in the modem; the
# QMI half of its bring-up is a user daemon's, this is the half that needs
# rights: the MSA given through TrustZone, then the copy engines.
PROG=	wcn
SRCS=	wcn.c wcn_hw.c wcn_ce.c wcn_htc.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBNETDRIVER} ${LIBCHARDRIVER} ${LIBSYS}
LDADD+=	-lnetdriver -lchardriver -lsys

WARNS?=	5

.include <minix.service.mk>
