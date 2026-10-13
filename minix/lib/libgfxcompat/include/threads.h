/*
 * <threads.h> (C11) for the Wayland stack (libgfxcompat): over the POSIX
 * thread names that libmthread gives (see pthread.h here for what such a
 * thread is worth).
 */
#ifndef _GFXCOMPAT_THREADS_H_
#define _GFXCOMPAT_THREADS_H_

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/*
 * A thrd_t is libmthread's thread number plus two.  Its numbers start at
 * 0 (the main thread is -1, "no thread" -2), and code written against
 * pthread_t being a pointer takes 0 for "no thread": foot's font loaders
 * did, and the first of four - thread 0 - always counted as not started.
 * Plus two keeps 0 for "no thread" and gives the main thread 1.
 */
typedef pthread_t thrd_t;
#define THRD_FROM_PTHREAD(id)	((thrd_t)((id) + 2))
#define THRD_TO_PTHREAD(t)	((pthread_t)((t) - 2))
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_key_t tss_t;
typedef pthread_once_t once_flag;
typedef int (*thrd_start_t)(void *);
typedef void (*tss_dtor_t)(void *);

#define ONCE_FLAG_INIT	PTHREAD_ONCE_INIT
#define thread_local	_Thread_local

enum { thrd_success = 0, thrd_busy, thrd_error, thrd_nomem, thrd_timedout };
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };

static inline int mtx_init(mtx_t *m, int type)
{ (void)type; return pthread_mutex_init(m, NULL) ? thrd_error : thrd_success; }
static inline int mtx_lock(mtx_t *m)
{ return pthread_mutex_lock(m) ? thrd_error : thrd_success; }
static inline int mtx_trylock(mtx_t *m)
{ return pthread_mutex_trylock(m) ? thrd_busy : thrd_success; }
static inline int mtx_unlock(mtx_t *m)
{ return pthread_mutex_unlock(m) ? thrd_error : thrd_success; }
static inline void mtx_destroy(mtx_t *m)
{ (void)pthread_mutex_destroy(m); }

static inline int cnd_init(cnd_t *c)
{ return pthread_cond_init(c, NULL) ? thrd_error : thrd_success; }
static inline int cnd_signal(cnd_t *c)
{ return pthread_cond_signal(c) ? thrd_error : thrd_success; }
static inline int cnd_broadcast(cnd_t *c)
{ return pthread_cond_broadcast(c) ? thrd_error : thrd_success; }
static inline int cnd_wait(cnd_t *c, mtx_t *m)
{ return pthread_cond_wait(c, m) ? thrd_error : thrd_success; }
static inline void cnd_destroy(cnd_t *c)
{ (void)pthread_cond_destroy(c); }

static inline void call_once(once_flag *f, void (*fn)(void))
{ (void)pthread_once(f, fn); }

static inline int tss_create(tss_t *k, tss_dtor_t d)
{ return pthread_key_create(k, d) ? thrd_error : thrd_success; }
static inline void *tss_get(tss_t k) { return pthread_getspecific(k); }
static inline int tss_set(tss_t k, void *v)
{ return pthread_setspecific(k, v) ? thrd_error : thrd_success; }
static inline void tss_delete(tss_t k) { (void)pthread_key_delete(k); }

/* In libgfxcompat (threads.c). */
int thrd_create(thrd_t *, thrd_start_t, void *);
int thrd_join(thrd_t, int *);
static inline thrd_t thrd_current(void)
{ return THRD_FROM_PTHREAD(pthread_self()); }
static inline int thrd_equal(thrd_t a, thrd_t b) { return a == b; }
/* The others run first, if any can: libmthread's own yield. */
static inline void thrd_yield(void) { (void)pthread_yield(); }

#endif
