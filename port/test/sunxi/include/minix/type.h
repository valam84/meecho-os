/* Стенд sunxi: типы MINIX на голом ARMv7 (ILP32 - заодно проверка, что
 * драйвер не держится за ширину LP64). */
#ifndef _H_MINIX_TYPE_H
#define _H_MINIX_TYPE_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef uint64_t u64_t;
typedef uint32_t u32_t;
typedef unsigned long phys_bytes;
typedef unsigned long vir_bytes;
typedef int endpoint_t;
typedef int cp_grant_id_t;
#endif
