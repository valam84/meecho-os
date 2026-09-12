# Makefile for random driver (RANDOM)
PROG=	random
SRCS=	main.c random.c trng.c virtio_rng.c rijndael_api.c rijndael_alg.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

.PATH:	${.CURDIR}/aes

DPADD+=	${LIBCHARDRIVER} ${LIBVIRTIO} ${LIBSYS}
LDADD+=	-lchardriver -lvirtio -lsys

CPPFLAGS.random.c+=	-I${NETBSDSRCDIR}/minix

.include <minix.service.mk>
