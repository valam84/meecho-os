/*
 * freedreno's DBG() (src/gallium/drivers/freedreno/freedreno_util.h) puts
 * the thread in each line as syscall(SYS_gettid) - Linux's system call by
 * number.  MINIX has no such number, and its syscall() is not that call at
 * all: _syscall(endpoint, number, message) of minix/lib/libc/sys/syscall.c,
 * which given one argument would send a message from a garbage pointer to a
 * garbage endpoint.  So for the files of the driver the call is made what
 * it can honestly be here: the process id.  libmthread's threads are the
 * process's own (CLAUDE.md, "Потоки libmthread пользовательские"), so no
 * number from the system would tell them apart anyway.
 *
 * The headers that declare syscall() come first, so that the macro below
 * never meets its declaration; one with more than one argument would not
 * compile, which is the point.
 */
#ifndef _MESA_FD_GETTID_H
#define _MESA_FD_GETTID_H

#include <sys/syscall.h>
#include <unistd.h>

#define SYS_gettid	0
#define syscall(n)	((long)getpid())

#endif /* _MESA_FD_GETTID_H */
