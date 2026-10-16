/*
 * dwc3_ecm's probe, run on the Mi Pad 4's real device tree.
 *
 * dwc3_ecm.c is included whole and dwc3_init() runs as on the tablet: the
 * tree is the mainline sdm660-xiaomi-clover.dtb (built by
 * port/clover/mkboot.sh; GPL-2.0-only, so not kept in this tree - the path
 * is the first argument), read by libsys's own fdt.c.  The GCC, the DWC3
 * core, Qualcomm's wrapper (QSCRATCH), the QUSB2 PHY and the QFPROM are
 * plain memory.  What is checked:
 *
 *   the tree      what dwc3_find() read, against values written down from
 *                 the tree's source separately (sdm630.dtsi, the clover
 *                 dtsi): addresses, the interrupt, the clocks with the
 *                 GCC's names for them, resets, the power domain, the
 *                 supplies, the fuse cell
 *   the guard     for six states of the GCC, which blocks were mapped at
 *                 all: none of the wrapper, core or PHY unless the power
 *                 domain, resets and clocks say they may be read
 *   no writes     after every run, every model byte for byte as before -
 *                 the milestone's promise
 *   bad trees     a tree without the wrapper, with the core naming no
 *                 usb2-phy, or with a PHY this driver does not know is
 *                 refused, and nothing is mapped
 */
#include <sys/mman.h>

typedef struct { int m_type; } message;
#define ANY	0x7ace
int sef_receive_status(int src, message *m, int *status);

#define main dwc3_main
#include "dwc3_ecm.c"
#undef main

#include "sdm660_gcc.h"
#include "dwc3_regs.h"

int stub_log_level = 2;			/* info: the lines the tablet shows */
static const char *dtb_path;
static unsigned checks, failures;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/* The blocks, at the tree's addresses. */
static uint8_t gccmem[0x94000] __attribute__((aligned(8)));
static uint8_t coremem[0xd000] __attribute__((aligned(8)));
static uint8_t qsmem[0x400] __attribute__((aligned(8)));
static uint8_t phymem[0x180] __attribute__((aligned(8)));
static uint8_t fusemem[0x621c] __attribute__((aligned(8)));
static struct region {
	unsigned long pa;
	size_t size;
	uint8_t *mem;
	const char *name;
	int mapped;
	uint8_t *snap;
} reg[] = {
	{ 0x00100000, sizeof(gccmem), gccmem, "gcc", 0, NULL },
	{ 0x0a800000, 0xc8d0, coremem, "core", 0, NULL },
	{ 0x0a8f8800, sizeof(qsmem), qsmem, "qscratch", 0, NULL },
	{ 0x0c012000, sizeof(phymem), phymem, "phy", 0, NULL },
	{ 0x00780000, sizeof(fusemem), fusemem, "qfprom", 0, NULL },
};
#define NREG	(sizeof(reg) / sizeof(reg[0]))
enum { GCC, CORE, QS, PHY, FUSE };
static unsigned unknown_maps;

void *
vm_map_phys(int who, void *phys, size_t len)
{
	unsigned long pa = (unsigned long)phys;
	unsigned i;

	(void)who;
	for (i = 0; i < NREG; i++)
		if (pa == reg[i].pa && len <= reg[i].size) {
			reg[i].mapped = 1;
			return reg[i].mem;
		}
	unknown_maps++;
	printf("  (map of 0x%lx+0x%zx: not a block of this stand)\n", pa, len);
	return MAP_FAILED;
}

int vm_unmap_phys(int who, void *vir, size_t len)
{ (void)who; (void)vir; (void)len; return 0; }
void micro_delay(unsigned long us) { (void)us; }

/* Byte replacements applied to the tree as it is fetched: bad trees. */
static const char *patch_from, *patch_to;

