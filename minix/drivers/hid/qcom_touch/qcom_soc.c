/*
 * TLMM pins and GCC branch clocks of a Qualcomm SoC (qcom_soc.h).
 *
 * TLMM: every GPIO has four registers at tile + 0x1000 * gpio (CFG,
 * IN_OUT, INTR_CFG, INTR_STATUS); which of the three tiles - south, center,
 * north, by the node's reg-names - comes from the SoC's table.  CFG holds
 * the pull (1:0), the mux (5:2), the drive (8:6, mA/2 - 1) and output
 * enable (9); only those fields are ever changed.
 *
 * GCC: a branch is on when its CBCR says so - CLK_OFF (31) clear, or the
 * NOC state (30:28) "on" - after its enable bit was set: the CBCR's own bit
 * 0, or a bit in the vote register for the "voted" ones the boot processor
 * shares with others.  A root (RCG) is set to the crystal by source 0,
 * divider field 1 in CFG_RCGR and the update bit in CMD_RCGR.
 *
 * Everything is read-modify-write of the fields named; nothing else in
 * either block is touched.
 */

#include <minix/drivers.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <string.h>

#include "qcom_soc.h"
#include "sdm660_tlmm.h"
#include "sdm660_gcc.h"

#define TLMM_STRIDE	0x1000
#define T_CFG		0x0
#define T_IN_OUT	0x4

#define CFG_PULL_MASK	0x003
#define CFG_FUNC_SHIFT	2
#define CFG_FUNC_MASK	0x03c
#define CFG_DRV_SHIFT	6
#define CFG_DRV_MASK	0x1c0
#define CFG_OE		0x200

#define IO_IN		0x1
#define IO_OUT		0x2

#define CBCR_ENABLE	0x00000001u
#define CBCR_OFF	0x80000000u
#define CBCR_NOC(v)	(((v) >> 28) & 7)
#define NOC_ON		2

#define RCG_CMD_UPDATE	0x00000001u
#define RCG_CFG_FIELDS	0x0010371fu	/* 4:0, 10:8, 13:12, 20 */

static volatile uint8_t *tile[3];
static size_t tile_size[3];
static volatile uint8_t *gcc;
static size_t gcc_size;

static uint32_t
rd32(volatile uint8_t *b, size_t off)
{
	return *(volatile uint32_t *)(b + off);
}

static void
wr32(volatile uint8_t *b, size_t off, uint32_t v)
{
	*(volatile uint32_t *)(b + off) = v;
}

/*===========================================================================*
 *    finding the blocks                                                      *
 *===========================================================================*/
struct soc_find {
	int tlmm, gcc;
	u64_t tile_pa[3], tile_sz[3], gcc_pa, gcc_sz;
};

static int
find_soc(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct soc_find *f = cookie;
	const char *names, *p;
	unsigned len, idx, t;
	u64_t a, sz;

	if (!f->tlmm && (fdt_node_is_compatible(node, "qcom,sdm660-pinctrl") ||
	    fdt_node_is_compatible(node, "qcom,sdm630-pinctrl")) &&
	    (names = fdt_getprop(node, "reg-names", &len)) != NULL) {
		for (p = names, idx = 0; p < names + len;
		    p += strlen(p) + 1, idx++)
			for (t = 0; t < 3; t++)
				if (strcmp(p, sdm660_tile_name[t]) == 0 &&
				    fdt_node_reg(node, idx, &a, &sz) == 0) {
					f->tile_pa[t] = a;
					f->tile_sz[t] = sz;
				}
		f->tlmm = f->tile_sz[0] && f->tile_sz[1] && f->tile_sz[2];
	}
	if (!f->gcc && (fdt_node_is_compatible(node, "qcom,gcc-sdm660") ||
	    fdt_node_is_compatible(node, "qcom,gcc-sdm630")) &&
	    fdt_node_reg(node, 0, &f->gcc_pa, &f->gcc_sz) == 0)
		f->gcc = 1;
	return f->tlmm && f->gcc;
}

