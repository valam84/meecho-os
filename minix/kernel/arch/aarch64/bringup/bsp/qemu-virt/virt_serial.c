/*
 * PL011 console for the QEMU virt machine.
 *
 * The same peripheral sits on the BCM2711 at a different address, so this
 * driver carries over to real hardware nearly unchanged; only the base moves.
 * That is the reason virt is the first bring-up target: the device set is the
 * one the Raspberry Pi actually has, without the gaps in QEMU's model of that
 * board.
 *
 * The register base is registered with mmu_map_device() and lives in a
 * variable, so one copy of the code serves both sides of the MMU switch:
 * physical addresses during boot, the kernel mapping of the same registers
 * afterwards. On earm bsp/ti does the same through kern_phys_map_ptr(), which
 * has VM rewrite the base once paging is up.
 */

#include <stdint.h>

#include "bsp_serial.h"
#include "mmio.h"
#include "mmu.h"

#include "virt_registers.h"

static uint64_t uart_base;

void
bsp_ser_init(void)
{
	mmu_map_device(VIRT_UART0_BASE, VIRT_UART0_SIZE, &uart_base);

	/*
	 * Bring the UART down before touching its configuration, and wait
	 * for any character still on the wire, as the PL011 manual requires.
	 */
	mmio_write(uart_base + PL011_CR, 0);
	while (mmio_read(uart_base + PL011_FR) & PL011_FR_BUSY)
		;

	/* Clearing FEN flushes the FIFOs. */
	mmio_write(uart_base + PL011_LCRH, 0);

	mmio_write(uart_base + PL011_IMSC, 0);
	mmio_write(uart_base + PL011_ICR, PL011_INT_ALL);

	/*
	 * QEMU ignores the baud rate divisors entirely, so there is nothing
	 * useful to program here. On the BCM2711 they matter and depend on
	 * the UART reference clock the firmware chose, which is why
	 * config.txt has to pin init_uart_clock.
	 */

	mmio_write(uart_base + PL011_LCRH, PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);
	mmio_write(uart_base + PL011_CR,
	    PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void
bsp_ser_putc(char c)
{
	/* Wait for room in the transmit FIFO. */
	while (mmio_read(uart_base + PL011_FR) & PL011_FR_TXFF)
		;

	mmio_write(uart_base + PL011_DR, (uint32_t)(unsigned char)c);

	/*
	 * Drain before returning. This makes the console slow, but it is the
	 * only behaviour that reliably survives a panic, which is exactly
	 * when the output matters.
	 */
	while (mmio_read(uart_base + PL011_FR) & PL011_FR_BUSY)
		;
}
