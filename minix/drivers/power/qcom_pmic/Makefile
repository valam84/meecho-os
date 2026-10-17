# Makefile for qcom_pmic: the buttons, backlight and battery of a Qualcomm
# PM660/PM660L pair over the SPMI PMIC arbiter (the Mi Pad 4).
PROG=	qcom_pmic
SRCS=	pmic.c spmi.c typec.c


# libsys's timer calls are built on libtimers, so it comes after.
DPADD+=	${LIBINPUTDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-linputdriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
