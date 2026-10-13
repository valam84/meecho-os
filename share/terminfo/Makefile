#	$NetBSD: Makefile,v 1.3 2012/06/03 23:19:11 joerg Exp $
#	from: @(#)Makefile	8.1 (Berkeley) 6/8/93

CLEANFILES= terminfo.cdb
realall: terminfo.cdb
FILES=terminfo.cdb terminfo
.if defined(__MINIX)
FILESDIR=/usr/share/terminfo
.else
FILESDIR=${BINDIR}/misc
.endif

.if defined(__MINIX)
# MEECHO: foot, the terminal of the desktop (stage 11.4), says TERM=foot,
# and this source (ncurses' of 2015) has no entry for it: every program
# in it said "No entry for terminal type" and fell back to a dumb
# terminal.  foot's own description is compiled in with the rest, from
# foot's import - one copy, which an update of foot updates.  libterminfo
# reads no other database than this one ($TERMINFO and ~/.terminfo
# aside).  foot-direct loses its colors#16777216: numbers here are 16
# bits, and tic drops the larger one.
FOOTINFO=	${NETBSDSRCDIR}/external/mit/foot/dist/foot.info
TISRC=		terminfo.all
CLEANFILES+=	terminfo.all
FILES=		terminfo.cdb terminfo.all
FILESNAME_terminfo.all=	terminfo

terminfo.all: terminfo ${FOOTINFO}
	${_MKTARGET_CREATE}
	{ cat "${.CURDIR}/terminfo"; \
	  ${TOOL_SED} 's/@default_terminfo@/foot/g' "${FOOTINFO}"; } \
	    > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}
.else
TISRC=		${.CURDIR}/terminfo
.endif

terminfo.cdb: ${TISRC} ${TOOL_TIC}
	${_MKTARGET_CREATE}
	${TOOL_TIC} -ax -o ${.TARGET} "${TISRC}"

.include <bsd.prog.mk>
