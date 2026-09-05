/*
 * PL011 console for the QEMU virt machine.
 *
 * The register base is not a constant. The kernel talks on this port before
 * paging exists, when the address it needs is the physical one, and goes on
 * talking on it afterwards, when the address it needs is whatever VM chose
 * for the mapping. So the base lives in a variable, kern_phys_map_ptr()
 * registers the range and names that variable, and VM rewrites it through the
 * callback once the mapping is in place. bsp/ti does the same thing on ARM.
 *
 * The bring-up kernel in arch/aarch64/bringup carries the same driver over a
 * different mechanism: it has no VM to ask, so mmu_map_device() records the
 * range and mmu_activate_device_maps() rewrites the base itself. Here there
 * is a VM, so the arrangement the rest of MINIX already has is the one to
 * use.
 */

#include <assert.h>
#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/vm.h"
#include "arch_proto.h"

#include "bsp_serial.h"
#include "virt_registers.h"

static struct {
	vir_bytes base;
	vir_bytes size;
} virt_serial = {
	.base = 0,
};

static kern_phys_map serial_phys_map;

void
bsp_ser_init(void)
{
	virt_serial.base = VIRT_UART0_BASE;
	virt_serial.size = VIRT_UART0_SIZE;

	kern_phys_map_ptr(virt_serial.base, virt_serial.size,
	    VMMF_UNCACHED | VMMF_WRITE, &serial_phys_map,
	    (vir_bytes)&virt_serial.base);
	assert(virt_serial.base);

	/*
	 * Bring the UART down before touching its configuration, and wait for
	 * any character still on the wire, as the PL011 manual requires.
	 */
	mmio_write(virt_serial.base + PL011_CR, 0);
	while (mmio_read(virt_serial.base + PL011_FR) & PL011_FR_BUSY)
		;

	/* Clearing FEN flushes the FIFOs. */
	mmio_write(virt_serial.base + PL011_LCRH, 0);

	mmio_write(virt_serial.base + PL011_IMSC, 0);
	mmio_write(virt_serial.base + PL011_ICR, PL011_INT_ALL);

	/*
	 * QEMU ignores the baud rate divisors entirely, so there is nothing
	 * useful to program here. On the BCM2711 they matter and depend on the
	 * UART reference clock the firmware chose, which is why config.txt has
	 * to pin init_uart_clock.
	 */

	mmio_write(virt_serial.base + PL011_LCRH,
	    PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);
	mmio_write(virt_serial.base + PL011_CR,
	    PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void
bsp_ser_putc(char c)
{
	assert(virt_serial.base);

	/* Wait for room in the transmit FIFO. */
	while (mmio_read(virt_serial.base + PL011_FR) & PL011_FR_TXFF)
		;

	mmio_write(virt_serial.base + PL011_DR, (u32_t)(unsigned char)c);

	/*
	 * Drain before returning. This makes the console slow, but it is the
	 * only behaviour that reliably survives a panic, which is exactly when
	 * the output matters.
	 */
	while (mmio_read(virt_serial.base + PL011_FR) & PL011_FR_BUSY)
		;
}