void *
fdt_fetch(void)
{
	FILE *f = fopen(dtb_path, "rb");
	long n, i;
	size_t pl;
	uint8_t *buf;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc((size_t)n);
	if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf != NULL && patch_from != NULL) {
		pl = strlen(patch_from) + 1;	/* the NUL too: whole strings */
		for (i = 0; i + (long)pl <= n; i++)
			if (memcmp(buf + i, patch_from, pl) == 0)
				memcpy(buf + i, patch_to, pl);
	}
	return buf;
}

void default_log(void) { }
int env_parse(const char *n, const char *f, int fl, long *p, long lo, long hi)
{ (void)n; (void)f; (void)fl; (void)p; (void)lo; (void)hi; return 0; }
void env_setargs(int argc, char **argv) { (void)argc; (void)argv; }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { (void)cb; }
void sef_setcb_signal_handler(void (*cb)(int)) { (void)cb; }
void sef_startup(void) { }
int sef_receive_status(int s, message *m, int *st)
{ (void)s; (void)m; (void)st; return 0; }
int sys_setalarm(clock_t t, int a) { (void)t; (void)a; return 0; }
clock_t sys_hz(void) { return 100; }

/*===========================================================================*
 *    the GCC's states                                                        *
 *===========================================================================*/
static void
w32(uint8_t *m, unsigned off, uint32_t v)
{
	memcpy(m + off, &v, 4);
}

/* Every branch's CBCR on (CLK_OFF clear) or off; GDSCs and BCRs as said. */
static void
gcc_state(int clocks_on, int gdsc_on)
{
	unsigned i;

	memset(gccmem, 0, sizeof(gccmem));
	for (i = 0; i < sizeof(sdm660_gcc) / sizeof(sdm660_gcc[0]); i++)
		if (sdm660_gcc[i].halt != 0)
			w32(gccmem, sdm660_gcc[i].halt,
			    clocks_on ? 0u : 0x80000000u);
	for (i = 0; i < sizeof(sdm660_gdsc) / sizeof(sdm660_gdsc[0]); i++)
		w32(gccmem, sdm660_gdsc[i].gdscr, gdsc_on ? 0x80000000u : 0u);
	/* The USB master root as the table would set it: GPLL0 / 5. */
	w32(gccmem, 0xf014 + 4, 0x109);
}

static void
blocks(void)
{
	memset(coremem, 0, sizeof(coremem));
	w32(coremem, DWC3_GSNPSID, 0x5533310a);
	w32(coremem, DWC3_GCTL, 0x2000);	/* device */
	memset(qsmem, 0, sizeof(qsmem));
	memset(phymem, 0, sizeof(phymem));
	w32(phymem, 0x38, 0x20);		/* PLL locked */
	memset(fusemem, 0, sizeof(fusemem));
	fusemem[0x243] = 0x0a;			/* bits 1..3: 5 */
}

static void
snapshot(void)
{
	unsigned i;

	for (i = 0; i < NREG; i++) {
		free(reg[i].snap);
		reg[i].snap = malloc(reg[i].size);
		memcpy(reg[i].snap, reg[i].mem, reg[i].size);
		reg[i].mapped = 0;
	}
	unknown_maps = 0;
}

static unsigned
changed(void)
{
	unsigned i, n = 0;

	for (i = 0; i < NREG; i++)
		if (memcmp(reg[i].snap, reg[i].mem, reg[i].size) != 0) {
			printf("  (%s changed)\n", reg[i].name);
			n++;
		}
	return n;
}

/*
 * One probe: the GCC as set up, then dwc3_init(); the expected result and
 * which of core, qscratch, phy may be mapped.
 */
static void
probe(const char *what, int want_r, int want_core, int want_phy)
{
	int r;

	printf("--- %s\n", what);
	snapshot();
	r = dwc3_init(0, NULL);
	CHECK(r == want_r, "%s: dwc3_init %d, expected %d", what, r, want_r);
	CHECK(reg[CORE].mapped == want_core && reg[QS].mapped == want_core,
	    "%s: core mapped %d, wrapper %d, expected %d", what,
	    reg[CORE].mapped, reg[QS].mapped, want_core);
	CHECK(reg[PHY].mapped == want_phy, "%s: phy mapped %d, expected %d",
	    what, reg[PHY].mapped, want_phy);
	CHECK(changed() == 0, "%s: a block was written", what);
	CHECK(unknown_maps == 0, "%s: %u maps of unknown addresses", what,
	    unknown_maps);
}

