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
typedef struct { int m_source; long m_type; long payload[14]; } message;

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


/*===========================================================================*
 *    what the interrupt-driven wait needs                                   *
 *===========================================================================*/
/*
 * The driver stopped polling for events and started waiting for the
 * interrupt, so the stand has to be able to be the kernel for it: deliver
 * a notification when the modelled controller raises its line, and an
 * alarm when the deadline passes.  Without this the stand could only ever
 * test the path the driver no longer takes.
 */
typedef int endpoint_t;

#define ANY		((endpoint_t)-1)
#define HARDWARE	((endpoint_t)-4)
#define CLOCK		((endpoint_t)-5)

#define _ENDPOINT_P(e)		(e)
#define is_ipc_notify(status)	((status) == 1)

int sef_receive_status(endpoint_t src, message *m, int *status);
int sys_setalarm(long ticks, int abs);
int sys_irqenable(int *hook);
long micros_to_ticks(unsigned long usec);

/* The free-running clock the driver measures itself with. */
void read_frclock_64(u64_t *t);
u64_t delta_frclock_64(u64_t base, u64_t now);
u64_t frclock_64_to_micros(u64_t delta);

#endif /* _SHIM_MINIX_DRIVERS_H */
