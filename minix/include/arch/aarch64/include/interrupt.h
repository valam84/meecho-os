/* Interrupt numbers and hardware vectors. */

#ifndef _INTERRUPT_H
#define _INTERRUPT_H

/*
 * IRQ numbers are GIC interrupt IDs: 0-15 SGI, 16-31 PPI, 32 and up SPI.
 * The GIC-400 on the BCM2711 supports up to 480 SPIs, which puts its
 * highest interrupt ID at 511; QEMU's virt machine uses far fewer. The
 * kernel's tables are sized by this, so it is the ceiling of the
 * controller rather than the count of any one board.
 */
#define NR_IRQ_VECTORS    512

#endif /* _INTERRUPT_H */
