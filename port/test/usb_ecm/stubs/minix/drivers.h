/*
 * usb_ecm stand: what the driver takes from <minix/drivers.h> - the types
 * of a message and an endpoint, SEF, the environment, the clock - with the
 * stand's own functions behind them (ecmtest.c).
 */
#ifndef _STUB_MINIX_DRIVERS_H
#define _STUB_MINIX_DRIVERS_H

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define OK		0
#define NONE		(-1)
#define ANY		(-2)
#define CLOCK		(-3)
#define TRUE		1
#define FALSE		0
#define SUSPEND		(-998)
#define EP_SET		1

typedef int endpoint_t;

typedef struct {
	endpoint_t m_source;
	int m_type;
	long m_l1, m_l2, m_l3;
} message;

#define is_ipc_notify(status)	((status) == 1)

typedef struct { int unused; } sef_init_info_t;

int env_parse(const char *env, const char *fmt, int field, long *param,
	long min, long max);
void env_setargs(int argc, char **argv);
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *));
void sef_setcb_signal_handler(void (*cb)(int));
void sef_startup(void);
void sef_cancel(void);
int sef_receive_status(endpoint_t src, message *m, int *status);
int ipc_sendrec(endpoint_t dst, message *m);
int sys_setalarm(clock_t ticks, int abs_time);
clock_t sys_hz(void);
void panic(const char *fmt, ...) __attribute__((noreturn));

#endif
