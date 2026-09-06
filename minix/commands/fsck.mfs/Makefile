FSCK=   ${NETBSDSRCDIR}/sbin/fsck
PROG=	fsck_mfs
SRCS=	fsck.c fsck4.c
CPPFLAGS+= -I${NETBSDSRCDIR}/minix/fs -I${FSCK}
BINDIR=	/sbin
MAN=	fsck.mfs.1

.include <bsd.prog.mk>
