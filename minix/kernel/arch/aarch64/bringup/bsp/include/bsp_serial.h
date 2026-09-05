#ifndef _BSP_SERIAL_H_
#define _BSP_SERIAL_H_

/*
 * The console registers its own address range with mmu_map_device(), so
 * nothing outside the BSP needs to know where they are or when they move.
 */
void bsp_ser_init(void);
void bsp_ser_putc(char c);

#endif /* _BSP_SERIAL_H_ */
