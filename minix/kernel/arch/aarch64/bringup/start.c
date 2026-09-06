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

#include "bench.h"
#include "bsp_intr.h"
#include "bsp_serial.h"
#include "bsp_timer.h"
#include "intr.h"
#include "kprint.h"
#include "mmu.h"
#include "proc.h"
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

/*
 * Whatever the BSP asked to have mapped, whichever board this is. The boot
 * path has no business naming the console's address or the GIC's.
 */
static void
report_device_mappings(void)
{
	uint64_t base, size;
	unsigned i;

	for (i = 0; i < mmu_device_map_count(); i++) {
		if (!mmu_device_map_get(i, &base, &size))
			break;
		kputs("device      : ");
		report_mapping("", phys_to_virt(base));
	}
}

/*
 * Tick rate. MINIX calls this system_hz and gets it from the generic kernel;
 * there is no generic kernel here, so it is a constant until there is.
 */
#define SYSTEM_HZ	100

static volatile uint64_t ticks;

/*
 * For the benchmark, which reports how many ticks landed inside a window it
 * measured with interrupts enabled. This is clock.c's business in the generic
 * kernel and the accessor goes away when there is one.
 */
uint64_t
clock_ticks(void)
{
	return ticks;
}

static void
timer_tick(int irq)
{
	(void)irq;

	/*
	 * Re-arm first. On earm this is the same order: arch_clock.c's
	 * timer_int_handler() calls bsp_timer_int_handler() before doing
	 * anything of its own, so that a slow handler costs a late tick
	 * rather than a lost one.
	 */
	bsp_timer_int_handler();
	ticks++;

	/*
	 * Whether this interrupted the kernel or a process is the first thing
	 * a scheduler asks - it decides whose time was just spent. Here it
	 * only counts, but counting it is what shows the process is genuinely
	 * preemptible rather than merely reachable.
	 */
	if (trap_from_user())
		proc_count_user_tick();
}

static void
irq_enable(void)
{
	/* Clear PSTATE.I. head.S masked everything until there were vectors. */
	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

static void
start_ticking(void)
{
	uint64_t seen = 0;

	kputs("\ninterrupts and the tick\n");
	kputs("-----------------------\n");

	kputs("GIC lines   : ");
	kput_dec((uint64_t)bsp_irq_lines());
	kputs("\n");

	bsp_timer_init(SYSTEM_HZ);

	kputs("counter     : ");
	kput_dec(bsp_timer_counter_hz());
	kputs(" Hz\n");
	kputs("tick        : ");
	kput_dec(SYSTEM_HZ);
	kputs(" Hz\n");

	bsp_register_timer_handler(timer_tick);
	irq_enable();
	kputs("interrupts unmasked\n");

	/*
	 * Wait for a few ticks before going any further. If the timer were
	 * not running this would never wake, and it is worth failing here -
	 * with the reason on the console - rather than inside the first
	 * process, where the same silence would have a dozen explanations.
	 */
	while (ticks < 3) {
		__asm__ volatile("wfi");

		if (ticks == seen)
			continue;
		seen = ticks;

		kputs("tick ");
		kput_dec(seen);
		kputs("  counter ");
		kput_dec(bsp_timer_counter());
		kputs("\n");
	}
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
	kputs("MEECHO/aarch64 early boot\n");
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

	/*
	 * The interrupt controller is set up before paging because it has to
	 * register its register ranges with mmu_map_device() before the
	 * tables are built. It is programmed here through physical addresses,
	 * which is what the identity map is for.
	 */
	intr_init(0);

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
	/*
	 * Move every driver onto its kernel mapping before the identity map
	 * disappears. Until this returns, the console and the interrupt
	 * controller are still reaching their registers physically.
	 */
	mmu_activate_device_maps();

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
	report_device_mappings();

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
	start_ticking();

	/*
	 * Stage 3 wants to know what a message costs before deciding how big
	 * one should be. The kernel-side half of that measurement runs here,
	 * while there is still a straight line to run it on; the half that
	 * needs a process runs from user.S.
	 */
	bench_run();

	/*
	 * From here the kernel stops being a straight line. proc_start_first()
	 * abandons this stack and enters EL0; everything after that happens in
	 * the trap handler, on the trap stack, driven by what the process and
	 * the timer do. That is the shape a kernel keeps.
	 */
	proc_start_first();
}
