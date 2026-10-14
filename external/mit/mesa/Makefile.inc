# Mesa 26.2.4, reachover over the import in dist/ and the generated files
# in gen/ (both made by port/gfx/mesa-import.sh).  The lists of sources and
# the flags meson gives each of its targets are in lib/*/srcs.mk, made by
# port/gfx/mesa-reachover.py; what MEECHO decided is here and in the
# Makefiles beside them.

.if !defined(_MESA_MAKEFILE_INC_)
_MESA_MAKEFILE_INC_=	1

.include <bsd.own.mk>

MESADIST=	${NETBSDSRCDIR}/external/mit/mesa/dist
MESAGEN=	${NETBSDSRCDIR}/external/mit/mesa/gen

# Mesa is partly C++ (the GLSL compiler, the GL state tracker), and C++ is
# in the toolchain's second pass, ~/xtools-aarch64-cxx (port/build-xtools.sh
# cxx), until the two passes are one prefix.  Only the compilers come from
# there: binutils are the first prefix's either way.
MESA_TOOLCHAIN?=	${EXTERNAL_TOOLCHAIN}-cxx
CC=		${MESA_TOOLCHAIN}/bin/${MACHINE_GNU_PLATFORM}-gcc
CXX=		${MESA_TOOLCHAIN}/bin/${MACHINE_GNU_PLATFORM}-c++

# util/detect_os.h knows no MINIX and leaves its DETECT_OS_* at 0 - but each
# only #ifndef, so the command line is the header's own knob.  POSIX, and
# NetBSD at the level Mesa asks it of: the C library and the sysctl tree
# (hw.physmem64, kern.proc_args, kern.cp_time, HW_NCPUONLINE - mib has them
# all).  __NetBSD__ itself stays undefined: the code sees MINIX.
CPPFLAGS+=	-DDETECT_OS_POSIX=1 -DDETECT_OS_BSD=1 -DDETECT_OS_NETBSD=1

# POSIX threads: libmthread's, under their POSIX names (libgfxcompat).
CPPFLAGS+=	-isystem ${DESTDIR}/usr/include/gfxcompat

# Third-party code, built with the flags its own build gives it (srcs.mk):
# the tree's warnings and -Werror are not added on top.
WARNS=		0
NOGCCERROR=	yes

# Static only, as the userland is.
MKPIC:=		no
MKPROFILE:=	no
MKLINT:=	no

.endif