/*===========================================================================*
 *    the tree, as read                                                       *
 *===========================================================================*/
static const struct { uint32_t id; const char *dt, *gcc; } want_clk[] = {
	{ 49, "cfg_noc",   "GCC_CFG_NOC_USB3_AXI_CLK" },
	{ 91, "core",      "GCC_USB30_MASTER_CLK" },
	{ 21, "iface",     "GCC_AGGRE2_USB3_AXI_CLK" },
	{ 93, "sleep",     "GCC_USB30_SLEEP_CLK" },
	{ 92, "mock_utmi", "GCC_USB30_MOCK_UTMI_CLK" },
}, want_phyclk[] = {
	{ 97,  "cfg_ahb",  "GCC_USB_PHY_CFG_AHB2PHY_CLK" },
	{ 129, "ref",      "GCC_RX0_USB2_CLKREF_CLK" },
};

static void
tree_checks(void)
{
	const char *name;
	unsigned i;

	printf("--- the tree as read\n");
	CHECK(dt.gcc_ref == 0x27 && qsoc_gcc_phandle() == 0x27,
	    "GCC phandle %u/%u", dt.gcc_ref, qsoc_gcc_phandle());
	CHECK(strcmp(dt.glue_node, "usb@a8f8800") == 0, "wrapper %s",
	    dt.glue_node);
	CHECK(dt.qscratch_pa == 0xa8f8800 && dt.qscratch_sz == 0x400,
	    "qscratch 0x%llx+0x%llx", (unsigned long long)dt.qscratch_pa,
	    (unsigned long long)dt.qscratch_sz);
	CHECK(dt.core_pa == 0xa800000 && dt.core_sz == 0xc8d0,
	    "core 0x%llx+0x%llx", (unsigned long long)dt.core_pa,
	    (unsigned long long)dt.core_sz);
	CHECK(dt.irq == 32 + 131, "irq %d (SPI 131 is 163)", dt.irq);
	CHECK(strcmp(dt.dr_mode, "peripheral") == 0, "dr_mode %s", dt.dr_mode);
	CHECK(strcmp(dt.max_speed, "high-speed") == 0, "speed %s",
	    dt.max_speed);
	CHECK(dt.utmi_as_pipe == 1, "utmi as pipe %d", dt.utmi_as_pipe);
	CHECK(dt.nclk == 5, "%u clocks", dt.nclk);
	for (i = 0; i < 5 && i < dt.nclk; i++) {
		CHECK(dt.clk[i].id == want_clk[i].id &&
		    strcmp(dt.clk[i].name, want_clk[i].dt) == 0,
		    "clock %u: %u %s", i, dt.clk[i].id, dt.clk[i].name);
		CHECK(qsoc_clk_on(dt.clk[i].id, NULL, &name) >= 0 &&
		    qsoc_clk_on(dt.clk[i].id, NULL, &name) <= 1 &&
		    strcmp(name, want_clk[i].gcc) == 0, "clock %u is %s",
		    dt.clk[i].id, want_clk[i].gcc);
	}
	CHECK(dt.have_pd && dt.pd == 2 && qsoc_gdsc_on(2, NULL, &name) <= 1 &&
	    strcmp(name, "USB_30_GDSC") == 0, "power domain %u", dt.pd);
	CHECK(dt.nrst == 1 && dt.rst[0] == 7 &&
	    qsoc_bcr_asserted(7, NULL, &name) <= 1 &&
	    strcmp(name, "GCC_USB_30_BCR") == 0, "reset %u", dt.rst[0]);
	CHECK(strcmp(dt.phy_compat, "qcom,sdm660-qusb2-phy") == 0 &&
	    dt.phy_pa == 0xc012000 && dt.phy_sz == 0x180, "phy %s 0x%llx",
	    dt.phy_compat, (unsigned long long)dt.phy_pa);
	CHECK(dt.nphyclk == 2, "%u phy clocks", dt.nphyclk);
	for (i = 0; i < 2 && i < dt.nphyclk; i++)
		CHECK(dt.phy_clk[i].id == want_phyclk[i].id &&
		    strcmp(dt.phy_clk[i].name, want_phyclk[i].dt) == 0 &&
		    qsoc_clk_on(dt.phy_clk[i].id, NULL, &name) <= 1 &&
		    strcmp(name, want_phyclk[i].gcc) == 0,
		    "phy clock %u: %u %s", i, dt.phy_clk[i].id,
		    dt.phy_clk[i].name);
	CHECK(dt.have_phy_rst && dt.phy_rst == 0 &&
	    qsoc_bcr_asserted(0, NULL, &name) <= 1 &&
	    strcmp(name, "GCC_QUSB2PHY_PRIM_BCR") == 0, "phy reset %u",
	    dt.phy_rst);
	CHECK(strcmp(dt.supply[0], "l1 (pm660l)") == 0 &&
	    strcmp(dt.supply[1], "l10 (pm660)") == 0 &&
	    strcmp(dt.supply[2], "l7 (pm660l)") == 0, "supplies %s, %s, %s",
	    dt.supply[0], dt.supply[1], dt.supply[2]);
	CHECK(dt.fuse_base == 0x780000 && dt.fuse_size == 0x621c &&
	    dt.trim_off == 0x243 && dt.trim_bit == 1 && dt.trim_nbits == 3,
	    "fuse 0x%llx+0x%llx cell 0x%llx bits %u+%u",
	    (unsigned long long)dt.fuse_base,
	    (unsigned long long)dt.fuse_size,
	    (unsigned long long)dt.trim_off, dt.trim_bit, dt.trim_nbits);
}

