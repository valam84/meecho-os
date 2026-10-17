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

/*
 * The render node's (msmtest.c, libdrmdriver): VFS and its grants, VM and
 * its sharing, cdevfd(2), the clock - all answered by osmodel.c.  Errors
 * of VFS are negative, as on the machine; EDONTREPLY is MINIX's own.
 */
#include <time.h>
#define NONE		((endpoint_t)0x6ace)
#define VFS_PROC_NR	((endpoint_t)1)
#define EDONTREPLY	9203
#define VM_D		0x0102
int sys_safecopyfrom(endpoint_t src, cp_grant_id_t grant, vir_bytes off,
	vir_bytes addr, size_t len);
int sys_safecopyto(endpoint_t dst, cp_grant_id_t grant, vir_bytes off,
	vir_bytes addr, size_t len);
int sys_vircopy(endpoint_t src_proc, vir_bytes src_v, endpoint_t dst_proc,
	vir_bytes dst_v, phys_bytes bytes, int flags);
int sys_umap(endpoint_t proc_ep, int seg, vir_bytes vir_addr, vir_bytes bytes,
	phys_bytes *phys_addr);
void *vm_remap(endpoint_t d, endpoint_t s, void *da, void *sa, size_t size);
void *vm_remap_ro(endpoint_t d, endpoint_t s, void *da, void *sa,
	size_t size);
endpoint_t sef_self(void);
int getuptime(clock_t *ticks, clock_t *realtime, time_t *boottime);
int cdevfd_new(endpoint_t endpt, devminor_t minor, int flags, uint64_t size);
int cdevfd_get(endpoint_t endpt, int fd);
#endif
