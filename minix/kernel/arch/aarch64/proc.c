/*
 * The first process: build an address space, put a program in it, and run it
 * at EL0.
 *
 * There is no loader, no file system and no scheduler. The program is a blob
 * assembled into the kernel image by user.S; the kernel copies it into a page
 * it then maps as user text. That is enough to answer the question stage 2.6
 * exists to answer - does a process execute, and does control come back - and
 * everything above it belongs to stage 4.
 */

#include <stdint.h>

#include "bsp_serial.h"
#include "kprint.h"
#include "mmu.h"
#include "proc.h"
#include "trap.h"

/* The program, assembled into .rodata by user.S. */
extern char user_prog_start[];
extern char user_prog_end[];

/*
 * Where the process sees itself.
 *
 * Two pages at conventional-looking addresses, well clear of zero so that a
 * null pointer in the program faults rather than landing on its own code.
 */
#define USER_TEXT_VA	0x0000000000400000UL
#define USER_STACK_VA	0x0000000000800000UL
#define USER_STACK_TOP	(USER_STACK_VA + PAGE_SIZE)

#define USER_ASID	1

/*
 * The pages behind those addresses. Ordinary kernel BSS: there is no page
 * allocator, and pretending otherwise would be more code than the whole of
 * this file.
 */
static uint8_t user_text_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static uint8_t user_stack_page[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static struct proc first_proc;

/*
 * PSTATE for a process.
 *
 * M[3:0] = 0 is EL0t: EL0, using SP_EL0. I is clear so the timer can
 * interrupt it, which is the point of doing this after stage 2.5 rather than
 * before. F, A and D are masked because nothing raises them and nothing would
 * know what to do if they did.
 */
#define SPSR_EL0T	0x00000340UL

static uint64_t ticks_in_user;
static uint64_t syscalls;

static void halt(void) __attribute__((noreturn));

static void
halt(void)
{
	for (;;)
		__asm__ volatile("wfi");
}

void
proc_count_user_tick(void)
{
	ticks_in_user++;
}

static void
copy_bytes(void *dst, const void *src, uint64_t len)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	uint64_t i;

	for (i = 0; i < len; i++)
		d[i] = s[i];
}

/* usercopy.S */
extern int usercopy_bytes(void *dst, const void *src, uint64_t len);
extern char usercopy_fault[];

int
copyin(uint64_t uaddr, void *dst, uint64_t len)
{
	int r;

	/*
	 * The pointer belongs to the process, so the copy has to be allowed
	 * to fail. Arming the expectation points the abort handler at the
	 * resume label inside usercopy.S, which returns -1 from there.
	 */
	trap_expect_fault_at((uint64_t)usercopy_fault);
	r = usercopy_bytes(dst, (const void *)uaddr, len);
	trap_expect_clear();

	return r;
}

int
copyout(uint64_t uaddr, const void *src, uint64_t len)
{
	int r;

	trap_expect_fault_at((uint64_t)usercopy_fault);
	r = usercopy_bytes((void *)uaddr, src, len);
	trap_expect_clear();

	return r;
}

static void
sys_write(struct stackframe_s *frame)
{
	char buf[128];
	uint64_t uaddr = frame->retreg;		/* x0 */
	uint64_t len = frame->x1;
	uint64_t i;

	if (len > sizeof(buf))
		len = sizeof(buf);

	/*
	 * The pointer came from the process and is not to be trusted. This is
	 * the only reason copyin() exists, and the reason it has to fail
	 * rather than fault.
	 */
	if (copyin(uaddr, buf, len) != 0) {
		kputs("  [refused: ");
		kput_hex(uaddr);
		kputs(" is not the process's to read]\n");
		frame->retreg = (uint64_t)-1;
		return;
	}

	kputs("  [user] ");
	for (i = 0; i < len; i++) {
		if (buf[i] == '\n')
			bsp_ser_putc('\r');
		bsp_ser_putc(buf[i]);
	}

	frame->retreg = len;
}

void
syscall_handler(struct stackframe_s *frame)
{
	syscalls++;

	switch (frame->x8) {
	case SYS_WRITE:
		sys_write(frame);
		break;

	case SYS_GETTICKS:
		frame->retreg = ticks_in_user;
		break;

	case SYS_EXIT:
		kputs("\nprocess exited\n");
		kputs("exit status : ");
		kput_dec(frame->retreg);
		kputs("\nsystem calls: ");
		kput_dec(syscalls);
		kputs("\nticks at EL0: ");
		kput_dec(ticks_in_user);
		kputs("\n\nnothing left to run, halting.\n");
		halt();

	default:
		kputs("unknown system call ");
		kput_dec(frame->x8);
		kputs("\n");
		frame->retreg = (uint64_t)-1;
		break;
	}
}

void
proc_fault(void)
{
	kputs("\nthe process cannot continue, and there is nobody else to run\n");
	halt();
}

void
proc_start_first(void)
{
	uint64_t prog_len = (uint64_t)user_prog_end - (uint64_t)user_prog_start;
	struct stackframe_s *frame;

	first_proc.p_name = "first";
	first_proc.p_asid = USER_ASID;
	first_proc.p_ttbr0 = mmu_user_create();

	kputs("\nfirst process\n");
	kputs("-------------\n");
	kputs("program size: ");
	kput_dec(prog_len);
	kputs(" bytes\n");

	if (prog_len > PAGE_SIZE) {
		kputs("the program does not fit in its page\n");
		halt();
	}

	copy_bytes(user_text_page, user_prog_start, prog_len);

	/*
	 * The program was written as data and is about to be executed. The
	 * data and instruction caches are not coherent with each other, so
	 * without this the process could fetch whatever that page held
	 * before. QEMU does not model it; the Cortex-A72 does, and the
	 * failure would appear only on the board.
	 */
	mmu_sync_icache((uint64_t)user_text_page, prog_len);

	mmu_user_map_text(first_proc.p_ttbr0, USER_TEXT_VA,
	    mmu_kern_phys(user_text_page), PAGE_SIZE);
	mmu_user_map_data(first_proc.p_ttbr0, USER_STACK_VA,
	    mmu_kern_phys(user_stack_page), PAGE_SIZE);

	mmu_user_enter(first_proc.p_ttbr0, first_proc.p_asid);

	kput_line("TTBR0_EL1   : ", first_proc.p_ttbr0);
	kput_line("entry       : ", USER_TEXT_VA);
	kput_line("stack top   : ", USER_STACK_TOP);

	first_proc.p_reg.pc = USER_TEXT_VA;
	first_proc.p_reg.sp = USER_STACK_TOP;
	first_proc.p_reg.psr = SPSR_EL0T;

	/*
	 * Hand the context to the frame the return path reads. With more than
	 * one process this is where a scheduler would decide whose context to
	 * copy in, and where the outgoing one would be copied back out.
	 */
	frame = trap_user_frame();
	copy_bytes(frame, &first_proc.p_reg, sizeof(*frame));

	kputs("entering EL0\n\n");
	restore_user_context();
}
