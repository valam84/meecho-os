# Makefile for dwc3_ecm: a DWC3 USB controller in device mode as a network
# interface to the host on the cable (CDC-ECM) - the Mi Pad 4's USB-C port.
PROG=	dwc3_ecm
SRCS=	dwc3_ecm.c dwc3_find.c dwc3_qcom.c qusb2.c qcom_soc.c

# The GCC tables and looks are qcom_touch's (qcom_soc.c, sdm660_gcc.h),
# generated from the Linux sources by port/clover/gen-sdm660-tables.py.
.PATH:	${.CURDIR}/../../hid/qcom_touch
CPPFLAGS+=	-I${.CURDIR}/../../hid/qcom_touch

# libsys's timer calls are built on libtimers, so it comes after.
DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
