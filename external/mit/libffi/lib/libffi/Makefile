.include <bsd.init.mk>

LIB=		ffi

.PATH:		${LIBFFIDIST}/src ${LIBFFIDIST}/src/aarch64 ${LIBFFIDIST}/include
SRCS=		prep_cif.c types.c raw_api.c java_raw_api.c closures.c tramp.c
SRCS+=		ffi.c sysv.S

# fficonfig.h is MEECHO's, beside this file; ffi.h is made from its
# template below, ffitarget.h is the architecture's own.
CPPFLAGS+=	-DHAVE_CONFIG_H -I. -I${.CURDIR} -I${LIBFFIDIST}/include \
		-I${LIBFFIDIST}/src/aarch64

# The Java raw API is deprecated, and its implementation calls itself.
COPTS.java_raw_api.c+=	${CC_WNO_DEPRECATED_DECLARATIONS}

DPSRCS+=	ffi.h
CLEANFILES+=	ffi.h

INCS=		ffi.h ffitarget.h
INCSDIR=	/usr/include

# What configure substitutes into the template for this machine.  No
# executable trampoline table: that is Apple's (FFI_EXEC_TRAMPOLINE_TABLE).
ffi.h: ffi.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@VERSION@/${LIBFFI_VERSION}/' \
	    -e 's/@TARGET@/AARCH64/' \
	    -e 's/@HAVE_LONG_DOUBLE@/1/' \
	    -e 's/@FFI_VERSION_STRING@/${LIBFFI_VERSION}/' \
	    -e 's/@FFI_VERSION_NUMBER@/${LIBFFI_VERSION_NUMBER}/' \
	    -e 's/@FFI_EXEC_TRAMPOLINE_TABLE@/0/' \
	    < ${.ALLSRC} > ${.TARGET}

.include <bsd.lib.mk>
