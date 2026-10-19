# diag-router (github.com/linux-msm/diag 23c12c1, 2026-07-09;
# BSD-3-Clause): routes the diagnostic streams of a Qualcomm SoC's
# processors - here the modem's, over QRTR (service 4097: control, command,
# data, DCI) - to a host.  postmarketOS runs it on the Mi Pad 4; without a
# taker of its stream the modem stops after some minutes with "Task
# starvation: diag" (port/clover/hw-modem.md, section 8).  Reachover over
# the import in dist/, as its Makefile builds it with libqrtr and without
# libudev.
#
# Beside this file, instead of two of dist/'s: watch_minix.c - the event
# loop over select(2), where dist/router/watch.c drives its queues with
# Linux's aio and an eventfd; usb_minix.c - no FunctionFS gadget and no
# Linux TCGETS here (dist/router/usb.c and uart.c are not built).
# dist/router/peripheral-rpmsg.c needs libudev and Linux's rpmsg devices;
# the modem's diag comes over QRTR.

.include <bsd.own.mk>

DIAGDIST=	${.CURDIR}/dist/router

PROG=		diag-router
MAN=
BINDIR=		/usr/sbin

.PATH:		${DIAGDIST}
SRCS=		app_cmds.c circ_buf.c common_cmds.c diag.c diag_cntl.c dm.c \
		hdlc.c masks.c mbuf.c peripheral.c router.c socket.c \
		unix.c util.c peripheral-qrtr.c
SRCS+=		watch_minix.c usb_minix.c

CPPFLAGS+=	-I${DIAGDIST} -I${NETBSDSRCDIR}/external/bsd/qrtr/lib \
		-I${NETBSDSRCDIR}/external/bsd/qrtr/dist/include
CPPFLAGS+=	-D_NETBSD_SOURCE -DHAS_LIBQRTR=1

# Packed request structures whose members are passed by address: the
# protocol's, in code that is not ours to rearrange.
COPTS.common_cmds.c+=	-Wno-address-of-packed-member

LDADD+=		-lqrtr
DPADD+=		${DESTDIR}/usr/lib/libqrtr.a

.include <bsd.prog.mk>
