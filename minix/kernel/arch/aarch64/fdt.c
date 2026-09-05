/*
 * Flattened device tree reader.
 *
 * The format is the Devicetree Specification's version 17 blob: a header, a
 * block of tokens describing the tree, and a block of NUL-terminated property
 * names the tokens index into.
 *
 * Two properties of this code matter more than its size. It runs before the
 * MMU is on, so it allocates nothing and keeps no state between calls - the
 * caller passes the blob's address every time, which is the physical one
 * early and the kernel-virtual one later. And it reads every number a byte at
 * a time: the blob is big-endian and this kernel is not, and nothing
 * guarantees the alignment of a property's data. Byte assembly settles both
 * questions without an assumption to get wrong.
 */

#include "kernel/kernel.h"

#include <string.h>

#include "fdt.h"

/* Header, all fields big-endian 32-bit, at these byte offsets. */
#define FDT_OFF_MAGIC		0
#define FDT_OFF_TOTALSIZE	4
#define FDT_OFF_DT_STRUCT	8
#define FDT_OFF_DT_STRINGS	12
#define FDT_OFF_VERSION		20
#define FDT_OFF_LAST_COMP_VER	24
#define FDT_OFF_SIZE_DT_STRUCT	36

#define FDT_MAGIC		0xd00dfeedU

/*
 * The version this reader understands. A blob says which version it is and
 * which is the oldest reader that can still read it; checking the second is
 * what lets a newer blob be read rather than rejected.
 */
#define FDT_SUPPORTED_VERSION	17

/* Tokens in the structure block. */
#define FDT_BEGIN_NODE		1
#define FDT_END_NODE		2
#define FDT_PROP		3
#define FDT_NOP			4
#define FDT_END			9

/* Nothing sane is this deep; a blob that says so is corrupt. */
#define FDT_MAX_DEPTH		32

/*===========================================================================*
 *				fdt_be32				     *
 *===========================================================================*/
static u32_t
fdt_be32(const void *p)
{
	const unsigned char *b = p;

	return ((u32_t)b[0] << 24) | ((u32_t)b[1] << 16) |
	    ((u32_t)b[2] << 8) | (u32_t)b[3];
}

/* Every token and every property's data start on a four-byte boundary. */
static unsigned
fdt_align4(unsigned n)
{
	return (n + 3) & ~3U;
}

/*===========================================================================*
 *				fdt_valid				     *
 *===========================================================================*/
int
fdt_valid(const void *dtb)
{
	if (dtb == NULL)
		return 0;

	if (fdt_be32((const char *)dtb + FDT_OFF_MAGIC) != FDT_MAGIC)
		return 0;

	/*
	 * A blob newer than this reader is still readable as long as it says
	 * so itself: last_comp_version is the oldest reader its author
	 * believes can cope.
	 */
	if (fdt_be32((const char *)dtb + FDT_OFF_LAST_COMP_VER) >
	    FDT_SUPPORTED_VERSION)
		return 0;

	return 1;
}

/*===========================================================================*
 *				fdt_size				     *
 *===========================================================================*/
size_t
fdt_size(const void *dtb)
{
	return (size_t)fdt_be32((const char *)dtb + FDT_OFF_TOTALSIZE);
}

/*===========================================================================*
 *				fdt_walk				     *
 *===========================================================================*/
int
fdt_walk(const void *dtb, fdt_node_cb cb, void *cookie)
{
	const char *base = dtb;
	const char *p, *end;
	int depth = -1;

	if (!fdt_valid(dtb))
		return -1;

	p = base + fdt_be32(base + FDT_OFF_DT_STRUCT);
	end = p + fdt_be32(base + FDT_OFF_SIZE_DT_STRUCT);

	while (p + 4 <= end) {
		u32_t token = fdt_be32(p);

		p += 4;

		switch (token) {
		case FDT_BEGIN_NODE: {
			struct fdt_node node;
			const char *name = p;
			int r;

			p += fdt_align4((unsigned)strlen(name) + 1);
			if (p > end)
				return -1;

			if (++depth >= FDT_MAX_DEPTH)
				return -1;

			/*
			 * Properties always precede child nodes, so the token
			 * after the name is where this node's properties
			 * begin and fdt_getprop() can start there.
			 */
			node.dtb = dtb;
			node.props = p;

			if ((r = cb(cookie, depth, name, &node)) != 0)
				return r;
			break;
		}

		case FDT_END_NODE:
			if (--depth < -1)
				return -1;
			break;

		case FDT_PROP: {
			u32_t len;

			if (p + 8 > end)
				return -1;
			len = fdt_be32(p);
			p += 8 + fdt_align4(len);
			break;
		}

		case FDT_NOP:
			break;

		case FDT_END:
			return 0;

		default:
			/* Not a token: the blob is not what it claims. */
			return -1;
		}
	}

	return -1;
}

/*===========================================================================*
 *				fdt_getprop				     *
 *===========================================================================*/
const void *
fdt_getprop(const struct fdt_node *node, const char *name, unsigned *len)
{
	const char *base = node->dtb;
	const char *strings = base + fdt_be32(base + FDT_OFF_DT_STRINGS);
	const char *p = node->props;

	for (;;) {
		u32_t token = fdt_be32(p);
		u32_t plen, nameoff;

		p += 4;

		if (token == FDT_NOP)
			continue;

		/*
		 * Anything else ends this node's property list: a child node
		 * begins, this node ends, or the tree does.
		 */
		if (token != FDT_PROP)
			return NULL;

		plen = fdt_be32(p);
		nameoff = fdt_be32(p + 4);
		p += 8;

		if (strcmp(strings + nameoff, name) == 0) {
			if (len != NULL)
				*len = plen;
			return p;
		}

		p += fdt_align4(plen);
	}
}

/*===========================================================================*
 *				fdt_read_cells				     *
 *===========================================================================*/
u64_t
fdt_read_cells(const void *data, unsigned cells)
{
	const char *p = data;
	u64_t v = 0;
	unsigned i;

	for (i = 0; i < cells; i++)
		v = (v << 32) | fdt_be32(p + 4 * i);

	return v;
}