static volatile uint8_t *
map(u64_t pa, u64_t sz)
{
	void *v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)sz);

	return v == MAP_FAILED ? NULL : v;
}

int
qsoc_init(const void *dtb)
{
	struct soc_find f;
	unsigned t;

	memset(&f, 0, sizeof(f));
	fdt_walk(dtb, find_soc, &f);
	if (!f.tlmm || !f.gcc)
		return ENXIO;
	for (t = 0; t < 3; t++) {
		if ((tile[t] = map(f.tile_pa[t], f.tile_sz[t])) == NULL)
			return EPERM;
		tile_size[t] = (size_t)f.tile_sz[t];
	}
	if ((gcc = map(f.gcc_pa, f.gcc_sz)) == NULL)
		return EPERM;
	gcc_size = (size_t)f.gcc_sz;
	return OK;
}

/*===========================================================================*
 *    pins                                                                    *
 *===========================================================================*/
static volatile uint8_t *
pin_regs(unsigned pin)
{
	unsigned t;

	if (pin >= SDM660_NGPIO)
		return NULL;
	t = sdm660_gpio[pin].tile;
	if (tile[t] == NULL || (size_t)(pin + 1) * TLMM_STRIDE > tile_size[t])
		return NULL;
	return tile[t] + (size_t)pin * TLMM_STRIDE;
}

int
qsoc_func(unsigned pin, const char *name)
{
	unsigned i;

	if (pin >= SDM660_NGPIO || name == NULL)
		return -1;
	if (strcmp(name, "gpio") == 0)
		return 0;
	for (i = 0; i < 9; i++)
		if (sdm660_gpio[pin].func[i] != NULL &&
		    strcmp(sdm660_gpio[pin].func[i], name) == 0)
			return (int)i + 1;
	return -1;
}

int
qsoc_pin(unsigned pin, int func, unsigned drive_ma, unsigned pull,
	int output)
{
	volatile uint8_t *r = pin_regs(pin);
	uint32_t v, drv;

	if (r == NULL || func < 0 || func > 9 || pull > 3)
		return EINVAL;
	if (drive_ma < 2)
		drive_ma = 2;
	if (drive_ma > 16)
		drive_ma = 16;
	drv = drive_ma / 2 - 1;
	v = rd32(r, T_CFG);
	v &= ~(CFG_PULL_MASK | CFG_FUNC_MASK | CFG_DRV_MASK | CFG_OE);
	v |= pull | (uint32_t)func << CFG_FUNC_SHIFT | drv << CFG_DRV_SHIFT;
	if (output)
		v |= CFG_OE;
	wr32(r, T_CFG, v);
	return OK;
}

void
qsoc_pin_set(unsigned pin, int high)
{
	volatile uint8_t *r = pin_regs(pin);
	uint32_t v;

	if (r == NULL)
		return;
	v = rd32(r, T_IN_OUT);
	v = high ? (v | IO_OUT) : (v & ~IO_OUT);
	wr32(r, T_IN_OUT, v);
}

int
qsoc_pin_get(unsigned pin)
{
	volatile uint8_t *r = pin_regs(pin);

	return r != NULL && (rd32(r, T_IN_OUT) & IO_IN) != 0;
}

uint32_t
qsoc_phandle(const struct fdt_node *node)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, "phandle", &len)) == NULL &&
	    (p = fdt_getprop(node, "linux,phandle", &len)) == NULL)
		return 0;
	return len == 4 ? (uint32_t)fdt_read_cells(p, 1) : 0;
}

/*
 * A pin configuration node, applied: the node with the phandle, and its
 * children if it has them (Linux allows both forms).
 */
struct pc_walk {
	uint32_t phandle;
	int in, depth, applied, err;
};

