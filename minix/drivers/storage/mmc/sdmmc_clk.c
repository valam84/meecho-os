/*
 * Clocks and resets: whatever holds them, asked through the tree.
 *
 * A host controller has two kinds of knowledge in it. One is its own register
 * set, which is the same on every board that has the part. The other is how
 * the SoC around it feeds it a clock and holds its reset lines, which is not
 * the part's at all: the same Synopsys block takes its clock from a Rockchip
 * CRU on one SoC and from an Exynos clock controller on another. The first
 * version of this driver had the second kind inside sdmmc_sdhci.c - a CRU
 * register offset, a multiplexer table, Rockchip's write-mask convention -
 * which made the SDHCI file an SDHCI file for one SoC.
 *
 * So the line is drawn where the tree draws it. A device names its clocks
 * and resets as <phandle specifier> pairs; the phandle is a node with a
 * compatible string, and that string picks a sdmmc_clkctl_ops out of the
 * table below, exactly as sdmmc_host.c picks a host. Adding a SoC is a file
 * beside sdmmc_clk_rk3568.c and a line here. A machine whose controller is
 * not in the table still works - its lines are SDMMC_NO_LINE, every request
 * answers ENOSYS, and the clocks the bootloader set are the ones in use.
 *
 * This is the third copy of the same idea on this port - dwmac_rk.c and
 * xhci_rk.c each speak to the same CRU for themselves - and the first one
 * written as a table rather than as one SoC. It lives with this driver until
 * a second driver wants it; then it is a library, and the move is a Makefile
 * line, because nothing in it knows it is serving an SD controller.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sdmmc.h"

struct clkctl_driver {
	const char *compatible;
	const struct sdmmc_clkctl_ops *ops;
};

static const struct clkctl_driver clkctl_drivers[] = {
	{ "rockchip,rk3568-cru",	&sdmmc_rk3568_cru },
};

/*
 * The controllers this device's lines point at. A controller is entered once
 * however many lines it holds - the eMMC here has five clocks and five resets
 * and all ten are in one CRU - so the number is small; four is generous.
 */
#define MAX_CTLS	4

static struct ctl {
	u32_t		phandle;
	phys_bytes	base;
	size_t		size;
	volatile uint8_t *regs;		/* NULL until the first operation */
	int		map_failed;
	const struct sdmmc_clkctl_ops *ops;
} ctls[MAX_CTLS];
static unsigned nctls;

/* What a walk for one phandle found. */
struct phandle_search {
	u32_t		phandle;
	int		found;
	u64_t		base, size;
	u32_t		clock_cells;
	u32_t		reset_cells;
	const struct sdmmc_clkctl_ops *ops;
	const char     *compatible;	/* the table's string, for the log */
};

static u32_t
prop_u32(const struct fdt_node *node, const char *name, u32_t dflt)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, name, &len)) == NULL || len < 4)
		return dflt;
	return (u32_t)fdt_read_cells(p, 1);
}

static int
find_phandle(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct phandle_search *s = cookie;
	unsigned i;

	if (prop_u32(node, "phandle", 0) != s->phandle &&
	    prop_u32(node, "linux,phandle", 0) != s->phandle)
		return 0;

	s->found = 1;
	if (fdt_node_reg(node, 0, &s->base, &s->size) != 0)
		s->base = s->size = 0;
	/*
	 * A missing #clock-cells is a malformed tree rather than a zero, but
	 * zero is the reading that cannot walk off the end of a list: a list
	 * misparsed with too few cells per entry names the wrong lines, one
	 * misparsed with too many runs past its property.
	 */
	s->clock_cells = prop_u32(node, "#clock-cells", 0);
	s->reset_cells = prop_u32(node, "#reset-cells", 0);

	for (i = 0; i < sizeof(clkctl_drivers) / sizeof(clkctl_drivers[0]);
	    i++) {
		if (fdt_node_is_compatible(node, clkctl_drivers[i].compatible)) {
			s->ops = clkctl_drivers[i].ops;
			s->compatible = clkctl_drivers[i].compatible;
			break;
		}
	}
	return 1;
}

/* Find the controller a phandle names, entering it in ctls[] once. */
static int
ctl_lookup(const void *dtb, u32_t phandle, struct phandle_search *s)
{
	unsigned i;

	memset(s, 0, sizeof(*s));
	s->phandle = phandle;
	(void)fdt_walk(dtb, find_phandle, s);
	if (!s->found)
		return SDMMC_NO_LINE;

	for (i = 0; i < nctls; i++)
		if (ctls[i].phandle == phandle)
			return (ctls[i].ops != NULL) ? (int)i : SDMMC_NO_LINE;

	if (s->ops == NULL || s->base == 0 || s->size == 0)
		return SDMMC_NO_LINE;
	if (nctls == MAX_CTLS) {
		log_warn(&sdmmc_log, "more than %u clock controllers; the rest "
		    "are left as the firmware set them\n", MAX_CTLS);
		return SDMMC_NO_LINE;
	}

	ctls[nctls].phandle = phandle;
	ctls[nctls].base = (phys_bytes)s->base;
	ctls[nctls].size = (size_t)s->size;
	ctls[nctls].regs = NULL;
	ctls[nctls].map_failed = 0;
	ctls[nctls].ops = s->ops;
	log_debug(&sdmmc_log, "clock controller %s at 0x%lx\n", s->compatible,
	    (unsigned long)s->base);
	return (int)nctls++;
}

/*
 * Which position the names property gives a name, or -1. The property is a
 * sequence of NUL-terminated strings, in the same order as the list.
 */
