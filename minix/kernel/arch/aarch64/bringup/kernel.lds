/*
 * Link script for the AArch64 kernel during bring-up.
 *
 * The kernel is linked at its virtual address in the upper half and loaded at
 * its physical one. Every output section therefore has two addresses: a VMA
 * the code is compiled against, and an LMA saying where the bytes actually go
 * in RAM.
 *
 * KERNEL_VA_OFFSET is the distance between them. It is not defined here: the
 * C sources need the same number, so Makefile.bringup passes it to both the
 * compiler and the linker and there is one place to change it.
 *
 * The entry point has to be the *physical* address of _start, because the
 * loader jumps there with the MMU still off and nothing yet maps the upper
 * half. e_entry is therefore taken from _start_phys below, not from _start.
 *
 * The load address is the base of RAM on the QEMU virt machine. On the
 * BCM2711 RAM starts at 0 and a loader (U-Boot) places the image where it
 * likes, so this becomes a per-board value - but the virtual address the
 * kernel runs at no longer has to move with it.
 */

OUTPUT_ARCH(aarch64)

/* QEMU virt: RAM begins at 0x40000000. */
KERNEL_PHYS_BASE = 0x40000000;
KERNEL_VIRT_BASE = KERNEL_PHYS_BASE + KERNEL_VA_OFFSET;

_start_phys = _start - KERNEL_VA_OFFSET;
ENTRY(_start_phys)

/*
 * Separate program headers per protection class.
 *
 * Without this the linker puts everything in one RWX segment and warns about
 * it. More to the point, the page tables need to know which range is code,
 * which is read-only and which is writable; the symbols below hand those
 * boundaries to mmu.c. Each region is page aligned because page permissions
 * have no finer granularity.
 */
PHDRS
{
	text	PT_LOAD FLAGS(5);	/* R E  */
	rodata	PT_LOAD FLAGS(4);	/* R    */
	data	PT_LOAD FLAGS(6);	/* RW   */
}

SECTIONS
{
	. = KERNEL_VIRT_BASE;

	__kernel_start = .;

	__text_start = .;
	.text : AT(ADDR(.text) - KERNEL_VA_OFFSET) {
		/*
		 * .text.boot must come first: the entry point has to be at
		 * the very start of the image, because a loader that does
		 * not read ELF headers will simply jump to the load address.
		 */
		KEEP(*(.text.boot))
		*(.text .text.*)
	} :text
	. = ALIGN(4096);
	__text_end = .;

	__rodata_start = .;
	.rodata : AT(ADDR(.rodata) - KERNEL_VA_OFFSET) {
		*(.rodata .rodata.*)
	} :rodata
	. = ALIGN(4096);
	__rodata_end = .;

	__data_start = .;
	.data : AT(ADDR(.data) - KERNEL_VA_OFFSET) {
		*(.data .data.*)
	} :data

	. = ALIGN(4096);
	__bss_start = .;
	.bss : AT(ADDR(.bss) - KERNEL_VA_OFFSET) {
		*(.bss.boot)
		*(.bss .bss.*)
		*(COMMON)
	} :data
	/*
	 * head.S clears the BSS eight bytes at a time without checking for a
	 * remainder, so the end must be a multiple of eight.
	 */
	. = ALIGN(8);
	__bss_end = .;

	. = ALIGN(4096);
	__kernel_end = .;

	/DISCARD/ : {
		*(.comment)
		*(.note.*)
		*(.eh_frame)
		*(.eh_frame_hdr)
	}
}
