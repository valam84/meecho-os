# Makefile for the DWMAC network driver
PROG=	dwmac
SRCS=	dwmac.c dwmac_find.c dwmac_rk.c dwmac_pins.c dwmac_mdio.c \
	dwmac_ring.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBNETDRIVER} ${LIBSYS}
LDADD+=	-lnetdriver -lsys

WARNS?=	5

.include <minix.service.mk>