static int
name_index(const struct fdt_node *node, const char *names, const char *name)
{
	const char *p;
	unsigned len, off, i;

	if ((p = fdt_getprop(node, names, &len)) == NULL)
		return -1;

	for (off = 0, i = 0; off < len; i++) {
		size_t n = strnlen(p + off, len - off);

		if (n == strlen(name) && strncmp(p + off, name, n) == 0)
			return (int)i;
		off += (unsigned)n + 1;
	}
	return -1;
}

int
sdmmc_line_get(const struct fdt_node *node, const char *list,
	const char *names, const char *name, unsigned index,
	struct sdmmc_line *line)
{
	struct phandle_search s;
	const uint8_t *p;
	unsigned len, off, i;
	int is_reset = (strcmp(list, "resets") == 0);

	line->ctl = SDMMC_NO_LINE;
	line->id = 0;

	if (name != NULL) {
		int at = name_index(node, names, name);

		if (at < 0)
			return ENOENT;
		index = (unsigned)at;
	}

	if ((p = fdt_getprop(node, list, &len)) == NULL)
		return ENOENT;

	/*
	 * Entries are not of one width: each is a phandle followed by as
	 * many cells as the node it points at declares. So the list is walked
	 * from the start, looking each holder up, until the wanted entry.
	 */
	for (off = 0, i = 0; off + 4 <= len; i++) {
		u32_t phandle = (u32_t)fdt_read_cells(p + off, 1);
		u32_t cells;
		int ctl;

		ctl = ctl_lookup(node->dtb, phandle, &s);
		if (!s.found) {
			log_warn(&sdmmc_log, "%s: entry %u points at phandle "
			    "%u, which no node carries\n", list, i, phandle);
			return EINVAL;
		}
		cells = is_reset ? s.reset_cells : s.clock_cells;
		if (off + 4 + 4 * cells > len)
			return EINVAL;

		if (i == index) {
			/*
			 * One cell is the binding of every controller this
			 * driver knows; zero means the holder has one line.
			 * Anything wider is a binding nothing here has met.
			 */
			if (cells > 1)
				return ENOTSUP;
			line->id = (cells == 1) ?
			    (unsigned)fdt_read_cells(p + off + 4, 1) : 0;
			line->ctl = ctl;
			return (ctl == SDMMC_NO_LINE) ? ENOTSUP : OK;
		}
		off += 4 + 4 * cells;
	}
	return ENOENT;
}

int
sdmmc_clk_get(const struct fdt_node *node, const char *name,
	struct sdmmc_line *line)
{
	return sdmmc_line_get(node, "clocks", "clock-names", name, 0, line);
}

/*
 * The controller behind a line, mapped. Mapping waits for the first use, as
 * the host's own registers wait for init: the walk that found the line runs
 * before anything is mapped, and a line that is never used costs nothing.
 */
static struct ctl *
ctl_of(const struct sdmmc_line *line)
{
	struct ctl *c;
	void *v;

	if (line == NULL || line->ctl < 0 || (unsigned)line->ctl >= nctls)
		return NULL;
	c = &ctls[line->ctl];
	if (c->regs != NULL)
		return c;
	if (c->map_failed)
		return NULL;

	v = vm_map_phys(SELF, (void *)c->base, c->size);
	if (v == MAP_FAILED) {
		/*
		 * Once, and loudly: this is "the system.conf line is
		 * missing", and every operation after it is a silent
		 * fallback to the firmware's settings.
		 */
		log_warn(&sdmmc_log, "cannot map the clock controller at 0x%lx; "
		    "not granted by RS? its settings stay as the firmware left "
		    "them\n", (unsigned long)c->base);
		c->map_failed = 1;
		return NULL;
	}
	c->regs = v;
	return c;
}

int
sdmmc_clk_enable(const struct sdmmc_line *line)
{
	struct ctl *c = ctl_of(line);

	if (c == NULL || c->ops->enable == NULL)
		return ENOSYS;
	return c->ops->enable(c->regs, c->size, line->id);
}

int
sdmmc_clk_set_rate(const struct sdmmc_line *line, uint32_t hz, uint32_t *got)
{
	struct ctl *c = ctl_of(line);

	if (c == NULL || c->ops->set_rate == NULL)
		return ENOSYS;
	return c->ops->set_rate(c->regs, c->size, line->id, hz, got);
}

int
sdmmc_clk_set_phase(const struct sdmmc_line *line, unsigned degrees)
{
	struct ctl *c = ctl_of(line);

	if (c == NULL || c->ops->set_phase == NULL)
		return ENOSYS;
	return c->ops->set_phase(c->regs, c->size, line->id, degrees % 360);
}

int
sdmmc_reset_set(const struct sdmmc_line *line, int asserted)
{
	struct ctl *c = ctl_of(line);

	if (c == NULL || c->ops->reset == NULL)
		return ENOSYS;
	return c->ops->reset(c->regs, c->size, line->id, asserted);
}

/*
 * Every line down, a pause, every line up, and a longer pause.
 *
 * All together rather than one at a time, because the lines of one block
 * guard its halves against each other: releasing the bus interface while the
 * core is still held is a state nobody designed for. The pauses are the ones
 * this driver always used; Linux's reset framework spends about the same.
 */
void
sdmmc_reset_pulse(const struct sdmmc_line *lines, unsigned n)
{
	unsigned i, done = 0;

	for (i = 0; i < n; i++)
		if (sdmmc_reset_set(&lines[i], 1) == OK)
			done++;
	if (done == 0)
		return;

	micro_delay(10);

	for (i = 0; i < n; i++)
		(void)sdmmc_reset_set(&lines[i], 0);

	micro_delay(100);
}
