/*
 * Memory-mapped register access.
 *
 * Stands in for the tree's <io.h>, which the bring-up kernel cannot include
 * because it comes with the rest of the MINIX headers. The names are the ones
 * bsp/ti and the BCM2711 BSP already use, so drivers written against either
 * read the same here.
 *
 * A base address is carried as an integer rather than a pointer because that
 * is what the BSPs do: the address changes when the MMU comes on, and an
 * integer makes the arithmetic that goes with the register offsets obvious.
 */

#ifndef _BSP_MMIO_H_
#define _BSP_MMIO_H_

#include <stdint.h>

static inline uint32_t
mmio_read(uint64_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline void
mmio_write(uint64_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static inline void
mmio_set(uint64_t addr, uint32_t bits)
{
	mmio_write(addr, mmio_read(addr) | bits);
}

static inline void
mmio_clear(uint64_t addr, uint32_t bits)
{
	mmio_write(addr, mmio_read(addr) & ~bits);
}

#endif /* _BSP_MMIO_H_ */
