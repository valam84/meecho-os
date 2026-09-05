#ifndef _AARCH64_FDT_H
#define _AARCH64_FDT_H

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
 * Deliberately only two operations: walk the tree, and read a property of a
 * node. Everything the kernel asks - which ranges are RAM, what the boot
 * arguments were, where the initrd is - is a few lines on top of those, and
 * lives with the code that asks rather than here.
 *
 * Addressing: every entry point takes the blob's address as the caller can
 * see it right now. Before the MMU is on that is the physical address the
 * loader passed; afterwards it is phys2vir() of the same number. Nothing is
 * remembered between calls, so there is no stale pointer to carry across the
 * switch.
 */

#include <minix/type.h>

/*
 * A node the walk has reached. Holds where its properties begin; the blob
 * comes along because property names live in a separate string block.
 */
struct fdt_node {
	const void *dtb;
	const void *props;
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

#endif /* _AARCH64_FDT_H */
