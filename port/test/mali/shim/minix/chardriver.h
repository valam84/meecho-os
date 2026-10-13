/*
 * Стенд mali: <minix/chardriver.h>.  Точки входа устройства стенд зовёт
 * сам, как их звал бы libchardriver; отложенные ответы записывает модель.
 */
#ifndef _SHIM_MINIX_CHARDRIVER_H
#define _SHIM_MINIX_CHARDRIVER_H
#include <minix/drivers.h>
typedef unsigned int cdev_id_t;
#define CDEV_CLONED	0x20000000
struct chardriver {
	int (*cdr_open)(devminor_t minor, int access, endpoint_t user_endpt);
	int (*cdr_close)(devminor_t minor);
	ssize_t (*cdr_read)(devminor_t minor, u64_t position, endpoint_t endpt,
		cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
	ssize_t (*cdr_write)(devminor_t minor, u64_t position, endpoint_t endpt,
		cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
	int (*cdr_ioctl)(devminor_t minor, unsigned long request,
		endpoint_t endpt, cp_grant_id_t grant, int flags,
		endpoint_t user_endpt, cdev_id_t id);
	int (*cdr_cancel)(devminor_t minor, endpoint_t endpt, cdev_id_t id);
	int (*cdr_select)(devminor_t minor, unsigned int ops, endpoint_t endpt);
	void (*cdr_intr)(unsigned int mask);
	void (*cdr_alarm)(clock_t stamp);
	void (*cdr_other)(void *m_ptr, int ipc_status);
};
void chardriver_reply_task(endpoint_t endpt, cdev_id_t id, int r);
#endif
