#ifndef _SHIM_MINIX_DRIVERS_H
#define _SHIM_MINIX_DRIVERS_H

/*
 * Enough of <minix/drivers.h> for the ring arithmetic of the xHCI driver
 * to compile on the host, and no more.
 *
 * The rule this file follows is the one port/test/cachectl uses: nothing
 * here may be a second implementation of anything the driver relies on
 * being true.  Types, error numbers and the two allocator calls are
 * declarations; what they DO is in model.c, where it can be watched.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/*
 * A message, only so that the driver's own header - which declares the
 * URB server's entry point - compiles.  The stand never sends one: the URB
 * protocol is the layer above the rings and belongs to a different kind
 * of check.
 */
typedef struct { long m_type; long payload[15]; } message;

typedef unsigned long vir_bytes;
typedef unsigned long phys_bytes;
typedef uint32_t u32_t;
typedef unsigned long u64_t;

#define OK		0
#define EIO		5
#define ENOMEM		12
#define EINVAL		22
#define ENODEV		19
#define ETIMEDOUT	60

/*
 * alloc_contig(3) as the driver uses it: page-aligned memory that has a
 * physical address.  On the host that address is invented by the model,
 * which is the only party that ever dereferences it.
 */
#define AC_ALIGN4K	0x02

void *alloc_contig(size_t size, int flags, phys_bytes *phys);
void free_contig(void *addr, size_t size);

/*
 * The driver's only way of letting time pass.  Here it is the handle the
 * stand turns: every call is one step of the modelled controller, so a
 * poll loop in the driver is what makes the machine advance, exactly as
 * on the board where the same loop is what lets the controller run.
 */
int micro_delay(u32_t usec);

#endif /* _SHIM_MINIX_DRIVERS_H */
