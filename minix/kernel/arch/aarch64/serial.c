/*
 * Finding the kernel's console in the device tree, and handing bsp_ser_putc()
 * to whichever UART is there.
 *
 * This used to be a constant. bsp/qemu-virt/virt_serial.c knew the PL011 sat
 * at 0x09000000 because the console has to come up as the first statement of
 * pre_init() - before the device tree has even been recorded, so that a
 * failure to parse it is reportable - and a driver that runs that early has
 * no one to ask.
 *
 * The CB2 ended that. Its console is a DesignWare 8250 at 0xfe660000, not a
 * PL011, and one kernel has to talk on both machines: the development cycle
 * runs in QEMU and the target is the board, so a build-time choice would mean
 * two kernels and two ways to be wrong. So the console is now asked for the
 * same way everything else on this port is asked for - the device tree - and
 * this file does its own small walk of the blob before anything else uses it.
 *
 * What that costs is the one case the old arrangement covered: when the tree
 * itself is unreadable, the console stays silent and "malformed device tree"
 * goes nowhere. That case is not worth a table of board addresses to fix. A
 * kernel handed a broken tree does not know what machine it is on, and
 * writing a UART address that belongs to some other machine is not a
 * diagnostic - it is a fault at an address nothing maps, on a machine with no
 * exception vectors installed yet. Silence is the honest answer, and the
 * project already decided this shape once: drivers here hold no board tables,
 * they ask the tree (see PORTING-LOG.md, stage 7.2).
 *
 * The kind of UART is remembered as an int rather than a table of function
 * pointers, for the reason gic.c gives at length: this runs before the MMU is
 * on, so a pointer stored here would be a physical address that stops
 * existing after the move to the upper half. An int survives it.
 */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <minix/type.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/vm.h"

#include "arch_proto.h"
#include <minix/fdt.h>

#include "serial.h"
#include "bsp_serial.h"

/*
 * Where the console is. base is the one field VM rewrites, through the
 * pointer handed to kern_phys_map_ptr(); offset carries the distance from the
 * start of the mapped page to the registers, because a reg property need not
 * be page aligned even though every console met so far is.
 *
 * volatile, and not for the usual reason - nothing here is written by
 * hardware. It is what stops the compiler from merging two adjacent fields
 * into one paired access. GCC turned
 *
 *	console.shift = s.shift;
 *	console.width = s.width;
 *
 * into a single "stp w1, w2, [x0, #28]", and that instruction is a fault
 * here: this structure is filled from pre_init(), before the MMU is on, when
 * every address the kernel touches is Device memory - and on Device memory an
 * LDP/STP counts as one access as wide as the whole pair, so it has to be
 * aligned to that width. Offset 28 is not. The machine stops with no
 * exception vectors installed yet, which is to say silently.
 *
 * The same trap took the packed attribute off multiboot_mmap_entry (see
 * CLAUDE.md), and it is worth knowing that it can bite through nothing more
 * exotic than two ints side by side in a struct.
 */
static volatile struct {
	vir_bytes base;
	vir_bytes offset;
	vir_bytes size;
	int kind;
	unsigned shift;
	unsigned width;
} console;

static kern_phys_map serial_phys_map;

/* Long enough for the paths that occur: "/serial@fe660000" and the like. */
#define CONSOLE_PATH_MAX	96

struct ser_scan {
	/* Passes one and two: what /chosen says, resolved through /aliases. */
	char path[CONSOLE_PATH_MAX];

	/* Pass three: the node to match, and what was found on it. */
	const char *want;
	int kind;
	u64_t addr, size;
	unsigned shift, width;
	int found;
};

/*===========================================================================*
 *				copy_token				     *
 *===========================================================================*/
/*
 * Copy src into dst, stopping at NUL or at stop. Truncation is not an error
 * worth reporting: a path this long is a tree we would not understand anyway,
 * and the match in pass two simply fails.
 */
