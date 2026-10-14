/*
 * Thread-local storage of libmthread's threads.
 *
 * On aarch64 a thread finds its TLS through TPIDR_EL0 (variant I: the block
 * starts 16 bytes past it).  libc gives the main thread its block before
 * main() (__libc_static_tls_setup(), lib/libc/tls/tls.c) and the kernel
 * keeps the register per process; what is left is the threads.  Each gets
 * a block of its own, a fresh copy of the program's TLS template, from
 * libc's _rtld_tls_allocate(), and mthread_schedule() loads the next
 * thread's before switching to it - EL0 writes the register itself, no
 * system call.  Mesa keeps its current GL context and dispatch table in
 * thread_local variables (stage 11.6), and its worker threads have their
 * own.
 *
 * A program without TLS set up - a service, linked with libminc, which has
 * no tls.c; the register is zero after exec - gets none for its threads
 * either, as before: _rtld_tls_allocate is a weak reference, and the main
 * thread's pointer is what decides.
 */

#include <minix/mthread.h>
#include "global.h"
#include "proto.h"

#if defined(__aarch64__)
#include <sys/tls.h>

#pragma weak _rtld_tls_allocate
#pragma weak _rtld_tls_free

static inline void *
tp_get(void)
{
	void *p;

	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(p));
	return p;
}

static inline void
tp_set(void *p)
{
	__asm__ volatile("msr tpidr_el0, %0" : : "r"(p));
}

/* The main thread's block is libc's, set before main(). */
void
mthread_tls_init_main(mthread_tcb_t *tcb)
{
	tcb->m_tls = tp_get();
}

/* A new thread's: a fresh copy of the template, if the program has TLS. */
void
mthread_tls_new(mthread_tcb_t *tcb)
{
	tcb->m_tls = NULL;
	if (mainthread.m_tls != NULL && _rtld_tls_allocate != NULL)
		tcb->m_tls = _rtld_tls_allocate();
}

void
mthread_tls_free(mthread_tcb_t *tcb)
{
	if (tcb != &mainthread && tcb->m_tls != NULL && _rtld_tls_free != NULL)
		_rtld_tls_free(tcb->m_tls);
	tcb->m_tls = NULL;
}

/* Just before switching to the thread. */
void
mthread_tls_switch(mthread_tcb_t *to)
{
	if (to->m_tls != NULL)
		tp_set(to->m_tls);
}

#else /* !__aarch64__: no thread pointer is kept */

void mthread_tls_init_main(mthread_tcb_t *tcb) { tcb->m_tls = NULL; }
void mthread_tls_new(mthread_tcb_t *tcb) { tcb->m_tls = NULL; }
void mthread_tls_free(mthread_tcb_t *tcb) { tcb->m_tls = NULL; }
void mthread_tls_switch(mthread_tcb_t *to) { (void)to; }

#endif
