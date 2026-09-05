#ifndef _AARCH64_IO_H_
#define _AARCH64_IO_H_

#ifndef __ASSEMBLY__

#include <sys/types.h>

/*
 * Access to memory-mapped device registers.
 *
 * A base address is carried as an integer, not a pointer, because that is
 * what a base address is here: it changes when VM remaps the range, and the
 * arithmetic that goes with the register offsets reads better on an integer.
 * The volatile is what stops the compiler from folding two reads of a status
 * register into one.
 *
 * Register widths are 32 bits. Every device the kernel touches on this
 * architecture has 32-bit registers - the PL011 and the GIC both do - and a
 * wider access to one of them is a fault, not a slow path, so there is no
 * general accessor here waiting to be used by accident.
 */
#define mmio_read(a)	(*(volatile u32_t *)(a))
#define mmio_write(a,v)	(*(volatile u32_t *)(a) = (v))
#define mmio_set(a,v)	mmio_write((a), mmio_read((a)) | (v))
#define mmio_clear(a,v)	mmio_write((a), mmio_read((a)) & ~(v))

#endif /* __ASSEMBLY__ */

#endif /* _AARCH64_IO_H_ */
