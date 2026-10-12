/* Стенд sunxi: всё в DRAM R40 с 0x40000000, стек - на 16 МиБ выше. */
ENTRY(_start)
SECTIONS
{
	. = 0x40000000;
	.text : { *(.text.start) *(.text*) }
	.ARM.exidx : { *(.ARM.exidx*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(8);
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	}
	. = ALIGN(16);
	end = .;
	__heap_start = .;
	__stack_top = 0x41000000;
}
