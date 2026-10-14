/*
 * <pthread.h> for the Wayland stack (libgfxcompat): the POSIX names of
 * libmthread's calls and types, which it gives behind _MTHREADIFY_PTHREADS.
 *
 * MEECHO has no kernel threads.  libmthread's are user-level and switch
 * only inside its own calls, so a program that starts threads to use more
 * cores gains nothing (foot is run with workers=0), while one that only
 * takes locks, or runs a thread to the end, works as written.
 */
#ifndef _GFXCOMPAT_PTHREAD_H_
#define _GFXCOMPAT_PTHREAD_H_

/*
 * <minix/mthread.h> includes <minix/config.h> and <minix/const.h>, which is
 * what MINIX's own code wants of it - and some 130 macros a POSIX header
 * has no business defining: EXTERN (a token of Mesa's GLSL grammar), TRUE,
 * FALSE, BYTE, READING, OS_NAME, NR_PROCS...  The one thing mthread.h needs
 * of them is UNUSED(v), in a prototype.  So here, unless the includer has
 * them already, both headers are taken as read for the length of the
 * include, and forgotten after it; and the includer's own UNUSED, whatever
 * it is (foot defines one, before or after including this), is what it has
 * afterwards.
 */
#if !defined(_CONFIG_H) && !defined(_MINIX_CONST_H)
#define _GFXCOMPAT_NO_MINIX_CONST
#define _CONFIG_H
#define _MINIX_CONST_H
#endif
#pragma push_macro("UNUSED")
#undef UNUSED
#define UNUSED(v) v

#define _MTHREADIFY_PTHREADS
#include <minix/mthread.h>

#undef UNUSED
#pragma pop_macro("UNUSED")
#ifdef _GFXCOMPAT_NO_MINIX_CONST
#undef _CONFIG_H
#undef _MINIX_CONST_H
#undef _GFXCOMPAT_NO_MINIX_CONST
#endif

/*
 * "Inclusion of the <pthread.h> header shall make symbols defined in the
 * headers <sched.h> and <time.h> visible" (POSIX), and code counts on it:
 * Mesa's Panfrost asks sched_setscheduler() for its counter thread with
 * nothing but <pthread.h> included.
 */
#include <sched.h>
#include <time.h>

#endif
