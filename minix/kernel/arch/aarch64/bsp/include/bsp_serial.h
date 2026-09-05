#ifndef _BSP_SERIAL_H_
#define _BSP_SERIAL_H_

/*
 * The kernel's console, as the board support package provides it. Two calls,
 * the same two ARM has: bring the port up, and put one character out.
 *
 * This is the console the kernel talks on before there is a tty driver and
 * after a panic has taken one away, so bsp_ser_putc() has to work at any
 * moment and must not depend on anything but the port itself.
 */
void bsp_ser_init(void);
void bsp_ser_putc(char c);

#endif /* _BSP_SERIAL_H_ */
