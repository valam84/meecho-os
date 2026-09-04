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
#include "trap.h"

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

static uint64_t
read_vbar(void)
{
	uint64_t vbar;

	__asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
	return vbar;
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

/*
 * An address in the kernel half that nothing maps. Well clear of the linear
 * map, which only ever needs the first 16 TiB of it.
 */
#define UNMAPPED_ADDR	0xffff800000000000UL

/* Somewhere writable to aim the control case at. */
static volatile uint32_t writable_probe;

/*
 * Try one access and say what the hardware did with it.
 *
 * This is what stage 2.3 could not do. AT S1E1W reports what the tables say;
 * this performs the access, so it reports what the CPU does - and it needs
 * exception vectors, because the answer arrives as a data abort.
 */
static void
check_access(const char *label, uint64_t va, int write)
{
	uint64_t esr, far;

	kputs(label);

	trap_expect_fault();
	if (write)
		*(volatile uint32_t *)va = 0xdeadbeefU;
	else
		writable_probe = *(volatile uint32_t *)va;

	if (!trap_took_fault(&esr, &far)) {
		kputs("succeeded\n");
		return;
	}

	kputs("data abort, ESR ");
	kput_hex(esr);
	kputs(" FAR ");
	kput_hex(far);
	kputs("\n");
}

static void
check_permissions(void)
{
	kputs("\nwhat the mappings actually refuse\n");
	kputs("---------------------------------\n");

	/*
	 * __text_start is the first instruction of head.S, which has already
	 * run and will not run again - so if this write were to succeed
	 * despite everything, it would corrupt nothing that matters.
	 */
	check_access("write .text        : ", (uint64_t)__text_start, 1);
	check_access("write .rodata      : ", (uint64_t)__rodata_start, 1);
	check_access("write .bss         : ", (uint64_t)&writable_probe, 1);
	check_access("read  unmapped     : ", UNMAPPED_ADDR, 0);
}

void
kernel_early_main(uint64_t dtb)
{
	bsp_ser_init();
	boot_dtb = dtb;

	/*
	 * Vectors before anything else that can go wrong. The table's address
	 * is physical here, which is exactly what is needed: a mistake in the
	 * page tables below faults with the MMU still off, and without
	 * vectors that fault is a machine that stops without a word.
	 */
	trap_init();

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
	kput_line("VBAR_EL1    : ", read_vbar());
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

	/*
	 * Same call as in early boot, different answer: the vector table's
	 * address is now the kernel-virtual one. VBAR_EL1 still holds the
	 * physical address until this runs, and that mapping is about to be
	 * taken away.
	 */
	trap_init();

	kputs("\n");
	kputs("running in the upper half\n");
	kputs("-------------------------\n");
	kput_line("PC          : ", read_pc());
	kput_line("SP          : ", read_sp());
	kput_line("VBAR_EL1    : ", read_vbar());
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

	check_permissions();

	kputs("\nboot reached the upper half.\n");

	/*
	 * Deliberate, and the last thing this stage does: take a fault nobody
	 * is expecting, so the report the kernel produces for a real one can
	 * be seen. It goes away at stage 2.5, when there is something to do
	 * after boot instead.
	 */
	kputs("\ntaking an unhandled fault on purpose\n");
	*(volatile uint64_t *)UNMAPPED_ADDR = 0;

	/*
	 * Not reached: trap_handler() halts. If this line ever runs, the
	 * fault above was handled by something that should not have handled
	 * it.
	 */
	kputs("the fault was swallowed - that is a bug\n");
	for (;;)
		__asm__ volatile("wfi");
}
