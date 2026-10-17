/* Стенд adreno, узел рендеринга: таймеры libsys; ведёт их osmodel.c по
 * часам модели (model_now_us(), 100 тиков в секунду). */
#ifndef _STUB_MINIX_TIMERS_H
#define _STUB_MINIX_TIMERS_H
#include <time.h>
typedef void (*tmr_func_t)(int arg);
typedef struct minix_timer {
	struct minix_timer *tmr_next;
	clock_t tmr_exp_time;
	tmr_func_t tmr_func;
	int tmr_arg;
} minix_timer_t;
void init_timer(minix_timer_t *tp);
void set_timer(minix_timer_t *tp, clock_t ticks, tmr_func_t watchdog, int arg);
void cancel_timer(minix_timer_t *tp);
void expire_timers(clock_t now);
#endif
