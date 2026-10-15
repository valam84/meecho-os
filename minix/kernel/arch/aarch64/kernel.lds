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
 * Where the image is loaded.
 *
 * RAM on QEMU's virt machine starts at 0x40000000, but the image does not go
 * there. A loader placing an arm64 image puts it at the 2 MiB-aligned base of
 * RAM plus the text_offset in the image header, and it rounds up: with a
 * text_offset of zero QEMU lands on 0x40200000, two megabytes in. That was
 * measured, not assumed - the first flat boot ran with the linker believing
 * 0x40000000 and the image actually at 0x40200000, and it survived long
 * enough to print its memory map only because early boot is PC-relative. The
 * first absolute address taken from the link script faulted.
 *
 * So the number here is the one the loader actually uses, and head.S declares
 * the matching text_offset.
 *
 * It is the address the image is *linked* for, not one it must run at. A
 * Qualcomm machine has no RAM at 0x40200000 at all - it starts at
 * 0x80000000 - and its loader puts the image where it likes. So the kernel
 * is linked position-independent (-pie, Makefile.inc), and head.S moves it
 * to wherever it was loaded before any C runs: it adds the distance between
 * the two addresses to every slot listed in .rela.dyn below. Early boot is
 * PC-relative throughout, which is why that table has a handful of entries
 * rather than thousands. The kernel still runs at physical address plus
 * KERNEL_VA_OFFSET; it is the physical address that is free to move, and the
 * virtual one follows it.
 */
_kern_phys_base = KERNEL_LINK_BASE;	/* 0x40200000, from Makefile.inc */
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
		KEEP(*(.text.head0))
		/*
		 * image_size, the third field of the arm64 header (head.S). It
		 * is written here because data the link script writes itself is
		 * never relocated, and a .quad of a symbol under -pie always is.
		 * 64 KiB more than the image: head.S may copy itself up by as
		 * much to reach a 64 KiB boundary, and the loader has to leave
		 * that room free.
		 */
		QUAD(__kernel_end - __kernel_start + 0x10000)
		KEEP(*(.text.head1))
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

	/*
	 * What -pie adds. Only .rela.dyn is ever read - by head.S, between the
	 * two symbols, with the MMU off - and Makefile.inc checks after the link
	 * that it holds nothing but R_AARCH64_RELATIVE. The rest is the linker's
	 * bookkeeping for a dynamic object, named here so that each piece lands
	 * in the segment with the right protection rather than wherever orphan
	 * placement would put it.
	 */
	.dynsym : AT(ADDR(.dynsym) - KERNEL_VA_OFFSET) { *(.dynsym) } :rodata
	.dynstr : AT(ADDR(.dynstr) - KERNEL_VA_OFFSET) { *(.dynstr) } :rodata
	.hash : AT(ADDR(.hash) - KERNEL_VA_OFFSET) { *(.hash) } :rodata
	.gnu.hash : AT(ADDR(.gnu.hash) - KERNEL_VA_OFFSET) { *(.gnu.hash) } :rodata
	.rela.dyn : AT(ADDR(.rela.dyn) - KERNEL_VA_OFFSET) {
		__rela_dyn_start = .;
		*(.rela.dyn .rela.*)
		__rela_dyn_end = .;
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
	.dynamic : AT(ADDR(.dynamic) - KERNEL_VA_OFFSET) { *(.dynamic) } :data
	.got : AT(ADDR(.got) - KERNEL_VA_OFFSET) { *(.got .got.plt) } :data
	.init_array : AT(ADDR(.init_array) - KERNEL_VA_OFFSET) {
		*(.init_array .init_array.*)
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
