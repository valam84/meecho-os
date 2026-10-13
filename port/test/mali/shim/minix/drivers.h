/*
 * Стенд mali: то немногое из <minix/drivers.h>, чем пользуется служба.
 * Здесь только объявления; что делают vm_map_phys(), micro_delay(),
 * память, кэш, часы, гранты и копирование, решает модель (malitest.c
 * для первой половины, model.c для MMU, буферов, заданий и устройства),
 * где это видно.
 */
#ifndef _SHIM_MINIX_DRIVERS_H
#define _SHIM_MINIX_DRIVERS_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <minix/type.h>
#define OK		0
#define SELF		0x8ace
#define VFS_PROC_NR	1
#define NONE		0x6ace
/*
 * cdevfd(2): ошибки - отрицательные, как у VFS на машине (там все коды
 * ошибок отрицательны); стенд сравнивает их по модулю.
 */
int cdevfd_new(endpoint_t endpt, devminor_t minor, int flags, uint64_t size);
int cdevfd_get(endpoint_t endpt, int fd);
#define UNUSED(x)	x __attribute__((unused))
#define EDONTREPLY	9203	/* у MINIX - _SIGN 203 */
void *vm_map_phys(endpoint_t who, void *phaddr, size_t len);
endpoint_t sef_self(void);
int micro_delay(u32_t usec);

/* Память, кэш, часы и будильник - в модели. */
#define AC_ALIGN4K	0x01
#define VM_D		0x0102
void *alloc_contig(size_t len, int flags, phys_bytes *phys);
int free_contig(void *addr, size_t len);
int sys_cachectl(int op, void *addr, size_t len);
int sys_umap(endpoint_t proc_ep, int seg, vir_bytes vir_addr,
	vir_bytes bytes, phys_bytes *phys_addr);
void *vm_remap(endpoint_t d, endpoint_t s, void *da, void *sa, size_t si);
void *vm_remap_ro(endpoint_t d, endpoint_t s, void *da, void *sa, size_t si);
void read_frclock_64(u64_t *frclk);
u64_t delta_frclock_64(u64_t base, u64_t cur);
u32_t frclock_64_to_micros(u64_t tsc);
int sys_setalarm(long exp_time, int abs_time);
u32_t sys_hz(void);
int getuptime(clock_t *ticks, clock_t *realtime, time_t *boottime);

/* Гранты VFS и память вызывающего. */
int sys_safecopyfrom(endpoint_t src, cp_grant_id_t grant, vir_bytes off,
	vir_bytes addr, size_t len);
int sys_safecopyto(endpoint_t dst, cp_grant_id_t grant, vir_bytes off,
	vir_bytes addr, size_t len);
int sys_vircopy(endpoint_t src_proc, vir_bytes src_v, endpoint_t dst_proc,
	vir_bytes dst_v, phys_bytes bytes, int flags);
#endif
