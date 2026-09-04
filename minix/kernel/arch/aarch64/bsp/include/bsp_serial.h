#ifndef _BSP_SERIAL_H_
#define _BSP_SERIAL_H_

#include <stdint.h>

void bsp_ser_init(void);
void bsp_ser_putc(char c);

/*
 * Where the console registers physically are, and how much address space they
 * occupy. The MMU code needs this before it can map them. It is asked for
 * rather than exported as a constant so that board addresses stay in the
 * board's own file.
 */
void bsp_ser_phys_range(uint64_t *base, uint64_t *size);

/*
 * Point the console at a different address for the same registers. Called
 * once, after paging is enabled, with the virtual address the kernel mapped
 * them at - otherwise the console is lost the moment the identity map goes
 * away.
 */
void bsp_ser_set_base(uint64_t base);

#endif /* _BSP_SERIAL_H_ */
