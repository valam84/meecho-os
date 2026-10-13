/*
 * Wayland's config.h for MEECHO: what meson's checks found of the system
 * when it built the prototype (port/gfx), kept by hand from then on.
 */
#ifndef WAYLAND_MEECHO_CONFIG_H
#define WAYLAND_MEECHO_CONFIG_H

#undef HAVE_ACCEPT4
#undef HAVE_GETTID
#undef HAVE_MREMAP
#undef HAVE_POSIX_FALLOCATE
#undef HAVE_PRCTL
#undef HAVE_SYS_PRCTL_H
#undef HAVE_SYS_PROCCTL_H

#define HAVE_BROKEN_MSG_CMSG_CLOEXEC 0

/* memfd_create(2): memory objects, stage 11.2. */
#define HAVE_MEMFD_CREATE

#define HAVE_MKOSTEMP
#define HAVE_STRNDUP

/* <sys/ucred.h> with NetBSD's struct xucred, which has no pid. */
#define HAVE_SYS_UCRED_H
#define HAVE_XUCRED_CR_PID 0

#define PACKAGE "wayland"
#define PACKAGE_VERSION "1.26.0"

#endif
