/*
 * libffi's fficonfig.h for MEECHO on AArch64: what its configure found of
 * the system when it built the prototype (port/gfx, with
 * --disable-exec-static-tramp), kept by hand from then on.
 */
#ifndef FFICONFIG_MEECHO_H
#define FFICONFIG_MEECHO_H

#define EH_FRAME_FLAGS "a"
#define HAVE_AS_CFI_PSEUDO_OP 1
#define HAVE_DLFCN_H 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#define HAVE_INT128 1
#define HAVE_INTTYPES_H 1
#define HAVE_LONG_DOUBLE 1
#define HAVE_MEMCPY 1
#define HAVE_MEMFD_CREATE 1
#define HAVE_RO_EH_FRAME 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1

#define PACKAGE "libffi"
#define PACKAGE_NAME "libffi"
#define PACKAGE_STRING "libffi 3.8.0"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_VERSION "3.8.0"
#define VERSION "3.8.0"

#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_SIZE_T 8

#ifdef HAVE_HIDDEN_VISIBILITY_ATTRIBUTE
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name) .hidden name
#else
#define FFI_HIDDEN __attribute__ ((visibility ("hidden")))
#endif
#else
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name)
#else
#define FFI_HIDDEN
#endif
#endif

#endif
