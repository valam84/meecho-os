/*
 * First C code to run on AArch64, and the boot path across the MMU switch.
 *
 * head.S has put the CPU at EL1 with a stack and a zeroed BSS. There are no
 * exception vectors, no timer and no libc. Everything used here has to be
 * written here or in the BSP.
 *
 * Boot runs in two worlds. kernel_early_main() executes from physical
 * addresses with the MMU off; kernel_main() executes from the upper half with
 * it on. The point of this stage is the transition between them, so both ends
 * report enough to tell which world the kernel is actually in.
 */

#include <stdint.h>

#include "bsp_serial.h"
#include "kprint.h"
#include "mmu.h"

/* Provided by the link script. */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __rodata_start[];
extern char __bss_start[];
extern char __bss_end[];

/*
 * The device tree pointer, handed over in a global rather than an argument
 * because the branch into the upper half does not carry a call frame.
 *
 * It survives the switch because the two addresses name the same physical
 * memory - which is the first thing worth proving about the new map.
 */
static uint64_t boot_dtb;

void kernel_early_main(uint64_t dtb);
void kernel_main(void);

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

/* Where this instruction actually is, as opposed to where it was linked. */
static uint64_t
read_pc(void)
{
	uint64_t pc;

	__asm__ volatile("adr %0, ." : "=r"(pc));
	return pc;
}

static uint64_t
read_sp(void)
{
	uint64_t sp;

	__asm__ volatile("mov %0, sp" : "=r"(sp));
	return sp;
}

/*
 * Report one mapping as the hardware sees it: where it points, and which of
 * the accesses we meant to allow it actually allows.
 *
 * Execute permission has no AT operation to ask with, so it is not in the
 * table. That .text is executable at EL1 is demonstrated by this code
 * running; that the rest is not will be demonstrated at stage 2.4, when there
 * are exception vectors to catch it.
 */
static void
report_mapping(const char *label, uint64_t va)
{
	uint64_t pa;

	kputs(label);
	kput_hex(va);
	kputs(" -> ");

	if (!mmu_probe(va, MMU_ACCESS_EL1_READ, &pa)) {
		kputs("fault\n");
		return;
	}
	kput_hex(pa);

	kputs("  EL1 ");
	kputs("r");
	kputs(mmu_probe(va, MMU_ACCESS_EL1_WRITE, &pa) ? "w" : "-");
	kputs("  EL0 ");
	kputs(mmu_probe(va, MMU_ACCESS_EL0_READ, &pa) ? "r" : "-");
	kputs("\n");
}

void
kernel_early_main(uint64_t dtb)
{
	bsp_ser_init();
	boot_dtb = dtb;

	kputs("\n");
	kputs("MINIX/aarch64 early boot\n");
	kputs("------------------------\n");

	/*
	 * If this says 1, the drop from EL2 in head.S worked. If it says 2,
	 * it silently did not, and everything downstream that touches EL1
	 * system registers will behave strangely.
	 */
	kput_line("CurrentEL   : ", read_current_el());
	kput_line("MPIDR_EL1   : ", read_mpidr());
	kput_line("MIDR_EL1    : ", read_midr());
	kput_line("DTB         : ", dtb);

	/*
	 * These are the same expressions kernel_main() prints after the
	 * switch. Here they resolve to physical addresses, there to virtual
	 * ones; the difference between the two listings is the entire result
	 * of this stage.
	 */
	kput_line("PC          : ", read_pc());
	kput_line("SP          : ", read_sp());
	kput_line("kernel start: ", (uint64_t)__kernel_start);
	kput_line("kernel end  : ", (uint64_t)__kernel_end);
	kput_line("bss start   : ", (uint64_t)__bss_start);
	kput_line("bss end     : ", (uint64_t)__bss_end);

	kputs("\nbuilding translation tables\n");
	mmu_setup();

	/*
	 * The MMU is on. This still prints through the identity map and the
	 * physical console alias, both of which are about to go away.
	 */
	kputs("MMU on, running on the identity map\n");

	mmu_switch_high(kernel_main);
}

void
kernel_main(void)
{
	uint64_t uart_base, uart_size;

	/*
	 * Move the console to its kernel mapping before the identity map
	 * disappears. Until this returns, output still goes through the
	 * physical alias.
	 */
	bsp_ser_phys_range(&uart_base, &uart_size);
	bsp_ser_set_base(phys_to_virt(uart_base));

	kputs("\n");
	kputs("running in the upper half\n");
	kputs("-------------------------\n");
	kput_line("PC          : ", read_pc());
	kput_line("SP          : ", read_sp());
	kput_line("kernel start: ", (uint64_t)__kernel_start);
	kput_line("kernel end  : ", (uint64_t)__kernel_end);

	/* Read back through the new map what the old world wrote. */
	kput_line("DTB (kept)  : ", boot_dtb);

	kputs("\n");
	mmu_report();

	/*
	 * The permissions the link script's section boundaries were supposed
	 * to buy: code readable and not writable, read-only data likewise,
	 * writable data writable, and none of it reachable from EL0.
	 */
	kputs("\n");
	report_mapping(".text       : ", (uint64_t)__text_start);
	report_mapping(".rodata     : ", (uint64_t)__rodata_start);
	report_mapping(".bss        : ", (uint64_t)__bss_start);
	report_mapping("console     : ", phys_to_virt(uart_base));

	/*
	 * Take the identity map away. If anything above still depended on a
	 * physical address, the next few instructions are where it shows -
	 * and until stage 2.4 provides exception vectors, "shows" means the
	 * console stops here.
	 */
	kputs("\ndropping the identity map\n");
	mmu_drop_identity();

	kputs("identity map gone, kernel runs on TTBR1 alone\n");
	report_mapping("identity    : ", virt_to_phys((uint64_t)__kernel_start));

	kputs("\nboot reached the upper half, halting.\n");

	/*
	 * Nothing to go on to yet. Halt in a way that leaves the emulator
	 * idle rather than spinning a host core at 100%.
	 */
	for (;;)
		__asm__ volatile("wfi");
}
