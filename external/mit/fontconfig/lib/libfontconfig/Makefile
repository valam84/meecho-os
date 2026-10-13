# The library, as meson builds it for 2.18.3 (NetBSD's list is of 2.15).

.include <bsd.init.mk>

LIB=	fontconfig

.PATH:	${FONTCONFIGDIST}/src
SRCS=	fcatomic.c \
	fccache.c \
	fccfg.c \
	fccharset.c \
	fcconffile.c \
	fccompat.c \
	fcdbg.c \
	fcdefault.c \
	fcdir.c \
	fcformat.c \
	fcfreetype.c \
	fcfs.c \
	fcgenericalias.c \
	fcptrlist.c \
	fchash.c \
	fcinit.c \
	fclang.c \
	fclist.c \
	fcmatch.c \
	fcmatrix.c \
	fcname.c \
	fcobjs.c \
	fcpat.c \
	fcrange.c \
	fcserialize.c \
	fcstat.c \
	fcstr.c \
	fcweight.c \
	fcxml.c \
	ftglue.c

.PATH:	${FONTCONFIGINCLUDE}/fontconfig ${FONTCONFIGDIST}/fontconfig
INCS=	fontconfig.h fcfreetype.h fcprivate.h
INCSDIR=/usr/include/fontconfig

.include <bsd.lib.mk>
