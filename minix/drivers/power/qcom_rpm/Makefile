# Makefile for qcom_rpm: the regulators a Qualcomm machine's device tree
# keeps on, asked of the RPM over GLINK (the Mi Pad 4's touch screen supply).
PROG=	qcom_rpm
SRCS=	rpm.c glink.c

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
