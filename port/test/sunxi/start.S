/*
 * Стенд sunxi: вход на голом Cortex-A7 машины QEMU "bpim2u" (Allwinner R40).
 * QEMU кладёт ELF по его адресам и прыгает на точку входа; стек и .bss -
 * наши, дерево устройств стенда - прямо в образе (.incbin).
 */
	.syntax unified
	.arm

	.section .text.start, "ax"
	.global	_start
_start:
	cpsid	if
	ldr	sp, =__stack_top
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b
	bl	main
	b	harness_exit

/*
 * Семихостинг ARM: SVC 0x123456, r0 - операция, r1 - её параметр. Только
 * SYS_EXIT (0x18): ApplicationExit (0x20026) - QEMU выходит с кодом 0,
 * RunTimeErrorUnknown (0x20023) - с кодом 1.
 */
	.global	harness_semihost
harness_semihost:
	svc	0x123456
	bx	lr

	.section .rodata.dtb, "a"
	.balign	8
	.global	harness_dtb
harness_dtb:
	.incbin	"qemu-r40.dtb"
	.global	harness_dtb_end
harness_dtb_end:
