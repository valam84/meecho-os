/*
 * Link script for the AArch64 kernel during bring-up.
 *
 * The load address is the base of RAM on the QEMU virt machine. On the
 * BCM2711 RAM starts at 0, and a loader (U-Boot) will place the image
 * wherever it likes, so this address becomes a per-board value once the
 * kernel runs from virtual addresses. Until the MMU is on, everything is
 * physical and the link address has to match where the image is loaded.
 */

OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* QEMU virt: RAM begins at 0x40000000. */
KERNEL_LOAD_ADDR = 0x40000000;

/*
 * Separate program headers per protection class.
 *
 * Without this the linker puts everything in one RWX segment and warns about
 * it. More to the point, once the MMU is enabled the page tables need to know
 * which range is code, which is read-only and which is writable; the symbols
 * below hand those boundaries to that code. Each region is page aligned
 * because page permissions have no finer granularity.
 */
PHDRS
{
	text	PT_LOAD FLAGS(5);	/* R E  */
	rodata	PT_LOAD FLAGS(4);	/* R    */
	data	PT_LOAD FLAGS(6);	/* RW   */
}

SECTIONS
{
	. = KERNEL_LOAD_ADDR;

	__kernel_start = .;

	__text_start = .;
	.text : {
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
	.rodata : {
		*(.rodata .rodata.*)
	} :rodata
	. = ALIGN(4096);
	__rodata_end = .;

	__data_start = .;
	.data : {
		*(.data .data.*)
	} :data

	. = ALIGN(4096);
	__bss_start = .;
	.bss : {
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
