/* Host stand: what the adreno service and qcom_soc.c take from
 * <minix/drivers.h>. */
#ifndef _STUB_MINIX_DRIVERS_H
#define _STUB_MINIX_DRIVERS_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <minix/type.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#define OK 0
#define UNUSED(x) x __attribute__((unused))

/* The kernel's notifications, as the service waits for its interrupt
 * (smmumodel.c answers). */
#define ANY		((endpoint_t)0x7ace)
#define HARDWARE	((endpoint_t)-2)
#define CLOCK		((endpoint_t)-3)
#define _ENDPOINT_P(e)	(e)
#define NOTIFY_MESSAGE	0x1000
#define STUB_NOTIFY_STATUS	4
#define is_ipc_notify(st)	((st) == STUB_NOTIFY_STATUS)
typedef struct {
	endpoint_t m_source;
	int m_type;
	struct {
		uint64_t interrupts;
		clock_t timestamp;
	} m_notify;
} message;
int ipc_receive(endpoint_t src, message *m, int *status);
#endif
