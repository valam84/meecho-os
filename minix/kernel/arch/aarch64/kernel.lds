/*
 * Link script for the AArch64 kernel, tree build.
 *
 * The kernel is linked at its virtual address in the upper half and loaded at
 * its physical one, so every output section has two addresses: a VMA the code
 * is compiled against and an LMA saying where the bytes go in RAM. The
 * distance between them is KERNEL_VA_OFFSET.
 *
 * That number is not written here. The C sources need the same value as a
 * compile-time constant - early boot computes physical addresses with it
 * before anything maps the upper half - so Makefile.inc defines it once and
 * hands it to both the compiler and the linker. A copy in a header and
 * another in this file would be free to drift, and the symptom of a drift is
 * a machine that dies with the MMU half on.
 *
 * The entry point is the *physical* address of _start: the loader jumps there
 * with the MMU off and nothing yet maps the upper half. e_entry therefore
 * comes from _start_phys, not from _start.
 *
 * Compared with the bring-up script this one adds what the generic kernel
 * wants: the usermapped sections, and the _kern_* symbols that the
 * architecture layer reads to describe itself to VM.
 */

OUTPUT_ARCH(aarch64)

/*
 * Where the image is loaded. 0x40000000 is the base of RAM on QEMU's virt
 * machine. On the BCM2711 RAM starts at 0 and U-Boot puts the image where it
 * likes, so this is a per-board value and will move into the board support
 * package; the virtual address the kernel runs at does not move with it.
 */
_kern_phys_base = 0x40000000;
_kern_vir_base = _kern_phys_base + KERNEL_VA_OFFSET;
_kern_offset = KERNEL_VA_OFFSET;

_start_phys = _start - KERNEL_VA_OFFSET;
ENTRY(_start_phys)

/*
 * One program header per protection class. Without this the linker puts
 * everything in one RWX segment and says so; more to the point, the page
 * tables need to know which range is code, which is read-only and which is
 * writable, and the symbols below are how those boundaries reach mmu code.
 * Each region is page aligned because page permissions have no finer grain.
 */
PHDRS
{
	text	PT_LOAD FLAGS(5);	/* R E  */
	rodata	PT_LOAD FLAGS(4);	/* R    */
	data	PT_LOAD FLAGS(6);	/* RW   */
}

SECTIONS
{
	. = _kern_vir_base;

	__kernel_start = .;

	__text_start = .;
	.text : AT(ADDR(.text) - KERNEL_VA_OFFSET) {
		/*
		 * .text.boot first: the entry point has to be at the very
		 * start of the image, because a loader that does not read ELF
		 * headers simply jumps to the load address.
		 */
		KEEP(*(.text.boot))
		*(.text .text.*)
	} :text
	. = ALIGN(4096);
	__text_end = .;
	_etext = .;

	__rodata_start = .;
	.rodata : AT(ADDR(.rodata) - KERNEL_VA_OFFSET) {
		*(.rodata .rodata.*)
	} :rodata
	. = ALIGN(4096);
	__rodata_end = .;

	/*
	 * Data the kernel shares with user processes: the kinfo structures,
	 * the clock information, the load average. VM asks the architecture
	 * layer for this range through arch_phys_map(), maps it into each
	 * process and reports back the address it chose.
	 *
	 * On this architecture the choice really is VM's. The kernel half of
	 * the address space is TTBR1 and EL0 cannot reach it at all, so a
	 * process sees these pages at an address of its own in the lower
	 * half, not at the kernel's. The two 32-bit ports, where one page
	 * table holds both halves, could and did use the same address for
	 * both; nothing in the interface required it.
	 *
	 * Page aligned and page granular because that is the unit VM maps.
	 */
	. = ALIGN(4096);
	usermapped_start = .;
	.usermapped_glo : AT(ADDR(.usermapped_glo) - KERNEL_VA_OFFSET) {
		*(.usermapped_glo)
	} :data
	. = ALIGN(4096);
	usermapped_nonglo_start = .;
	.usermapped : AT(ADDR(.usermapped) - KERNEL_VA_OFFSET) {
		*(.usermapped)
	} :data
	. = ALIGN(4096);
	usermapped_end = .;

	__data_start = .;
	.data : AT(ADDR(.data) - KERNEL_VA_OFFSET) {
		*(.data .data.*)
	} :data
	. = ALIGN(4096);
	_edata = .;

	__bss_start = .;
	.bss : AT(ADDR(.bss) - KERNEL_VA_OFFSET) {
		*(.bss.boot)
		*(.bss .bss.*)
		*(COMMON)
	} :data
	/*
	 * Early code clears the BSS eight bytes at a time without checking
	 * for a remainder, so the end must be a multiple of eight.
	 */
	. = ALIGN(8);
	__bss_end = .;

	. = ALIGN(4096);
	__kernel_end = .;
	_end = .;

	/* What the architecture layer reports to VM as the kernel's extent. */
	_kern_size = . - _kern_vir_base;

	/DISCARD/ : {
		*(.comment)
		*(.note.*)
		*(.eh_frame)
		*(.eh_frame_hdr)
	}
}
