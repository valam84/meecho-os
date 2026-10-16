/*
 * The touch driver's bring-up, run on the Mi Pad 4's real device tree.
 *
 * touch.c is included whole and bring_up() runs as on the tablet: the tree
 * is the mainline sdm660-xiaomi-clover.dtb (built by port/clover/mkboot.sh;
 * GPL-2.0-only, so not kept in this tree - the path is the first argument),
 * read by libsys's own fdt.c.  The TLMM's three tiles, the GCC and the QUP
 * are plain memory; afterwards the stand checks what landed in them, word
 * by word, against what the tree asks for:
 *
 *   the bus      i2c@c177000 (BLSP1 QUP3), clocks 30 (QUP3 I2C) and 25
 *                (BLSP1 AHB, a vote), pins gpio10/11 at blsp_i2c3, 2 mA,
 *                no pull - "i2c3-default-state"
 *   the screen   touchscreen@38, reset gpio66 driven low, interrupt gpio67
 *                an input with its pull-up, 1200x1920 like the frame buffer
 *
 * and that nothing else in the TLMM was written - above all not gpio0..3,
 * which the tree reserves for the secure world (gpio-reserved-ranges): a
 * touch there is an access fault, not a wrong pin.
 *
 * The QUP model is memory that never answers, so bring_up() stops at the
 * controller with QUP_ERR_*; the clock and pin work before it is what this
 * stand is about.  The protocol itself is quptest.c's.
 */
#include <sys/mman.h>

#define main touch_main
#include "touch.c"
#undef main

#include "sdm660_gcc.h"
#include "sdm660_tlmm.h"

int stub_log_level = 1;			/* warnings */
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

#define TILE_SZ		0x400000
#define GCC_SZ		0x94000
static uint8_t tiles[3][TILE_SZ] __attribute__((aligned(8)));
static uint8_t gccmem[GCC_SZ] __attribute__((aligned(8)));
static uint8_t qupmem[0x1000] __attribute__((aligned(8)));
static const unsigned long tile_pa[3] = { 0x3100000, 0x3500000, 0x3900000 };
static unsigned mapped_other;

void *
vm_map_phys(int who, void *phys, size_t len)
{
	unsigned long pa = (unsigned long)phys;
	unsigned t;

	(void)who;
	for (t = 0; t < 3; t++)
		if (pa == tile_pa[t] && len == TILE_SZ)
			return tiles[t];
	if (pa == 0x100000 && len == GCC_SZ)
		return gccmem;
	if (pa == 0xc177000 && len == 0x600)
		return qupmem;
	printf("  FAIL: mapping 0x%lx+0x%zx is none of TLMM, GCC, QUP3\n",
	    pa, len);
	mapped_other++;
	return MAP_FAILED;
}

int vm_unmap_phys(int who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }

/* The GCC's root update finishes as the hardware would: by the next look. */
void
micro_delay(unsigned long us)
{
	unsigned i;

	(void)us;
	for (i = 0; i < sizeof(sdm660_gcc) / sizeof(sdm660_gcc[0]); i++)
		if (sdm660_gcc[i].rcg != 0)
			*(uint32_t *)(gccmem + sdm660_gcc[i].rcg) &= ~1u;
}

void *
fdt_fetch(void)
{
	FILE *f = fopen(dtb_path, "rb");
	long n;
	void *buf;

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
	return buf;
}

void default_log(void) { }
int env_parse(const char *n, const char *f, int fl, long *p, long lo, long hi)
	{ (void)n; (void)f; (void)fl; (void)p; (void)lo; (void)hi; return 0; }
void env_setargs(int argc, char **argv) { (void)argc; (void)argv; }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { (void)cb; }
void sef_setcb_signal_handler(void (*cb)(int)) { (void)cb; }
void sef_startup(void) { }
int sys_setalarm(clock_t t, int a) { (void)t; (void)a; return 0; }
clock_t sys_hz(void) { return 100; }
void inputdriver_send_event(int m, unsigned short p, unsigned short c,
	int v, int f) { (void)m; (void)p; (void)c; (void)v; (void)f; }
void inputdriver_announce(unsigned int t) { (void)t; }
void inputdriver_terminate(void) { }
void inputdriver_task(struct inputdriver *t) { (void)t; }

static uint32_t
pin_cfg(unsigned pin)
{
	return *(uint32_t *)(tiles[sdm660_gpio[pin].tile] + pin * 0x1000);
}

static uint32_t
pin_io(unsigned pin)
{
	return *(uint32_t *)(tiles[sdm660_gpio[pin].tile] + pin * 0x1000 + 4);
}

static uint32_t
gcc_rd(uint32_t off)
{
	return *(uint32_t *)(gccmem + off);
}

