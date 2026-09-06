#	$NetBSD: bsd.inc.mk,v 1.32 2006/03/16 18:43:34 jwise Exp $

.include <bsd.init.mk>

##### Basic targets
includes:	${INCS} incinstall inclinkinstall

##### Default values
INCSYMLINKS?=

##### Install rules
incinstall::	# ensure existence
.PHONY:		incinstall

# -c is forced on here, in order to preserve modtimes for "make depend"
#
# ${PRESERVE} (-p) is filtered out, because on this path it breaks "make
# depend" instead of helping it.  install -p stamps the installed header with
# the *source* file's mtime, so a header edited at T1 and installed at T2 lands
# in DESTDIR carrying T1.  Anything compiled from the old DESTDIR copy between
# T1 and T2 -- one stray kernel build is enough -- is then newer than the
# header it is stale against, and make reports the tree up to date forever
# after.  The cmp above is what actually makes reinstalling an unchanged header
# a no-op, so -p buys nothing here that we do not already have.
_INCINSTALL_FILE=	${INSTALL_FILE:N-p}

__incinstall: .USE
	@cmp -s ${.ALLSRC} ${.TARGET} > /dev/null 2>&1 || \
	    (${_MKSHMSG_INSTALL} ${.TARGET}; \
	     ${_MKSHECHO} "${_INCINSTALL_FILE} -c -o ${BINOWN} -g ${BINGRP} \
		-m ${NONBINMODE} ${.ALLSRC} ${.TARGET}" && \
	     ${_INCINSTALL_FILE} -c -o ${BINOWN} -g ${BINGRP} \
		-m ${NONBINMODE} ${.ALLSRC} ${.TARGET})

.for F in ${INCS:O:u}
_FDIR:=		${INCSDIR_${F:C,/,_,g}:U${INCSDIR}}	# dir override
_FNAME:=	${INCSNAME_${F:C,/,_,g}:U${INCSNAME:U${F}}} # name override
_F:=		${DESTDIR}${_FDIR}/${_FNAME}		# installed path

.if ${MKUPDATE} == "no"
${_F}!		${F} __incinstall			# install rule
.else
${_F}:		${F} __incinstall			# install rule
.endif

incinstall::	${_F}
.PRECIOUS:	${_F}					# keep if install fails
.endfor

.undef _FDIR
.undef _FNAME
.undef _F

inclinkinstall:	.PHONY
.if !empty(INCSYMLINKS)
	@(set ${INCSYMLINKS}; \
	 while test $$# -ge 2; do \
		l=$$1; shift; \
		t=${DESTDIR}$$1; shift; \
		if  ttarg=`${TOOL_STAT} -qf '%Y' $$t` && \
		    [ "$$l" = "$$ttarg" ]; then \
			continue ; \
		fi ; \
		${_MKSHMSG_INSTALL} $$t; \
		${_MKSHECHO} ${INSTALL_SYMLINK} $$l $$t; \
		${INSTALL_SYMLINK} $$l $$t; \
	 done; )
.endif
