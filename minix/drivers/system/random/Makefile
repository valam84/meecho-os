# Makefile for random driver (RANDOM)
PROG=	random
SRCS=	main.c random.c trng.c virtio_rng.c qcom_prng.c rijndael_api.c \
	rijndael_alg.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

.PATH:	${.CURDIR}/aes

# The Qualcomm PRNG's clock is looked at through the GCC: qcom_touch's
# qcom_soc.c and its tables, on aarch64, where that PRNG is.
.if ${MACHINE_ARCH} == "aarch64"
SRCS+=	qcom_soc.c
.PATH:	${.CURDIR}/../../hid/qcom_touch
CPPFLAGS.qcom_prng.c+=	-I${.CURDIR}/../../hid/qcom_touch
CPPFLAGS.qcom_soc.c+=	-I${.CURDIR}/../../hid/qcom_touch
.endif

DPADD+=	${LIBCHARDRIVER} ${LIBVIRTIO} ${LIBSYS}
LDADD+=	-lchardriver -lvirtio -lsys

CPPFLAGS.random.c+=	-I${NETBSDSRCDIR}/minix

.include <minix.service.mk>
