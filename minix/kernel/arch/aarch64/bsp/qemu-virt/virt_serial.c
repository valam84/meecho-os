/*
 * PL011 console for the QEMU virt machine.
 *
 * The same peripheral sits on the BCM2711 at a different address, so this
 * driver carries over to real hardware nearly unchanged; only the base moves.
 * That is the reason virt is the first bring-up target: the device set is the
 * one the Raspberry Pi actually has, without the gaps in QEMU's model of that
 * board.
 *
 * The register base lives in a variable rather than a constant so that one
 * copy of the code serves both sides of the MMU switch: physical addresses
 * during boot, the kernel mapping of the same registers afterwards. The
 * kernel moves it with bsp_ser_set_base(). On earm bsp/ti does the same job
 * through kern_phys_map_ptr(), which has VM rewrite the base once paging is
 * up; this is the reduced form of that, with no VM to ask.
 */

#include <stdint.h>

#include "bsp_serial.h"

/* QEMU virt places the first PL011 here; see hw/arm/virt.c, VIRT_UART. */
#define VIRT_UART0_BASE		0x09000000UL

/* PL011 register offsets. */
#define PL011_DR		0x000	/* data */
#define PL011_FR		0x018	/* flag */
#define PL011_IBRD		0x024	/* integer baud rate divisor */
#define PL011_FBRD		0x028	/* fractional baud rate divisor */
#define PL011_LCRH		0x02c	/* line control */
#define PL011_CR		0x030	/* control */
#define PL011_IMSC		0x038	/* interrupt mask set/clear */
#define PL011_ICR		0x044	/* interrupt clear */

/* PL011_FR bits. */
#define PL011_FR_BUSY		(1 << 3)
#define PL011_FR_TXFF		(1 << 5)	/* transmit FIFO full */

/* PL011_LCRH bits. */
#define PL011_LCRH_FEN		(1 << 4)	/* enable FIFOs */
#define PL011_LCRH_WLEN_8	(3 << 5)

/* PL011_CR bits. */
#define PL011_CR_UARTEN		(1 << 0)
#define PL011_CR_TXE		(1 << 8)
#define PL011_CR_RXE		(1 << 9)

#define PL011_INT_ALL		0x7ff

static volatile uint32_t *uart_base;

static inline void
uart_write(unsigned int reg, uint32_t value)
{
	uart_base[reg / 4] = value;
}

static inline uint32_t
uart_read(unsigned int reg)
{
	return uart_base[reg / 4];
}

void
bsp_ser_init(void)
{
	uart_base = (volatile uint32_t *)VIRT_UART0_BASE;

	/*
	 * Bring the UART down before touching its configuration, and wait
	 * for any character still on the wire, as the PL011 manual requires.
	 */
	uart_write(PL011_CR, 0);
	while (uart_read(PL011_FR) & PL011_FR_BUSY)
		;

	/* Clearing FEN flushes the FIFOs. */
	uart_write(PL011_LCRH, 0);

	uart_write(PL011_IMSC, 0);
	uart_write(PL011_ICR, PL011_INT_ALL);

	/*
	 * QEMU ignores the baud rate divisors entirely, so there is nothing
	 * useful to program here. On the BCM2711 they matter and depend on
	 * the UART reference clock the firmware chose, which is why
	 * config.txt has to pin init_uart_clock.
	 */

	uart_write(PL011_LCRH, PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);
	uart_write(PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void
bsp_ser_phys_range(uint64_t *base, uint64_t *size)
{
	*base = VIRT_UART0_BASE;

	/*
	 * The PL011 register file is well under a page, but a page is the
	 * finest granularity a mapping has, so that is what has to be asked
	 * for. Nothing else lives in this page on virt.
	 */
	*size = 0x1000;
}

void
bsp_ser_set_base(uint64_t base)
{
	uart_base = (volatile uint32_t *)base;
}

void
bsp_ser_putc(char c)
{
	/* Wait for room in the transmit FIFO. */
	while (uart_read(PL011_FR) & PL011_FR_TXFF)
		;

	uart_write(PL011_DR, (uint32_t)(unsigned char)c);

	/*
	 * Drain before returning. This makes the console slow, but it is the
	 * only behaviour that reliably survives a panic, which is exactly
	 * when the output matters.
	 */
	while (uart_read(PL011_FR) & PL011_FR_BUSY)
		;
}
