#ifndef _MINIX_FDT_H
#define _MINIX_FDT_H

/*
 * A reader for the flattened device tree the boot loader leaves in x0.
 *
 * On AArch64 this is how a kernel is told what machine it is on: not only how
 * much memory there is and where, but the core clock, the addresses of the
 * devices and how many CPUs there are. The two 32-bit MINIX ports get their
 * memory map from a multiboot-shaped structure U-Boot fills in; there is no
 * such structure here, and inventing one outside the kernel would mean
 * maintaining a boot loader beside the one that already answers the question.
 * See port/PORTING-LOG.md, stage 4 group 1.
 *
 * The reader started life inside the kernel and moved to libsys when the
 * kernel stopped being its only reader: RS grants a driver the registers and
 * interrupt of a device-tree node named in system.conf, and a driver finds
 * its device by walking the same tree. The kernel links libsys, so one copy
 * of the code serves all three. Nothing in it allocates or keeps state, which
 * is what lets it run before the MMU is on as well as in a process.
 *
 * Deliberately few operations: walk the tree, read a property of a node, and
 * two conveniences every caller was writing for itself - is this node
 * compatible with X, and which interrupt line does this GIC triplet name.
 * Everything else a caller asks - which ranges are RAM, what the boot
 * arguments were, where the initrd is - is a few lines on top of those, and
 * lives with the code that asks rather than here.
 *
 * Addressing: every entry point takes the blob's address as the caller can
 * see it right now. In the kernel before the MMU is on that is the physical
 * address the loader passed; afterwards it is phys2vir() of the same number;
 * in a process it is the copy fdt_fetch() returned. Nothing is remembered
 * between calls, so there is no stale pointer to carry across a switch.
 */

#include <sys/types.h>
#include <minix/type.h>

/*
 * A node the walk has reached. Holds where its properties begin; the blob
 * comes along because property names live in a separate string block.
 *
 * addr_cells and size_cells are the widths this node's "reg" is written in.
 * They are a property of the parent (#address-cells, #size-cells, with the
 * specification's defaults of 2 and 1 when the parent says nothing), so a
 * callback could only learn them by keeping a stack of its own; the walk
 * keeps that stack once, for everyone.
 */
struct fdt_node {
	const void *dtb;
	const void *props;
	unsigned addr_cells;
	unsigned size_cells;
};

/*
 * Called once per node, parents before children. depth is 0 for the root.
 * Returning non-zero stops the walk and becomes fdt_walk()'s result, which
 * is how a search says "found it".
 */
typedef int (*fdt_node_cb)(void *cookie, int depth, const char *name,
	const struct fdt_node *node);

/* Is this a device tree blob this code can read? */
int fdt_valid(const void *dtb);

/* Total size of the blob, so its memory can be kept out of the free list. */
size_t fdt_size(const void *dtb);

/*
 * The index'th entry of the blob's memory reservation block - memory the
 * firmware keeps and the system must not hand out. 0 on success, -1 once the
 * list ends, so a caller counts up until it stops.
 *
 * The block predates /reserved-memory and says the same kind of thing; a
 * machine may use either spelling or both, so both want reading. It lives in
 * the header rather than in the tree, which is why a walk cannot reach it.
 */
int fdt_memreserve(const void *dtb, unsigned index, u64_t *addr, u64_t *size);

int fdt_walk(const void *dtb, fdt_node_cb cb, void *cookie);

/*
 * The value of one property, or NULL if the node has no such property.
 * *len, when len is not NULL, is set to its length in bytes.
 */
const void *fdt_getprop(const struct fdt_node *node, const char *name,
	unsigned *len);

/*
 * Read one number out of property data. Device tree numbers are big-endian
 * sequences of 32-bit cells, and how many cells make an address or a size is
 * a property of the parent node - which is why the count is an argument.
 */
u64_t fdt_read_cells(const void *data, unsigned cells);

/*
 * Does the node's "compatible" list name this string? The property is a
 * sequence of NUL-terminated strings, most specific first, and a match on
 * any of them is a match.
 */
int fdt_node_is_compatible(const struct fdt_node *node, const char *want);

/*
 * The interrupt line named by one <type number flags> triplet of an
 * "interrupts" property whose parent is a GIC: SPI n is line 32 + n, PPI n
 * is line 16 + n. Returns -1 for a type this does not know. Every AArch64
 * machine this system targets has a GIC, so the binding is a property of
 * the architecture rather than of a board; a machine with another interrupt
 * parent would need a second translation beside this one, not a change to
 * it.
 */
int fdt_gic_intid(const void *triplet);

/*
 * The index'th <address size> pair of the node's "reg", read with the cell
 * widths the walk recorded. 0 on success, -1 when there is no such pair or
 * the widths are ones this reader does not handle. The address is as the
 * node's bus writes it; a bus that translates ("ranges") is not applied.
 */
int fdt_node_reg(const struct fdt_node *node, unsigned index, u64_t *addr,
	u64_t *size);

/*
 * The index'th interrupt of the node's "interrupts", as a GIC line; -1 when
 * there is none or its type is unknown. Assumes the three-cell GIC binding,
 * as fdt_gic_intid() does.
 */
int fdt_node_gic_irq(const struct fdt_node *node, unsigned index);

/*
 * The "reg" of the node some other node points at.
 *
 * A device tree refers to a device from another device by phandle: the
 * network controller names its GRF that way, its reset controller that way,
 * and the GPIO that resets its PHY that way.  Following one means finding
 * the node that carries a "phandle" property of that value, which is a
 * second walk of the tree - and a walk is all this reader has, so that is
 * what this does.
 *
 * The alternative, which the driver before this one used, is to look for
 * the pointed-at node by its compatible string instead.  That works while
 * there is one of its kind on the machine and stops working at the second:
 * this SoC has five GPIO banks, and "the one with this phandle" is the only
 * question with an answer.
 *
 * Returns 0 and fills in addr and size, or -1.
 */
int fdt_phandle_reg(const void *dtb, u32_t phandle, unsigned index,
	u64_t *addr, u64_t *size);

/*
 * A process's copy of the blob the kernel booted with, freshly allocated
 * with malloc(); the caller owns it. NULL, with errno set, when the kernel
 * has no device tree - which is how a machine without one, or an
 * architecture that never had one, answers. Not for the kernel, which has
 * the blob already.
 */
void *fdt_fetch(void);

#endif /* _MINIX_FDT_H */
