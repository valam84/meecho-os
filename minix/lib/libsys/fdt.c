/*
 * Flattened device tree reader.
 *
 * The format is the Devicetree Specification's version 17 blob: a header, a
 * block of tokens describing the tree, and a block of NUL-terminated property
 * names the tokens index into.
 *
 * Two properties of this code matter more than its size. It is linked into
 * the kernel as well as into services, and the kernel runs it before the MMU
 * is on, so it allocates nothing and keeps no state between calls - the
 * caller passes the blob's address every time, which is the physical one
 * early and the kernel-virtual one later. And it reads every number a byte at
 * a time: the blob is big-endian and this system is not, and nothing
 * guarantees the alignment of a property's data. Byte assembly settles both
 * questions without an assumption to get wrong.
 *
 * fdt_fetch(), the one function here that needs a kernel call, lives in
 * fdt_fetch.c so that this object stays free of references the kernel cannot
 * satisfy.
 */

#include <string.h>

#include <minix/fdt.h>

/* Header, all fields big-endian 32-bit, at these byte offsets. */
#define FDT_OFF_MAGIC		0
#define FDT_OFF_TOTALSIZE	4
#define FDT_OFF_DT_STRUCT	8
#define FDT_OFF_DT_STRINGS	12
#define FDT_OFF_MEM_RSVMAP	16
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
 *				fdt_memreserve				     *
 *===========================================================================*/
/*
 * The blob's memory reservation block: pairs of 64-bit big-endian numbers,
 * address then size, ending with a pair of zeroes. It sits outside the tree
 * proper, which is why it needs a call of its own rather than a walk.
 *
 * This is where firmware puts the memory an operating system must not use and
 * cannot see any other way - on the CB2 the BL31 that TF-A left running below
 * the kernel. /reserved-memory in the tree says the same kind of thing in the
 * newer spelling, and a machine may use either or both.
 */
