/* Host stand: the types the service and the device tree reader use. */
#ifndef _STUB_MINIX_TYPE_H
#define _STUB_MINIX_TYPE_H
#include <stdint.h>
#include <stddef.h>
#include <time.h>
typedef uint64_t u64_t;
typedef uint32_t u32_t;
typedef uint16_t u16_t;
typedef uint8_t u8_t;
typedef int64_t i64_t;
typedef unsigned long phys_bytes;
typedef unsigned long vir_bytes;
typedef int endpoint_t;
/* The render node's (msmtest.c): grants and minors. */
typedef int32_t cp_grant_id_t;
typedef int devminor_t;
#endif