static void
apply(const struct fdt_node *node, struct pc_walk *w)
{
	const char *pins, *p, *fn;
	unsigned len, plen, drive = 2, pull = QSOC_PULL_NONE, pin;
	const void *d;
	int func;

	if ((pins = fdt_getprop(node, "pins", &plen)) == NULL)
		return;
	fn = fdt_getprop(node, "function", &len);
	if ((d = fdt_getprop(node, "drive-strength", &len)) != NULL &&
	    len == 4)
		drive = (unsigned)fdt_read_cells(d, 1);
	if (fdt_getprop(node, "bias-pull-up", &len) != NULL)
		pull = QSOC_PULL_UP;
	else if (fdt_getprop(node, "bias-pull-down", &len) != NULL)
		pull = QSOC_PULL_DOWN;
	else if (fdt_getprop(node, "bias-bus-hold", &len) != NULL)
		pull = QSOC_PULL_KEEPER;
	for (p = pins; p < pins + plen; p += strlen(p) + 1) {
		if (strncmp(p, "gpio", 4) != 0) {
			w->err = EINVAL;	/* sdc/qdsd groups: not ours */
			continue;
		}
		pin = (unsigned)strtoul(p + 4, NULL, 10);
		func = qsoc_func(pin, fn != NULL ? fn : "gpio");
		if (func < 0 || qsoc_pin(pin, func, drive, pull, 0) != OK) {
			w->err = EINVAL;
			continue;
		}
		w->applied++;
	}
}

static int
pc_cb(void *cookie, int depth, const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct pc_walk *w = cookie;

	if (w->in && depth <= w->depth)
		return 1;			/* left the node: done */
	if (!w->in) {
		if (qsoc_phandle(node) != w->phandle)
			return 0;
		w->in = 1;
		w->depth = depth;
	}
	apply(node, w);
	return 0;
}

int
qsoc_pinctrl(const void *dtb, uint32_t phandle)
{
	struct pc_walk w;

	memset(&w, 0, sizeof(w));
	w.phandle = phandle;
	fdt_walk(dtb, pc_cb, &w);
	if (w.err != 0)
		return w.err;
	return w.applied > 0 ? OK : ENOENT;
}

/*===========================================================================*
 *    clocks                                                                  *
 *===========================================================================*/
static int
branch_on(uint32_t v)
{
	return !(v & CBCR_OFF) || CBCR_NOC(v) == NOC_ON;
}

int
qsoc_clk_enable(unsigned id, uint32_t hz)
{
	unsigned i, t;
	uint32_t v;

	for (i = 0; i < sizeof(sdm660_gcc) / sizeof(sdm660_gcc[0]); i++)
		if (sdm660_gcc[i].id == id)
			break;
	if (i == sizeof(sdm660_gcc) / sizeof(sdm660_gcc[0]) || gcc == NULL)
		return ENOENT;
	if (sdm660_gcc[i].en + 4 > gcc_size ||
	    sdm660_gcc[i].halt + 4 > gcc_size ||
	    sdm660_gcc[i].rcg + 8 > gcc_size)
		return EINVAL;

	/* The root at the crystal, if asked and it has a root of its own. */
	if (sdm660_gcc[i].rcg != 0 && hz == 19200000) {
		v = rd32(gcc, sdm660_gcc[i].rcg + 4);
		v = (v & ~RCG_CFG_FIELDS) | (0u << 8) | 1u;
		wr32(gcc, sdm660_gcc[i].rcg + 4, v);
		wr32(gcc, sdm660_gcc[i].rcg, rd32(gcc, sdm660_gcc[i].rcg) |
		    RCG_CMD_UPDATE);
		for (t = 0; t < 500 && (rd32(gcc, sdm660_gcc[i].rcg) &
		    RCG_CMD_UPDATE); t++)
			micro_delay(1);
		if (rd32(gcc, sdm660_gcc[i].rcg) & RCG_CMD_UPDATE)
			return ETIMEDOUT;
	}

	wr32(gcc, sdm660_gcc[i].en, rd32(gcc, sdm660_gcc[i].en) |
	    (1u << sdm660_gcc[i].bit));
	if (sdm660_gcc[i].nocheck || sdm660_gcc[i].halt == 0)
		return OK;
	for (t = 0; t < 200; t++) {
		if (branch_on(rd32(gcc, sdm660_gcc[i].halt)))
			return OK;
		micro_delay(1);
	}
	return ETIMEDOUT;
}
