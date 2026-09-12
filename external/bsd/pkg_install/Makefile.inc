# $NetBSD: Makefile.inc,v 1.8 2023/06/03 21:30:21 lukem Exp $

DIST=	${NETBSDSRCDIR}/external/bsd/pkg_install/dist

USE_FORT?=yes	# network client

CPPFLAGS+=-I${DIST}/lib
CPPFLAGS+=-I${NETBSDSRCDIR}/external/bsd/pkg_install/lib
CPPFLAGS+=-DHAVE_CONFIG_H -DNETBSD -DHAVE_SSL
.if !defined(__MINIX)
CPPFLAGS+=-DSYSCONFDIR='"/etc"'
.else
# Where MINIX always kept them: the tools live in base, the packages and
# their database under /usr/pkg, as pkgsrc's own pkg_install would put
# them.  OPSYS_NAME is what +BUILD_INFO has to say for pkg_add to accept a
# package (see port/mkpkg.sh).
CPPFLAGS+=-DSYSCONFDIR='"/usr/pkg/etc"'
CPPFLAGS+=-DDEF_LOG_DIR='"/usr/pkg/var/db/pkg"'
CPPFLAGS+=-DOPSYS_NAME='"Minix"'
# The libarchive in this tree is 2.8 (2012); pkg_install speaks the 3.0
# names, and for what it uses those are the same functions renamed:
# "compression" became "filter" and "finish" became "free".  Mapped here
# rather than in dist/, so that the import stays refreshable and the day
# libarchive is brought up to date this block is simply deleted.
CPPFLAGS+=-Darchive_read_free=archive_read_finish
CPPFLAGS+=-Darchive_write_free=archive_write_finish
CPPFLAGS+=-Darchive_read_support_filter_gzip=archive_read_support_compression_gzip
CPPFLAGS+=-Darchive_read_support_filter_bzip2=archive_read_support_compression_bzip2
CPPFLAGS+=-Darchive_read_support_filter_xz=archive_read_support_compression_xz
CPPFLAGS+=-Darchive_write_add_filter_gzip=archive_write_set_compression_gzip
CPPFLAGS+=-Darchive_write_add_filter_bzip2=archive_write_set_compression_bzip2
CPPFLAGS+=-Darchive_write_add_filter_xz=archive_write_set_compression_xz
.endif # !defined(__MINIX)

WARNS=	4
CWARNFLAGS+=	-Wno-missing-noreturn

.include <bsd.own.mk>

# show_version() does not return
COPTS.main.c+=		${CC_WNO_IMPLICIT_FALLTHROUGH}
COPTS.pkg_delete.c+=	${CC_WNO_IMPLICIT_FALLTHROUGH}