static void
bad_tree(const char *what, const char *from, const char *to, int want_r)
{
	patch_from = from;
	patch_to = to;
	blocks();
	gcc_state(1, 1);
	stub_log_level = 1;
	probe(what, want_r, 0, 0);
	stub_log_level = 2;
	CHECK(!reg[GCC].mapped && !reg[FUSE].mapped, "%s: GCC or QFPROM "
	    "mapped for a refused tree", what);
	patch_from = patch_to = NULL;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: dwtest clover.dtb\n");
		return 2;
	}
	dtb_path = argv[1];

	blocks();
	gcc_state(0, 0);
	probe("GCC: everything off", OK, 0, 0);
	CHECK(reg[GCC].mapped && reg[FUSE].mapped, "GCC and QFPROM mapped");

	blocks();
	gcc_state(1, 1);
	probe("GCC: everything on", OK, 1, 1);
	tree_checks();

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0x12000, 1);		/* the PHY's reset held */
	probe("GCC: on, the PHY held in reset", OK, 1, 0);

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0xf000, 1);			/* the wrapper's reset held */
	probe("GCC: on, the wrapper held in reset", OK, 0, 1);

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0xf010, 0x80000000u);	/* mock_utmi off */
	probe("GCC: on but mock_utmi", OK, 0, 1);

	blocks();
	gcc_state(1, 0);
	probe("GCC: clocks on, power domain off", OK, 0, 1);

	/* The checker itself: a write must be seen. */
	snapshot();
	coremem[0x100] ^= 1;
	CHECK(changed() == 1, "the comparison misses a written byte");
	coremem[0x100] ^= 1;

	bad_tree("a tree without the wrapper", "qcom,dwc3", "qcom,dwcX",
	    ENXIO);
	bad_tree("a core naming no usb2-phy", "usb2-phy", "usbX-phy", EINVAL);
	bad_tree("a PHY this driver does not know", "qcom,sdm660-qusb2-phy",
	    "qcom,sdm660-qusbX-phy", EINVAL);

	printf("dwtest: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}
