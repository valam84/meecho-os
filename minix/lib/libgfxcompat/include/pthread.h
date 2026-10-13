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
 * <minix/mthread.h> brings <minix/const.h>, and with it UNUSED(v), a name
 * a POSIX header has no business defining: foot defines its own UNUSED,
 * before or after including this.  So the includer's UNUSED, whatever it
 * is, is what it has afterwards; the declarations below need none.
 */
#pragma push_macro("UNUSED")
#undef UNUSED

#define _MTHREADIFY_PTHREADS
#include <minix/mthread.h>

#undef UNUSED
#pragma pop_macro("UNUSED")

#endif
