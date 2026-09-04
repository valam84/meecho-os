/*
 * What to do with an exception once exception.S has saved the registers.
 *
 * At this stage that means one of two things: step over a fault the kernel
 * said it was expecting, or report everything known about the fault and stop.
 * There is no scheduler to kill anything and no user process to blame.
 *
 * This code has to survive being called before the MMU is on, because that is
 * when a fault is hardest to diagnose and vectors are most valuable. So it
 * obeys the same rule as mmu.c: no static data containing an address. That is
 * why the tables below are arrays of characters rather than arrays of
 * pointers to string literals - a char array is reached with PC-relative
 * adrp, an array of pointers holds link-time constants.
 */

#include <stddef.h>
#include <stdint.h>

#include "bsp_serial.h"
#include "kprint.h"
#include "trap.h"

/* exception.S */
extern char exception_vectors[];

/* The offsets exception.S was assembled against have to be the real ones. */
_Static_assert(sizeof(struct stackframe_s) == FRAME_SIZE, "frame size");
_Static_assert(offsetof(struct stackframe_s, retreg) == FRAME_X(0), "x0");
_Static_assert(offsetof(struct stackframe_s, x1) == FRAME_X(1), "x1");
_Static_assert(offsetof(struct stackframe_s, x18) == FRAME_X(18), "x18");
_Static_assert(offsetof(struct stackframe_s, lr) == FRAME_X(30), "x30");
_Static_assert(offsetof(struct stackframe_s, sp) == FRAME_SP, "sp");
_Static_assert(offsetof(struct stackframe_s, pc) == FRAME_PC, "pc");
_Static_assert(offsetof(struct stackframe_s, psr) == FRAME_PSR, "psr");

/* ESR_EL1 fields. */
#define ESR_EC(esr)		(((esr) >> 26) & 0x3f)
#define ESR_ISS(esr)		((esr) & 0x1ffffff)

/* Exception classes we can say something useful about. */
#define EC_UNKNOWN		0x00
#define EC_WF			0x01
#define EC_ILLEGAL_STATE	0x0e
#define EC_SVC64		0x15
#define EC_MSR_MRS		0x18
#define EC_IABT_LOWER		0x20
#define EC_IABT_SAME		0x21
#define EC_PC_ALIGN		0x22
#define EC_DABT_LOWER		0x24
#define EC_DABT_SAME		0x25
#define EC_SP_ALIGN		0x26
#define EC_SERROR		0x2f
#define EC_BRK64		0x3c

/* Data abort ISS: bit 6 says the access was a write. */
#define ISS_WNR			(1UL << 6)
/* Data and instruction abort ISS: the fault status code. */
#define ISS_FSC(iss)		((iss) & 0x3f)

#define NAME_LEN		34

static const char vector_name[16][NAME_LEN] = {
	"EL1t synchronous",
	"EL1t IRQ",
	"EL1t FIQ",
	"EL1t SError",
	"EL1h synchronous",
	"EL1h IRQ",
	"EL1h FIQ",
	"EL1h SError",
	"EL0 AArch64 synchronous",
	"EL0 AArch64 IRQ",
	"EL0 AArch64 FIQ",
	"EL0 AArch64 SError",
	"EL0 AArch32 synchronous",
	"EL0 AArch32 IRQ",
	"EL0 AArch32 FIQ",
	"EL0 AArch32 SError",
};

struct code_name {
	uint32_t code;
	char name[NAME_LEN];
};

static const struct code_name ec_name[] = {
	{ EC_UNKNOWN,		"unknown reason" },
	{ EC_WF,		"trapped WFI or WFE" },
	{ EC_ILLEGAL_STATE,	"illegal execution state" },
	{ EC_SVC64,		"SVC" },
	{ EC_MSR_MRS,		"trapped MSR or MRS" },
	{ EC_IABT_LOWER,	"instruction abort, lower EL" },
	{ EC_IABT_SAME,		"instruction abort, same EL" },
	{ EC_PC_ALIGN,		"PC alignment fault" },
	{ EC_DABT_LOWER,	"data abort, lower EL" },
	{ EC_DABT_SAME,		"data abort, same EL" },
	{ EC_SP_ALIGN,		"SP alignment fault" },
	{ EC_SERROR,		"SError" },
	{ EC_BRK64,		"BRK instruction" },
};

/*
 * Fault status codes. The level is in the low two bits of the translation,
 * access-flag and permission groups, so those are listed per level rather
 * than decoded separately - shorter than the arithmetic that would avoid it.
 */
