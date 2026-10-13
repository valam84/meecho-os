.include "${.PARSEDIR}/../Makefile.inc"

.PATH:	${FONTCONFIGDIST}/${PROG}
MAN=
BINDIR=	/usr/bin

# The library from its own object directory, as external/mit/expat's
# xmlwf takes libexpat: one pass over this package builds both, and the
# library reaches DESTDIR only at install.
.if !defined(FONTCONFIGOBJDIR)
FONTCONFIGOBJDIR!=	cd ${FONTCONFIGDIR}/lib/libfontconfig && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	FONTCONFIGOBJDIR
.endif

LDADD+=	-L${FONTCONFIGOBJDIR} -lfontconfig -lfreetype -lexpat -lz \
	-lgfxcompat -lmthread -lm
DPADD+=	${FONTCONFIGOBJDIR}/libfontconfig.a ${DESTDIR}/usr/lib/libfreetype.a \
	${LIBEXPAT} ${LIBZ} ${DESTDIR}/usr/lib/libgfxcompat.a ${LIBMTHREAD} \
	${LIBM}
