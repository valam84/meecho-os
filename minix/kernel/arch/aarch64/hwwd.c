/*
 * The SoC's hardware watchdog, fed from the boot core's timer tick.
 *
 * The kernel has two watchdogs of its own already, and neither sees the
 * failure this one exists for. bootwd resets the machine at a fixed time
 * after boot, whether it is well or not; lockwd (lockwd.c) catches a core
 * that holds the kernel lock too long, but it runs on another core, so it
 * cannot see a machine with one core (no_smp), or all cores stuck at once,
 * or a core hung outside the lock - see KERNEL-AUDIT.md, #10. A watchdog
 * outside the cores sees all of these the same way: the tick that restarts
 * it stops, and the machine resets. Without a word - nothing is left to say
 * it - but always.
 *
 * Why the tick: it is an interrupt, so a kernel stuck with interrupts masked
 * stops it, and a kernel stuck waiting for its own lock stops it too (the
 * tick enters the kernel through the same context_stop()). The boot core,
 * because it is the one core that keeps its timer while idle (proc.c,
 * idle()). What the tick does not catch is a system that is alive in the
 * kernel and dead above it - every server deadlocked, the clock still
 * ticking. That is not this layer's failure to see.
 *
 * The command line decides: hwwd=N seconds, rounded up to the next range the
 * part offers (22.4 s for the default 20 on the CB2); hwwd=0 leaves it off.
 *
 * Why 20 and not something shorter: this is the last resort, and lockwd
 * should get to speak first whenever it can, since it names the core and the
 * process and this cannot. lockwd fires 10 s into a wait for the lock, and an
 * idle secondary core starts waiting only when its half-period alarm wakes
 * it - up to 5 s after the hang. So lockwd may need 15 s. With the range at
 * 11.1 s (hwwd=10) the board reset before lockwd said anything, on four
 * cores as on one (port/cb2-wdt/reference.txt, the probe runs).
 * Off means never touched: the DesignWare part cannot be disabled once it
 * runs. A machine whose tree has no watchdog this file knows - QEMU's virt -
 * has nothing to do.
 *
 * The reset path was checked on the board before a line of this was
 * written: under its vendor Linux, /dev/watchdog held open and not fed for
 * a 10 s timeout reset the CB2 eleven seconds after the last feed, with the
 * firmware's settings and nothing configured in the CRU. See
 * port/cb2-wdt/reference.txt.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>
#include <minix/fdt.h>

#include "kernel/kernel.h"
#include "kernel/vm.h"

#include "arch_proto.h"
#include "hwwd.h"

#define HWWDVARNAME	"hwwd"
#define HWWD_DEFAULT_S	20

/*
 * Where the part is. volatile for the reason serial.c gives at length: this
 * is filled before the MMU is on, where the compiler's paired stores are
 * alignment faults.
 */
static volatile struct {
	vir_bytes base;
	vir_bytes offset;
	vir_bytes size;
	int kind;
	int running;
} wd;

static kern_phys_map hwwd_phys_map;

struct wd_scan {
	u64_t addr, size;
	int kind;
};

/*===========================================================================*
 *				scan_wd					     *
 *===========================================================================*/
static int
scan_wd(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct wd_scan *s = cookie;
	const char *status;
	unsigned len;

	if (depth == 0 || !fdt_node_is_compatible(node, "snps,dw-wdt"))
		return 0;

	/* A disabled node is a part the board's makers chose not to use. */
	status = fdt_getprop(node, "status", &len);
	if (status != NULL && len > 0 && strcmp(status, "okay") != 0 &&
	    strcmp(status, "ok") != 0)
		return 0;

	if (fdt_node_reg(node, 0, &s->addr, &s->size) != 0)
		return 0;

	s->kind = HWWD_DW;
	return 1;
}

/*===========================================================================*
 *				hwwd_probe				     *
 *===========================================================================*/
/*
 * From pre_init(), before the MMU is on: find the part and register its
 * registers for the kernel's own map. Nothing is written to it here.
 */
void
hwwd_probe(phys_bytes dtb)
{
	struct wd_scan s;

	memset(&s, 0, sizeof(s));
	if (dtb == 0 || !fdt_valid((const void *)dtb))
		return;
	if (!fdt_walk((const void *)dtb, scan_wd, &s))
		return;

	wd.offset = (vir_bytes)(s.addr & (AARCH64_PAGE_SIZE - 1));
	wd.base = (vir_bytes)(s.addr - wd.offset);
	wd.size = (vir_bytes)(wd.offset + s.size + AARCH64_PAGE_SIZE - 1) &
	    ~(vir_bytes)(AARCH64_PAGE_SIZE - 1);
	wd.kind = s.kind;

	kern_phys_map_ptr(wd.base, wd.size, VMMF_UNCACHED | VMMF_WRITE,
	    &hwwd_phys_map, (vir_bytes)&wd.base);
}

/*===========================================================================*
 *				hwwd_init				     *
 *===========================================================================*/
/*
 * From arch_init() on the boot core, where the command line is parsed and
 * the MMU long on. The tick starts later in kmain(), well inside the
 * shortest range the default asks for.
 */
void
hwwd_init(void)
{
	unsigned seconds = HWWD_DEFAULT_S;
	u32_t rate = 0, ms = 0;
	u64_t freq;
	char *value;

	if (wd.kind == HWWD_NONE)
		return;
	if ((value = env_get(HWWDVARNAME)) != NULL)
		seconds = (unsigned)atoi(value);
	if (seconds == 0) {
		printf("hwwd: watchdog at 0x%lx left off (hwwd=0)\n",
		    (unsigned long)(wd.base + wd.offset));
		return;
	}

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));

	switch (wd.kind) {
	case HWWD_DW:
		if (dw_wdt_start(wd.base + wd.offset, seconds, freq,
		    &rate, &ms) != 0) {
			printf("hwwd: dw-wdt with variable ranges, left off\n");
			return;
		}
		break;
	default:
		return;
	}
	wd.running = 1;

	if (rate != 0)
		printf("hwwd: dw-wdt, %u.%u MHz, resets after %u.%u s "
		    "without a tick\n", rate / 1000000, rate / 100000 % 10,
		    ms / 1000, ms / 100 % 10);
	else
		printf("hwwd: dw-wdt, clock rate not measurable, longest "
		    "range\n");
}

/*===========================================================================*
 *				hwwd_pet				     *
 *===========================================================================*/
/* Every tick of the boot core. One register write. */
void
hwwd_pet(void)
{
	if (!wd.running)
		return;

	switch (wd.kind) {
	case HWWD_DW:
		dw_wdt_pet(wd.base + wd.offset);
		break;
	}
}