static const struct code_name fsc_name[] = {
	{ 0x00, "address size fault, level 0" },
	{ 0x01, "address size fault, level 1" },
	{ 0x02, "address size fault, level 2" },
	{ 0x03, "address size fault, level 3" },
	{ 0x04, "translation fault, level 0" },
	{ 0x05, "translation fault, level 1" },
	{ 0x06, "translation fault, level 2" },
	{ 0x07, "translation fault, level 3" },
	{ 0x09, "access flag fault, level 1" },
	{ 0x0a, "access flag fault, level 2" },
	{ 0x0b, "access flag fault, level 3" },
	{ 0x0d, "permission fault, level 1" },
	{ 0x0e, "permission fault, level 2" },
	{ 0x0f, "permission fault, level 3" },
	{ 0x10, "external abort" },
	{ 0x21, "alignment fault" },
	{ 0x30, "TLB conflict abort" },
};

static const char unknown_name[NAME_LEN] = "unrecognised";

static volatile int trap_expecting;
static volatile int trap_fired;
static volatile uint64_t trap_esr;
static volatile uint64_t trap_far;

/*
 * Depth guard. A fault inside the handler would otherwise fault again on the
 * same instruction forever, and the console would fill with the first half of
 * a report.
 */
static volatile int trap_depth;

static const char *
lookup(const struct code_name *table, unsigned entries, uint32_t code)
{
	unsigned i;

	for (i = 0; i < entries; i++)
		if (table[i].code == code)
			return table[i].name;

	return unknown_name;
}

void
trap_init(void)
{
	__asm__ volatile(
		"msr	vbar_el1, %0\n\t"
		"isb"
		:: "r"((uint64_t)exception_vectors) : "memory");
}

void
trap_expect_fault(void)
{
	trap_expecting = 1;
	trap_fired = 0;
}

int
trap_took_fault(uint64_t *esr, uint64_t *far)
{
	trap_expecting = 0;

	if (!trap_fired)
		return 0;

	*esr = trap_esr;
	*far = trap_far;
	return 1;
}

static void
put_regname(unsigned n)
{
	bsp_ser_putc('x');
	bsp_ser_putc((char)('0' + n / 10));
	bsp_ser_putc((char)('0' + n % 10));
	bsp_ser_putc(' ');
}

static void
dump_registers(const struct stackframe_s *frame)
{
	const reg_t *x = &frame->retreg;
	unsigned i;

	/*
	 * Two per line. Wider would wrap on an 80-column console, and a
	 * wrapped register dump is worse than a long one.
	 */
	for (i = 0; i < 31; i++) {
		put_regname(i);
		kput_hex(x[i]);
		if ((i % 2) == 1 || i == 30)
			kputs("\n");
		else
			kputs("  ");
	}

	kput_line("sp  ", frame->sp);
	kput_line("pc  ", frame->pc);
	kput_line("psr ", frame->psr);
}

/* Decode the part of ESR that depends on the exception class. */
static void
describe_fault(uint64_t esr, uint64_t far)
{
	uint32_t ec = (uint32_t)ESR_EC(esr);
	uint32_t iss = (uint32_t)ESR_ISS(esr);

	kputs("ESR         : ");
	kput_hex(esr);
	kputs("  EC ");
	kput_hexn(ec, 2);
	kputs("\n              ");
	kputs(lookup(ec_name, sizeof(ec_name) / sizeof(ec_name[0]), ec));
	kputs("\n");

	if (ec != EC_DABT_SAME && ec != EC_DABT_LOWER &&
	    ec != EC_IABT_SAME && ec != EC_IABT_LOWER)
		return;

	kputs("              ");
	if (ec == EC_DABT_SAME || ec == EC_DABT_LOWER)
		kputs((iss & ISS_WNR) ? "write, " : "read, ");
	kputs(lookup(fsc_name, sizeof(fsc_name) / sizeof(fsc_name[0]),
	    ISS_FSC(iss)));
	kputs("\n");

	kput_line("FAR         : ", far);
}

void
trap_handler(struct stackframe_s *frame, uint64_t kind, uint64_t esr,
	uint64_t far)
{
	uint32_t ec = (uint32_t)ESR_EC(esr);

	/*
	 * A data abort the kernel asked for. Record it, step over the
	 * instruction that took it and go back.
	 *
	 * Only data aborts: every A64 instruction is four bytes, so skipping
	 * one is well defined, but an instruction abort means the four bytes
	 * after the fault are no more fetchable than the fault itself.
	 */
	if (trap_expecting && kind == EXC_EL1H_SYNC && ec == EC_DABT_SAME) {
		trap_expecting = 0;
		trap_fired = 1;
		trap_esr = esr;
		trap_far = far;
		frame->pc += 4;
		return;
	}

	if (++trap_depth > 1) {
		kputs("\nfault inside the exception handler, stopping\n");
		for (;;)
			__asm__ volatile("wfi");
	}

	kputs("\n=== unhandled exception ===\n");

	kputs("vector      : ");
	kput_hexn(kind, 2);
	kputs("  ");
	kputs(kind < 16 ? vector_name[kind] : unknown_name);
	kputs("\n");

	describe_fault(esr, far);

	kputs("\n");
	dump_registers(frame);

	kputs("\nno way to recover, halting.\n");
	for (;;)
		__asm__ volatile("wfi");
}
