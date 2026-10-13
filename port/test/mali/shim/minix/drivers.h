/*
 * Стенд mali: то немногое из <minix/drivers.h>, чем пользуются mali_find.c
 * и mali_rk.c.  Здесь только объявления; что делают vm_map_phys() и
 * micro_delay(), решает модель в malitest.c, где это видно.
 */
#ifndef _SHIM_MINIX_DRIVERS_H
#define _SHIM_MINIX_DRIVERS_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <minix/type.h>
#define OK		0
#define SELF		0x8ace
#define UNUSED(x)	x __attribute__((unused))
void *vm_map_phys(endpoint_t who, void *phaddr, size_t len);
int micro_delay(u32_t usec);

/* Для mali_job.c: память, кэш, часы и будильник - в модели jobtest.c. */
#define AC_ALIGN4K	0x01
void *alloc_contig(size_t len, int flags, phys_bytes *phys);
int free_contig(void *addr, size_t len);
int sys_cachectl(int op, void *addr, size_t len);
void read_frclock_64(u64_t *frclk);
u64_t delta_frclock_64(u64_t base, u64_t cur);
u32_t frclock_64_to_micros(u64_t tsc);
int sys_setalarm(long exp_time, int abs_time);
u32_t sys_hz(void);
#endif
