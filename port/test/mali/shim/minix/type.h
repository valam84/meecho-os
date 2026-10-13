/* Стенд mali: типы MINIX, которыми пользуются mali.h и <minix/fdt.h>. */
#ifndef _SHIM_MINIX_TYPE_H
#define _SHIM_MINIX_TYPE_H
#include <stdint.h>
#include <stddef.h>
typedef uint64_t u64_t;
typedef uint32_t u32_t;
typedef unsigned long phys_bytes;
typedef unsigned long vir_bytes;
typedef int endpoint_t;
typedef int32_t cp_grant_id_t;
typedef int devminor_t;
#endif
