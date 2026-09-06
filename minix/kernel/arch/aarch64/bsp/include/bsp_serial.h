#ifndef _BSP_SERIAL_H_
#define _BSP_SERIAL_H_

/*
 * The kernel's console, as the board support package provides it. Two calls,
 * the same two ARM has: bring the port up, and put one character out.
 *
 * This is the console the kernel talks on before there is a tty driver and
 * after a panic has taken one away, so bsp_ser_putc() has to work at any
 * moment and must not depend on anything but the port itself.
 *
 * bsp_ser_init() takes the device tree because on this architecture the
 * console is not a board constant: QEMU's virt machine has a PL011, the
 * RK3566 of the CB2 has a DesignWare 8250, and the tree is what says which it
 * is and where. The implementation left the board package for the same reason
 * the GIC did - see arch/aarch64/serial.c.
 */
void bsp_ser_init(phys_bytes dtb);
void bsp_ser_putc(char c);

#endif /* _BSP_SERIAL_H_ */