int
main(int argc, char **argv)
{
	unsigned pin, t, off, written;
	int r;

	if (argc < 2) {
		fprintf(stderr, "usage: %s sdm660-xiaomi-clover.dtb\n", argv[0]);
		return 2;
	}
	dtb_path = argv[1];

	printf("touch bring-up on the clover tree\n");
	r = bring_up();
	CHECK(r != OK, "bring_up() returned OK against a QUP that never "
	    "answers");
	CHECK(mapped_other == 0, "a block other than TLMM/GCC/QUP3 mapped");

	/* What the tree said. */
	CHECK(dt.found, "no touch screen found");
	CHECK(dt.bus_pa == 0xc177000 && dt.bus_sz == 0x600,
	    "bus 0x%llx+0x%llx, expected i2c@c177000+0x600",
	    (unsigned long long)dt.bus_pa, (unsigned long long)dt.bus_sz);
	CHECK(dt.clk_core == 30 && dt.clk_iface == 25,
	    "clocks core %u iface %u, expected 30 (QUP3 I2C) and 25 (BLSP1 "
	    "AHB)", dt.clk_core, dt.clk_iface);
	CHECK(dt.pinctrl == 0x6b, "bus pinctrl phandle 0x%x, expected 0x6b",
	    dt.pinctrl);
	CHECK(dt.addr == 0x38, "address 0x%x, expected 0x38", dt.addr);
	CHECK(dt.have_reset && dt.reset_pin == 66, "reset gpio %u (%d)",
	    dt.reset_pin, dt.have_reset);
	CHECK(dt.have_int && dt.int_pin == 67, "interrupt gpio %u (%d)",
	    dt.int_pin, dt.have_int);
	CHECK(dt.size_x == 1200 && dt.size_y == 1920 && !dt.swap &&
	    !dt.inv_x && !dt.inv_y, "touch %ux%u swap %d inv %d/%d",
	    dt.size_x, dt.size_y, dt.swap, dt.inv_x, dt.inv_y);
	CHECK(dt.scr_w == 1200 && dt.scr_h == 1920,
	    "screen %ux%u from simple-framebuffer", dt.scr_w, dt.scr_h);

	/* Clocks: the AHB vote, the QUP clock's branch, its root at XO. */
	CHECK(gcc_rd(0x52004) == (1u << 17),
	    "BLSP1 AHB vote: 0x52004 = 0x%08x, expected bit 17 only",
	    gcc_rd(0x52004));
	CHECK(gcc_rd(0x1d008) == 1, "QUP3 I2C CBCR 0x1d008 = 0x%08x, "
	    "expected the enable bit", gcc_rd(0x1d008));
	CHECK(gcc_rd(0x1d024) == 1, "QUP3 I2C root CFG 0x1d024 = 0x%08x, "
	    "expected XO (source 0), divider 1", gcc_rd(0x1d024));
	for (off = 0, written = 0; off < GCC_SZ; off += 4)
		if (gcc_rd(off) != 0 && off != 0x52004 && off != 0x1d008 &&
		    off != 0x1d024) {
			written++;
			printf("  GCC 0x%05x = 0x%08x\n", off, gcc_rd(off));
		}
	CHECK(written == 0, "%u other GCC words written", written);

	/* Pins. */
	CHECK(pin_cfg(10) == (2u << 2) && pin_cfg(11) == (2u << 2),
	    "SDA/SCL cfg %03x/%03x, expected 0x008 (blsp_i2c3, 2 mA, no "
	    "pull, input)", pin_cfg(10), pin_cfg(11));
	CHECK(pin_cfg(67) == 0x0c3, "interrupt pin cfg 0x%03x, expected "
	    "0x0c3 (gpio, 8 mA, pull-up, input)", pin_cfg(67));
	/*
	 * The reset line is not touched before the controller has been asked
	 * as the boot loader left it - and here the QUP never starts, so not
	 * at all.
	 */
	CHECK(pin_cfg(66) == 0 && pin_io(66) == 0,
	    "reset pin cfg 0x%03x io 0x%x: touched before the controller "
	    "was asked", pin_cfg(66), pin_io(66));
	for (t = 0, written = 0; t < 3; t++)
		for (off = 0; off < TILE_SZ; off += 4) {
			uint32_t v = *(uint32_t *)(tiles[t] + off);

			pin = off / 0x1000;
			if (v == 0 || (sdm660_gpio[pin < SDM660_NGPIO ? pin :
			    0].tile == t && (pin == 10 || pin == 11 ||
			    pin == 66 || pin == 67)))
				continue;
			written++;
			printf("  TLMM tile %u +0x%06x = 0x%08x\n", t, off, v);
		}
	CHECK(written == 0, "%u other TLMM words written", written);
	for (pin = 0; pin < 4; pin++)
		CHECK(pin_cfg(pin) == 0 && pin_io(pin) == 0,
		    "reserved gpio%u touched", pin);

	printf("\n%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
