# libepoll-shim: epoll, eventfd, timerfd and signalfd over poll(2), for the
# Wayland software of stage 11.  The interface and the layout - headers in
# /usr/include/libepoll-shim, -lepoll-shim, epoll-shim.pc - are those of
# the epoll-shim project the BSDs use, so that a port asking for
# dependency('epoll-shim') finds this one; the code is our own.

LIB=	epoll-shim

SRCS=	fdtab.c epoll.c eventfd.c timerfd.c signalfd.c

CPPFLAGS+=	-I${.CURDIR}/include -I${.CURDIR}

.PATH:	${.CURDIR}/include

INCS=		sys/epoll.h sys/eventfd.h sys/signalfd.h sys/timerfd.h \
		epoll-shim/detail/interpose.h
INCSDIR=	/usr/include/libepoll-shim

FILES+=			epoll-shim.pc
FILESOWN_epoll-shim.pc=	${BINOWN}
FILESGRP_epoll-shim.pc=	${BINGRP}
FILESMODE_epoll-shim.pc= ${NONBINMODE}
FILESDIR_epoll-shim.pc=	/usr/lib/pkgconfig

WARNS?=	5

.include <bsd.lib.mk>
