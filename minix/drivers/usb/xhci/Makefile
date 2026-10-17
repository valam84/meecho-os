# Makefile for the xHCI host controller driver
PROG=	xhci
SRCS=	xhci.c xhci_find.c xhci_dwc3.c xhci_rk.c xhci_ring.c xhci_dev.c \
	xhci_tree.c xhci_urb.c

# Qualcomm's glue is dwc3_ecm's - its tree reader, wrapper and PHY - with
# the GCC looks of qcom_touch's qcom_soc.c, as dwc3_ecm itself takes them:
# the port is the same, a device to the network and a host to a keyboard.
SRCS+=	xhci_qcom.c dwc3_find.c dwc3_qcom.c qusb2.c qcom_soc.c
.PATH:	${.CURDIR}/../../net/dwc3_ecm ${.CURDIR}/../../hid/qcom_touch
CPPFLAGS+=	-I${.CURDIR}/../../net/dwc3_ecm -I${.CURDIR}/../../hid/qcom_touch

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

DPADD+=	${LIBSYS}
LDADD+=	-lsys

WARNS?=	5

.include <minix.service.mk>
