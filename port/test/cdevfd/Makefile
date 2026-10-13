# The test driver of cdevfd(2) (cdevfdtest.c); qemu-cdevfd.py builds it.
PROG=	cdevfdtest
SRCS=	cdevfdtest.c
MAN=

DPADD+=	${LIBCHARDRIVER} ${LIBSYS}
LDADD+=	-lchardriver -lsys

.include <minix.service.mk>
