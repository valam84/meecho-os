/* Host stand: what touch.c and qcom_soc.c take from <minix/drivers.h>. */
#ifndef _STUB_MINIX_DRIVERS_H
#define _STUB_MINIX_DRIVERS_H
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <minix/type.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#define OK 0
/* A message, as far as touch.c looks into one. */
typedef struct { int m_source, m_type; } message;
typedef int endpoint_t;
typedef uint32_t u32_t;
#define DS_PROC_NR	6
#define is_ipc_notify(s)	((s) != 0)
#define UNUSED(x) x __attribute__((unused))
int sys_setalarm(clock_t ticks, int abs_time);
clock_t sys_hz(void);
#endif
