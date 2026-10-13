# libgfxcompat: what the Wayland stack of stage 11 needs of the system and
# the system does not have yet.
#
#   <pthread.h>    POSIX thread names over libmthread (_MTHREADIFY_PTHREADS)
#   <threads.h>    C11 threads over the same, thrd_create/thrd_join here
#   sem_*(3)       declared by <semaphore.h>, implemented by nothing else
#   pthread_sigmask(3)  (__libc_thr_sigsetmask)
#   <uchar.h>      C11 char32_t conversions, UTF-8 whatever the locale
#   <linux/input-event-codes.h>  FreeBSD's header (external/bsd/evdev)
#
# The headers go to /usr/include/gfxcompat, not /usr/include, and the code
# is not in libc or libmthread, on purpose: both halves are stand-ins.
# libmthread's threads are user-level and switch only in its own calls -
# a pthread.h in the default path would tell every configure script there
# are POSIX threads, and OpenSSL is built thread-less for this reason - and
# the conversions speak UTF-8 because the static libc has no UTF-8 locale
# (its multibyte locales are libc.so's), not because the locale says so.
# When libc grows <uchar.h> and the locales, and libmthread C11 threads,
# this library shrinks to nothing.  The stack's reachovers say
# -I${DESTDIR}/usr/include/gfxcompat and -lgfxcompat -lmthread.

LIB=	gfxcompat

SRCS=	threads.c semaphore.c sigmask.c uchar.c

CPPFLAGS+=	-I${.CURDIR}/include

.PATH:	${.CURDIR}/include ${NETBSDSRCDIR}/external/bsd/evdev/dist

INCS=		pthread.h threads.h uchar.h input-event-codes.h
INCSDIR=	/usr/include/gfxcompat
INCSDIR_input-event-codes.h=	/usr/include/gfxcompat/linux

WARNS?=	5

.include <bsd.lib.mk>
