/*
 * PL011 UART0 driver for the in-kernel debug console on BCM2711.
 *
 * Modelled on bsp/ti/omap_serial.c. Read the comment there first: this file
 * is compiled TWICE into the kernel, once normally and once with every symbol
 * prefixed by __k_unpaged_ (see BSP_OBJS_UNPAGED in Makefile.inc). The unpaged
 * copy runs in pre_init, before the MMU is on, with a 1:1 mapping. The paged
 * copy gets its base address rewritten by VM through kern_phys_map_ptr.
 *
 * Consequence: nothing here may depend on kernel state that is not yet
 * initialised, and no static data may be shared between the two copies.
 */

#include <assert.h>
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"

#include "bcm2711_registers.h"

struct bcm2711_serial
{
	vir_bytes base;
	vir_bytes size;
};

static struct bcm2711_serial bcm2711_serial = {
	.base = 0,
};

static kern_phys_map serial_phys_map;

/*
 * Program the baud rate divisors.
 *
 * PL011 divisor = UARTCLK / (16 * baud), split into a 16-bit integer part
 * and a 6-bit fractional part. We compute both from a 64x scaled value so
 * the fraction rounds correctly without floating point.
 */
static void
bcm2711_ser_set_baud(unsigned int baud)
{
	unsigned int div64;

	if (baud == 0)
		return;

	/* (UARTCLK * 4) / baud == 64 * UARTCLK / (16 * baud) */
	div64 = (BCM2711_UART0_CLOCK * 4) / baud;

	mmio_write(bcm2711_serial.base + PL011_IBRD, div64 >> 6);
	mmio_write(bcm2711_serial.base + PL011_FBRD, div64 & 0x3f);
}

void
bsp_ser_init(void)
{
	bcm2711_serial.base = BCM2711_UART0_BASE;
	bcm2711_serial.size = BCM2711_UART0_SIZE;

	kern_phys_map_ptr(bcm2711_serial.base, bcm2711_serial.size,
	    VMMF_UNCACHED | VMMF_WRITE, &serial_phys_map,
	    (vir_bytes) & bcm2711_serial.base);
	assert(bcm2711_serial.base);

	/*
	 * U-Boot has normally already configured the UART for us. We
	 * reprogram it anyway so that the console works even when the kernel
	 * is started by something else, and so that the settings are known.
	 *
	 * The sequence is the one required by the PL011 TRM: disable the
	 * UART, wait for the current character to drain, flush the FIFO by
	 * clearing FEN, then reprogram and re-enable.
	 */
	mmio_write(bcm2711_serial.base + PL011_CR, 0);

	while (mmio_read(bcm2711_serial.base + PL011_FR) & PL011_FR_BUSY)
		;

	mmio_clear(bcm2711_serial.base + PL011_LCRH, PL011_LCRH_FEN);

	/* Mask and acknowledge every interrupt source. */
	mmio_write(bcm2711_serial.base + PL011_IMSC, 0);
	mmio_write(bcm2711_serial.base + PL011_ICR, PL011_INT_ALL);

	bcm2711_ser_set_baud(115200);

	/* 8 data bits, no parity, 1 stop bit, FIFOs enabled. */
	mmio_write(bcm2711_serial.base + PL011_LCRH,
	    PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);

	mmio_write(bcm2711_serial.base + PL011_CR,
	    PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void
bsp_ser_putc(char c)
{
	int i;

	assert(bcm2711_serial.base);

	/* Wait for room in the transmit FIFO. */
	for (i = 0; i < 100000; i++) {
		if (!(mmio_read(bcm2711_serial.base + PL011_FR) &
			PL011_FR_TXFF)) {
			break;
		}
	}

	mmio_write(bcm2711_serial.base + PL011_DR, c);

	/*
	 * Drain before returning. The OMAP driver does the same, to stop TTY
	 * from overwriting output that has not left the FIFO yet. This makes
	 * the console slow but it is the only thing that reliably survives a
	 * panic, which is exactly when we need it.
	 */
	for (i = 0; i < 100000; i++) {
		if (!(mmio_read(bcm2711_serial.base + PL011_FR) &
			PL011_FR_BUSY)) {
			break;
		}
	}
}
