/*
 * pixman's config header for MEECHO: what meson's checks found of the
 * system when it built the prototype (port/gfx), kept by hand from then
 * on - with one answer changed, the thread-local storage.
 *
 * A static binary here is given no TLS block: nothing sets up the thread
 * pointer, and a __thread variable is read at address 0 plus its offset.
 * Processes have no kernel threads either, so the plain variable that
 * PIXMAN_NO_TLS gives is exact.  (The prototype patched pixman-compiler.h
 * to the same effect.)  Without TLS, HAVE_PTHREADS would make pixman keep
 * the variable in pthread keys instead: not that either.
 */
#ifndef PIXMAN_MEECHO_CONFIG_H
#define PIXMAN_MEECHO_CONFIG_H

#define PIXMAN_NO_TLS 1

#define ASM_HAVE_FUNC_DIRECTIVE 1
#define HAVE_ALARM 1
#define HAVE_BUILTIN_CLZ 1
#define HAVE_FEDIVBYZERO 1
#define HAVE_FEENABLEEXCEPT 1
#define HAVE_FENV_H 1
#define HAVE_GCC_VECTOR_EXTENSIONS 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_MMAP 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_SIGACTION 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_UNISTD_H 1
#define PACKAGE foo
#define SIZEOF_LONG 8
#define TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR 1
#define TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR 1
#define USE_GCC_INLINE_ASM 1

#endif
