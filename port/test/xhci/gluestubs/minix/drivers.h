/*
 * xHCI glue stand: qcom_touch's <minix/drivers.h> stand-in, plus what the
 * driver's header and its glue take from the real one - a message (the
 * URB server's entry points are declared next to the glue's), the device
 * memory calls, the delay.  gluetest.c implements them.
 */
#ifndef _GLUE_MINIX_DRIVERS_H
#define _GLUE_MINIX_DRIVERS_H

#include_next <minix/drivers.h>

#include <sys/mman.h>

typedef struct { int m_source; long m_type; long payload[14]; } message;

#ifndef SELF
#define SELF		((endpoint_t)0x8ace)
#endif

void *vm_map_phys(endpoint_t who, void *phys, size_t len);
int vm_unmap_phys(endpoint_t who, void *vaddr, size_t len);

#endif /* _GLUE_MINIX_DRIVERS_H */
