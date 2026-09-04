/*
 * First C code to run on AArch64.
 *
 * head.S has put the CPU at EL1 with a stack and a zeroed BSS. There is no
 * MMU, no exception vectors, no timer and no libc. Everything this file uses
 * has to be written here or in the BSP.
 *
 * The point of this stage is to prove the boot path works and to report
 * enough state to tell what the firmware actually handed us, which is rarely
 * what the documentation claims.
 */

#include <stdint.h>

#include "bsp_serial.h"

/* Provided by the link script. */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

static void
puts(const char *s)
{
	while (*s != '\0') {
		if (*s == '\n')
			bsp_ser_putc('\r');
		bsp_ser_putc(*s++);
	}
}

/*
 * There is no printf yet and this is not the place to write one: the real
 * kernel gets NetBSD's subr_prf once the generic kernel is linked in. Hex is
 * enough to report addresses and register values, which is all early boot
 * has to say.
 */
static void
put_hex(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	int shift;

	puts("0x");
	for (shift = 60; shift >= 0; shift -= 4)
		bsp_ser_putc(digits[(value >> shift) & 0xf]);
}

static void
put_line(const char *label, uint64_t value)
{
	puts(label);
	put_hex(value);
	puts("\n");
}

static uint64_t
read_current_el(void)
{
	uint64_t el;

	__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
	return el >> 2;
}

static uint64_t
read_mpidr(void)
{
	uint64_t mpidr;

	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
	return mpidr;
}

static uint64_t
read_midr(void)
{
	uint64_t midr;

	__asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
	return midr;
}

void kernel_early_main(uint64_t dtb);

void
kernel_early_main(uint64_t dtb)
{
	bsp_ser_init();

	puts("\n");
	puts("MINIX/aarch64 early boot\n");
	puts("------------------------\n");

	/*
	 * If this says 1, the drop from EL2 in head.S worked. If it says 2,
	 * it silently did not, and everything downstream that touches EL1
	 * system registers will behave strangely.
	 */
	put_line("CurrentEL   : ", read_current_el());
	put_line("MPIDR_EL1   : ", read_mpidr());
	put_line("MIDR_EL1    : ", read_midr());
	put_line("DTB         : ", dtb);
	put_line("kernel start: ", (uint64_t)__kernel_start);
	put_line("kernel end  : ", (uint64_t)__kernel_end);
	put_line("bss start   : ", (uint64_t)__bss_start);
	put_line("bss end     : ", (uint64_t)__bss_end);

	puts("\nboot reached C, halting.\n");

	/*
	 * Nothing to go on to yet. Halt in a way that leaves the emulator
	 * idle rather than spinning a host core at 100%.
	 */
	for (;;)
		__asm__ volatile("wfi");
}
