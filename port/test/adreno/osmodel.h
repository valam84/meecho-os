/*
 * Модель окружения службы adreno для стенда узла рендеринга (osmodel.c):
 * VFS, VM, libchardriver, часы.
 */
#ifndef _OSMODEL_H
#define _OSMODEL_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>

#include <minix/chardriver.h>

#define OSM_SELF_EP	0x5eef		/* sef_self() службы */

extern unsigned osm_errors;
extern char osm_error_text[512];
extern endpoint_t osm_user;		/* от кого текущий вызов */
void osm_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

clock_t osm_ticks(void);
int osm_timers_pending(void);
int64_t osm_mono_ns(void);

cp_grant_id_t osm_grant(void *buf, size_t len);
int osm_reply(cdev_id_t id, int *r);

struct osm_remap {
	endpoint_t who;
	void *src, *addr;
	size_t len;
	int ro;
};
extern struct osm_remap osm_remaps[256];
extern unsigned osm_nremaps;

void osm_ds_set(const char *key, uint32_t val);	/* fb's keys in DS */

extern int (*osm_cdev_close)(devminor_t minor);
int osm_fd_pass(endpoint_t from, int fd, endpoint_t to);
int osm_fd_close(endpoint_t who, int fd);
void osm_proc_exit(endpoint_t who);
int osm_fd_device(endpoint_t who, devminor_t minor);
int osm_fd_minor(endpoint_t who, int fd);
unsigned osm_files_open(void);
unsigned osm_select_reply(devminor_t minor);
void osm_reset(void);

void *model_mmap(void *addr, size_t len, int prot, int flags, int fd,
	off_t off);
int model_munmap(void *addr, size_t len);
void *model_vm_remap(endpoint_t d, endpoint_t s, void *da, void *sa,
	size_t size);
void *model_vm_remap_ro(endpoint_t d, endpoint_t s, void *da, void *sa,
	size_t size);

#endif
