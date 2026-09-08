/* Стенд: та часть libblockdriver, которую видит драйвер. */
#ifndef _STUB_MINIX_BLOCKDRIVER_H
#define _STUB_MINIX_BLOCKDRIVER_H
#include <minix/type.h>
#include <sys/types.h>

typedef int devminor_t;
typedef int endpoint_t;
typedef int cp_grant_id_t;
typedef int device_id_t;
typedef int thread_id_t;

/* Поле iov_addr — грант для чужого запроса и указатель для своего.
 * Ровно то, на чём ломается mmcblk.c под LP64. */
typedef struct { vir_bytes iov_addr; vir_bytes iov_size; } iovec_t;

struct part_geom { unsigned int cylinders, heads, sectors; };

#define NR_IOREQS 64
#define BLOCKDRIVER_TYPE_DISK 0

struct blockdriver {
	int bdr_type;
	int (*bdr_open)(devminor_t, int);
	int (*bdr_close)(devminor_t);
	ssize_t (*bdr_transfer)(devminor_t, int, u64_t, endpoint_t,
	    iovec_t *, unsigned int, int);
	int (*bdr_ioctl)(devminor_t, unsigned long, endpoint_t,
	    cp_grant_id_t, endpoint_t);
	struct device *(*bdr_part)(devminor_t);
	void (*bdr_geometry)(devminor_t, struct part_geom *);
	void (*bdr_intr)(unsigned int);
	void (*bdr_alarm)(clock_t);
	int (*bdr_device)(devminor_t, device_id_t *);
	int (*bdr_flush)(devminor_t);
	int (*bdr_discard)(devminor_t, u64_t, u64_t);
};

void blockdriver_announce(int type);
void blockdriver_task(struct blockdriver *bdp);
#endif
