# $NetBSD: Makefile,v 1.4 2011/10/11 19:20:14 joerg Exp $

LIBISPRIVATE=	yes

LIB=	mandoc
SRCS=	man.c man_hash.c man_macro.c man_validate.c \
	arch.c att.c lib.c mdoc.c mdoc_argv.c mdoc_hash.c mdoc_macro.c \
	mdoc_man.c mdoc_validate.c msec.c st.c vol.c \
	eqn.c roff.c tbl.c tbl_data.c tbl_layout.c tbl_opts.c \
	mandoc.c read.c
MAN=	mandoc.3

COPTS.mdoc_macro.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.mdoc_man.c+=	${CC_WNO_FORMAT_TRUNCATION}

.include <bsd.lib.mk>
