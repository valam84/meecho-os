/* Host stand: what rpm.c takes from <minix/drivers.h>. */
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
#define UNUSED(x) x __attribute__((unused))
#define ANY ((int)0x7ace)
typedef struct { int m_type; } message;
int sef_receive_status(int src, message *m, int *status);
#endif