static void
copy_token(char *dst, size_t size, const char *src, char stop)
{
	size_t i;

	for (i = 0; i + 1 < size && src[i] != '\0' && src[i] != stop; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/*===========================================================================*
 *				last_component				     *
 *===========================================================================*/
/* The part of a device tree path after the final slash: its node name. */
static const char *
last_component(const char *path)
{
	const char *p, *last = path;

	for (p = path; *p != '\0'; p++)
		if (*p == '/')
			last = p + 1;

	return last;
}

/*===========================================================================*
 *				scan_chosen				     *
 *===========================================================================*/
/*
 * Pass one: /chosen/stdout-path names the console.
 */
static int
scan_chosen(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;
	const char *p;
	unsigned len;

	if (depth != 1 || strcmp(name, "chosen") != 0)
		return 0;

	/*
	 * The property is "stdout-path"; "linux,stdout-path" is the older
	 * spelling, and trees in the wild still carry it.
	 *
	 * Its value is a path, optionally followed by ':' and the line
	 * settings - "serial2:1500000n8" on the CB2. The settings are not
	 * read: the loader has already applied them, which is why
	 * ns8250_init() programs nothing.
	 */
	if ((p = fdt_getprop(node, "stdout-path", &len)) == NULL)
		p = fdt_getprop(node, "linux,stdout-path", &len);
	if (p != NULL && len > 0)
		copy_token(s->path, sizeof(s->path), p, ':');

	return 1;
}

/*===========================================================================*
 *				scan_alias				     *
 *===========================================================================*/
/*
 * Pass two: turn an alias into a path. QEMU writes stdout-path as a path
 * already ("/pl011@9000000") and skips this; the CB2 writes "serial2", which
 * /aliases resolves to "/serial@fe660000".
 *
 * A separate walk, rather than remembering the /aliases node from pass one,
 * because remembering it means copying a struct fdt_node - and the compiler
 * copies a 24-byte struct with an ldp/stp pair, which is a 16-byte access,
 * which faults on Device memory unless the address happens to be 16-aligned.
 * That is the same trap the console struct above documents, and here it is
 * cheaper to walk the blob again than to depend on where a stack frame fell.
 */
static int
scan_alias(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;
	const char *p;
	unsigned len;

	if (depth != 1 || strcmp(name, "aliases") != 0)
		return 0;

	if ((p = fdt_getprop(node, s->path, &len)) != NULL && len > 0)
		copy_token(s->path, sizeof(s->path), p, '\0');

	return 1;
}

/*===========================================================================*
 *				take_uart				     *
 *===========================================================================*/
/*
 * Is this node a UART this kernel can print on, and if so, where and how.
 * Returns 1 when it filled the scan in.
 */
static int
take_uart(struct ser_scan *s, const struct fdt_node *node)
{
	const void *p;
	unsigned len;
	int kind;

	if (fdt_node_is_compatible(node, "arm,pl011"))
		kind = SERIAL_PL011;
	else if (fdt_node_is_compatible(node, "snps,dw-apb-uart") ||
	    fdt_node_is_compatible(node, "ns16550a") ||
	    fdt_node_is_compatible(node, "ns16550"))
		kind = SERIAL_8250;
	else
		return 0;

	if (fdt_node_reg(node, 0, &s->addr, &s->size) != 0)
		return 0;

	/*
	 * The 8250 defaults - one byte per register, byte-wide access - are
	 * what the original part does and what a tree that says nothing means.
	 * The RK3566 says 2 and 4. The PL011 has neither property and needs
	 * neither: its offsets are fixed and its registers are words.
	 */
	s->shift = 0;
	s->width = 1;
	if ((p = fdt_getprop(node, "reg-shift", &len)) != NULL && len == 4)
		s->shift = (unsigned)fdt_read_cells(p, 1);
	if ((p = fdt_getprop(node, "reg-io-width", &len)) != NULL && len == 4)
		s->width = (unsigned)fdt_read_cells(p, 1);

	s->kind = kind;
	s->found = 1;

	return 1;
}

/*===========================================================================*
 *				scan_uart				     *
 *===========================================================================*/
/*
 * Pass two. With a name from /chosen, take that node and no other: a board
 * has several UARTs and only one of them is wired to a connector - the CB2
 * has ten, and the first in the tree is not the console. Without a name, any
 * UART is better than none.
 */
static int
scan_uart(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct ser_scan *s = cookie;

	if (depth == 0)
		return 0;

	if (s->want != NULL) {
		if (strcmp(name, s->want) != 0)
			return 0;
		/*
		 * Stop either way. The tree named this node as the console, so
		 * if it is not a UART this code knows, there is nothing better
		 * further down - and picking some other port would print into
		 * a connector nobody is watching.
		 */
		(void)take_uart(s, node);
		return 1;
	}

	return take_uart(s, node);
}

/*===========================================================================*
 *				find_console				     *
 *===========================================================================*/
static void
find_console(phys_bytes dtb)
{
	struct ser_scan s;

	memset(&s, 0, sizeof(s));

	if (dtb == 0 || !fdt_valid((const void *)dtb))
		return;

	/*
	 * A walk that finds what it wants returns non-zero, so the result says
	 * nothing about success here: a tree with no /chosen is a tree the
	 * next pass simply searches by compatible instead.
	 */
	(void)fdt_walk((const void *)dtb, scan_chosen, &s);

	/*
	 * A path starting with '/' is the node itself; anything else is an
	 * alias, and /aliases turns it into a path. The CB2 writes "serial2",
	 * QEMU's virt machine writes "/pl011@9000000".
	 */
	if (s.path[0] != '\0' && s.path[0] != '/')
		(void)fdt_walk((const void *)dtb, scan_alias, &s);

	if (s.path[0] == '/')
		s.want = last_component(s.path);

	(void)fdt_walk((const void *)dtb, scan_uart, &s);

	if (!s.found)
		return;

	console.offset = (vir_bytes)(s.addr & (AARCH64_PAGE_SIZE - 1));
	console.base = (vir_bytes)(s.addr - console.offset);
	console.size = (vir_bytes)(console.offset + s.size +
	    AARCH64_PAGE_SIZE - 1) & ~(vir_bytes)(AARCH64_PAGE_SIZE - 1);
	console.shift = s.shift;
	console.width = s.width;
	console.kind = s.kind;
}

/*===========================================================================*
 *				bsp_ser_init				     *
 *===========================================================================*/
void
bsp_ser_init(phys_bytes dtb)
{
	find_console(dtb);

	if (console.kind == SERIAL_NONE)
		return;

	/*
	 * The register base is not a constant. The kernel talks on this port
	 * before paging exists, when the address it needs is the physical one,
	 * and goes on talking on it afterwards, when the address it needs is
	 * whatever VM chose for the mapping. So the base lives in a variable,
	 * kern_phys_map_ptr() registers the range and names that variable, and
	 * VM rewrites it through the callback once the mapping is in place.
	 * bsp/ti does the same thing on ARM.
	 */
	kern_phys_map_ptr(console.base, console.size,
	    VMMF_UNCACHED | VMMF_WRITE, &serial_phys_map,
	    (vir_bytes)&console.base);
	assert(console.base);

	switch (console.kind) {
	case SERIAL_PL011:
		pl011_init(console.base + console.offset);
		break;
	case SERIAL_8250:
		ns8250_init(console.base + console.offset, console.shift,
		    console.width);
		break;
	}
}

/*===========================================================================*
 *				bsp_ser_putc				     *
 *===========================================================================*/
void
bsp_ser_putc(char c)
{
	/*
	 * A machine whose tree named no console this code understands prints
	 * nowhere, rather than into an address that belongs to something else.
	 */
	switch (console.kind) {
	case SERIAL_PL011:
		pl011_putc(console.base + console.offset, c);
		break;
	case SERIAL_8250:
		ns8250_putc(console.base + console.offset, console.shift,
		    console.width, c);
		break;
	}
}
