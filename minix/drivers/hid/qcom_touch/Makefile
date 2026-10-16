# Makefile for qcom_touch: an FT5x06 touch screen on a Qualcomm QUP I2C bus
# (the Mi Pad 4) as the on-screen keyboard's keys.
PROG=	qcom_touch
SRCS=	touch.c qup.c keys.c qcom_soc.c


# libsys's timer calls are built on libtimers, so it comes after.
DPADD+=	${LIBINPUTDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-linputdriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