int
fdt_memreserve(const void *dtb, unsigned index, u64_t *addr, u64_t *size)
{
	const char *base = dtb;
	const char *p;
	u64_t a, s;

	p = base + fdt_be32(base + FDT_OFF_MEM_RSVMAP) + (size_t)index * 16;

	/* Reading a 64-bit field as two 32-bit halves, for the alignment
	 * reason the file comment gives. */
	a = ((u64_t)fdt_be32(p) << 32) | fdt_be32(p + 4);
	s = ((u64_t)fdt_be32(p + 8) << 32) | fdt_be32(p + 12);

	if (a == 0 && s == 0)
		return -1;

	*addr = a;
	*size = s;

	return 0;
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
	/*
	 * The cell widths in force for the children of the node at each
	 * depth. Slot 0 is what the root's children would get from a parent
	 * that said nothing, which is what the specification's defaults are
	 * for.
	 */
	unsigned acells[FDT_MAX_DEPTH + 1], scells[FDT_MAX_DEPTH + 1];

	if (!fdt_valid(dtb))
		return -1;

	acells[0] = 2;
	scells[0] = 1;

	p = base + fdt_be32(base + FDT_OFF_DT_STRUCT);
	end = p + fdt_be32(base + FDT_OFF_SIZE_DT_STRUCT);

	while (p + 4 <= end) {
		u32_t token = fdt_be32(p);

		p += 4;

		switch (token) {
		case FDT_BEGIN_NODE: {
			struct fdt_node node;
			const char *name = p;
			const void *prop;
			unsigned len;
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
			node.addr_cells = acells[depth];
			node.size_cells = scells[depth];

			/* What this node's own children will read reg with. */
			acells[depth + 1] = 2;
			scells[depth + 1] = 1;
			if ((prop = fdt_getprop(&node, "#address-cells",
			    &len)) != NULL && len == 4)
				acells[depth + 1] =
				    (unsigned)fdt_read_cells(prop, 1);
			if ((prop = fdt_getprop(&node, "#size-cells",
			    &len)) != NULL && len == 4)
				scells[depth + 1] =
				    (unsigned)fdt_read_cells(prop, 1);

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

/*===========================================================================*
 *				fdt_node_is_compatible			     *
 *===========================================================================*/
int
fdt_node_is_compatible(const struct fdt_node *node, const char *want)
{
	const char *list;
	unsigned len, off;

	if ((list = fdt_getprop(node, "compatible", &len)) == NULL)
		return 0;

	for (off = 0; off < len; off += strlen(list + off) + 1)
		if (strcmp(list + off, want) == 0)
			return 1;

	return 0;
}

/*===========================================================================*
 *				fdt_gic_intid				     *
 *===========================================================================*/
int
fdt_gic_intid(const void *triplet)
{
	/*
	 * The GIC binding's three cells are <type number flags>. Type 0 is a
	 * shared peripheral interrupt, numbered from line 32; type 1 a
	 * private one, numbered from line 16. The flags say edge or level
	 * and are the controller's business, not the caller's.
	 */
	u32_t type = (u32_t)fdt_read_cells(triplet, 1);
	u32_t number = (u32_t)fdt_read_cells((const char *)triplet + 4, 1);

	switch (type) {
	case 0:
		return (int)(32 + number);
	case 1:
		return (int)(16 + number);
	default:
		return -1;
	}
}

/*===========================================================================*
 *				fdt_node_reg				     *
 *===========================================================================*/
int
fdt_node_reg(const struct fdt_node *node, unsigned index, u64_t *addr,
	u64_t *size)
{
	const char *p;
	unsigned len, step;

	/* Wider than two cells does not fit a 64-bit number. */
	if (node->addr_cells == 0 || node->addr_cells > 2 ||
	    node->size_cells > 2)
		return -1;

	if ((p = fdt_getprop(node, "reg", &len)) == NULL)
		return -1;

	step = 4 * (node->addr_cells + node->size_cells);
	if ((index + 1) * step > len)
		return -1;

	p += index * step;
	*addr = fdt_read_cells(p, node->addr_cells);
	*size = node->size_cells ?
	    fdt_read_cells(p + 4 * node->addr_cells, node->size_cells) : 0;

	return 0;
}


/*===========================================================================*
 *				fdt_phandle_reg				     *
 *===========================================================================*/
struct phandle_search {
	u32_t want;
	unsigned index;
	u64_t addr;
	u64_t size;
	int found;
};

static int
match_phandle(void *cookie, int depth, const char *name __unused,
	const struct fdt_node *node)
{
	struct phandle_search *s = cookie;
	const void *p;
	unsigned len;

	if (depth == 0)
		return 0;

	/*
	 * Both spellings: "phandle" is what a tree compiled this decade
	 * says, "linux,phandle" what an older one says, and a tree that
	 * carries both carries the same value in both.
	 */
	if ((p = fdt_getprop(node, "phandle", &len)) == NULL || len < 4)
		if ((p = fdt_getprop(node, "linux,phandle", &len)) == NULL ||
		    len < 4)
			return 0;

	if ((u32_t)fdt_read_cells(p, 1) != s->want)
		return 0;

	if (fdt_node_reg(node, s->index, &s->addr, &s->size) != 0)
		return 0;

	s->found = 1;
	return 1;			/* stops the walk */
}

int
fdt_phandle_reg(const void *dtb, u32_t phandle, unsigned index, u64_t *addr,
	u64_t *size)
{
	struct phandle_search s;

	/* Zero is not a phandle; it is what an absent one reads as. */
	if (phandle == 0)
		return -1;

	memset(&s, 0, sizeof(s));
	s.want = phandle;
	s.index = index;

	(void)fdt_walk(dtb, match_phandle, &s);

	if (!s.found)
		return -1;

	*addr = s.addr;
	*size = s.size;
	return 0;
}

/*===========================================================================*
 *				fdt_node_gic_irq			     *
 *===========================================================================*/
int
fdt_node_gic_irq(const struct fdt_node *node, unsigned index)
{
	const char *p;
	unsigned len;

	if ((p = fdt_getprop(node, "interrupts", &len)) == NULL)
		return -1;

	if ((index + 1) * 12 > len)
		return -1;

	return fdt_gic_intid(p + index * 12);
}
