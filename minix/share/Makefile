#	$NetBSD: Makefile,v 1.18 2012/06/14 04:14:36 riz Exp $

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	beaglebone
.endif # ${MACHINE_ARCH} == "earm"

# MEECHO's house style: the desktop's colours and what is made of them.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	style
.endif

.include <bsd.subdir.mk>
