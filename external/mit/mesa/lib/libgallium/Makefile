# libgallium: the driver - Mesa's GL ES state tracker, the GLSL and NIR
# compilers, Gallium's auxiliaries and the DRI frontend, and Panfrost
# (its compilers for Midgard, Bifrost and Valhall, and its per-architecture
# code built once for each of v4...v14): what meson links into
# libgallium-26.2.4.so, here a static library.  The sources and their flags
# are srcs.mk's; what MEECHO adds is below.

.include <bsd.init.mk>

LIB=		gallium

.include "srcs.mk"

# 52 MB of NIR's algebraic automaton, generated, kept compressed (gen/).
compiler-nir-nir_opt_algebraic.c: ${MESAGEN}/src/compiler/nir/nir_opt_algebraic.c.gz
	${_MKTARGET_CREATE}
	${TOOL_GZIP} -dc ${.ALLSRC} > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}
CLEANFILES+=	compiler-nir-nir_opt_algebraic.c

# The HUD's toggle signal: MINIX has no SA_SIGINFO (no siginfo delivery,
# <sys/signal.h> leaves it out for __minix), and the handler reads none of
# its arguments - so here it is installed as a plain one.
.for _f in ${SRCS:M*-hud-hud_context.c}
CPPFLAGS.${_f}+=	-DSA_SIGINFO=0
.endfor

# Panfrost's counter thread waits on an eventfd and a timerfd:
# libepoll-shim's, over poll(2).
.for _f in ${SRCS:M*panfrost-lib-kmod-panfrost_kmod.c}
CPPFLAGS.${_f}+=	-isystem ${DESTDIR}/usr/include/libepoll-shim
.endfor

# pan_mod_conv_cso.h uses pthread_mutex_t and includes nothing that
# declares it: POSIX puts it in <sys/types.h>, MINIX's has no POSIX
# threads, so the driver gets <pthread.h> (libgfxcompat) first.
.for _f in ${SRCS:M*gallium-drivers-panfrost-*.c}
CPPFLAGS.${_f}+=	-include pthread.h
.endfor

# freedreno's debug lines name their thread by syscall(SYS_gettid), which
# MINIX's syscall() is not; fd-gettid.h says why and what stands in.
.for _f in ${SRCS:M*gallium-drivers-freedreno-*} ${SRCS:M*gallium-winsys-freedreno-*}
CPPFLAGS.${_f}+=	-include ${.CURDIR}/fd-gettid.h
.endfor

# The ASTC tables take a std::mutex, and MEECHO's libstdc++ is
# single-threaded: none.  onethread-mutex.h gives one that does nothing,
# which is exact for libmthread's threads there (see the header).
.for _f in ${SRCS:M*util-texcompress_astc_luts*.cpp}
CPPFLAGS.${_f}+=	-include ${.CURDIR}/onethread-mutex.h
.endfor

.include <bsd.lib.mk>
