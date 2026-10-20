# wcnd: the user half of the WCN3990 Wi-Fi (port/clover/hw-wifi.md,
# section 9) - QMI with the WLAN firmware in the modem over QRTR; the wcn
# service does what needs rights.  The protocol code is the driver's own
# pure files, built here as well.
PROG=	wcnd
SRCS=	wcnd.c wcn_wlfw.c wcn_fw.c
MAN=
BINDIR=	/usr/sbin

WCN=	${NETBSDSRCDIR}/minix/drivers/net/wcn
.PATH:	${WCN}
CPPFLAGS+=	-I${WCN}

.include <bsd.prog.mk>
