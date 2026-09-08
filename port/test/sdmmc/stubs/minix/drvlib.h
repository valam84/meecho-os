/* Стенд: константы миноров те же, что в minix/include/minix/drvlib.h. */
#ifndef _STUB_MINIX_DRVLIB_H
#define _STUB_MINIX_DRVLIB_H
#include <minix/type.h>
#include <minix/blockdriver.h>

#define NR_PARTITIONS	4
#define DEV_PER_DRIVE	(1 + NR_PARTITIONS)
#define SUB_PER_DRIVE	(NR_PARTITIONS * NR_PARTITIONS)
#define MINOR_d0p0s0	128
#define P_PRIMARY	1

struct device { u64_t dv_base; u64_t dv_size; };

void partition(struct blockdriver *bdp, int device, int style, int atapi);
#endif
