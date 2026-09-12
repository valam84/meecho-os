#	$NetBSD: thread.inc,v 1.2 2025/10/19 00:59:57 riastradh Exp $

.PATH.c:	${OPENSSLSRC}/crypto/thread ${OPENSSLSRC}/crypto/thread/arch

.if defined(__MINIX)
# No threads and therefore no thread pool (see include/openssl/
# configuration.h).  build.info then compiles api.c for libcrypto and
# moves the arch files (thread_none.c is the one that has code here)
# into libssl, where QUIC needs their mutex and condvar; internal.c,
# the pool itself, is left out.  One library or the other -- they are
# kept in libcrypto, where every consumer of libssl finds them.
THREAD_SRCS += \
api.c \
arch.c \
thread_none.c \
thread_posix.c \
thread_win.c \

.else
THREAD_SRCS += \
api.c \
arch.c \
internal.c \
thread_none.c \
thread_posix.c \
thread_win.c \

.endif

SRCS += ${THREAD_SRCS}

.for cryptosrc in ${THREAD_SRCS}
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/thread
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/thread/arch ${THREADCPPFLAGS}
.endfor

CPPFLAGS.thread_posix.c+=	-D_NETBSD_PTHREAD_CREATE_WEAK
