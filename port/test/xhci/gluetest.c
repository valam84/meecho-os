/*
 * xHCI glue stand: what the driver does to the SoC before its xHCI is
 * spoken to, written down as a trace.
 *
 * The driver's own files - the tree (xhci_find.c), the glue (xhci_rk.c,
 * xhci_qcom.c with dwc3_ecm's files), the core (xhci_dwc3.c) - run on a
 * real device tree, the same one the machine boots with, and every mapping,
 * register write and delay they make is printed on stdout, one per line,
 * in order:
 *
 *	map 0xfcc00000+0x400000
 *	wr 0xfdd20000+0x0484 = 0x07030000
 *	delay 100
 *
 * Writes that do not go through xhci_wr() - Qualcomm's wrapper and PHY are
 * dwc3_ecm's code, which stores directly - are found by comparing each
 * block other than the core against what it held when it was mapped, at
 * the end:
 *
 *	final 0x0a8f8800+0x0010 = 0x00000000 (was 0x10100000)
 *
 * Reads come from memory behind each mapping, as the last write left it.
 * Nothing here knows what the right sequence is: gluetrace.sh compares the
 * CB2's trace against the driver as it was before a change, and checks the
 * Mi Pad 4's against what hw-usbhost.md says the glue may write.
 *
 *	gluetest [-v] [-d] DTB INSTANCE		Rockchip, or whatever the tree has
 *	gluetest [-v] [-d] -q on|off|nolock [-s] DTB INSTANCE
 *		Qualcomm, with the block as the boot loader leaves it after
 *		fastboot boot (on), its power domain off (off) or its PHY's
 *		PLL not locked (nolock); -s: the session override set, as
 *		dwc3_ecm leaves it when the port was a device until now
 *	-d: and going down afterwards
 *
 * Built with -DOLD_API against the driver before the glue became a table
 * (xhci_rk_map() and friends), without it against the table.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "xhci.h"
#include "xhcireg.h"

#ifndef OLD_API
#include <minix/fdt.h>
#include "dwc3_ecm.h"
#include "sdm660_gcc.h"
#endif

struct xhci xhci;
struct log xhci_log = { .name = "xhci" };

static const char *dtb_path;
static int verbose;

#define MAX_MAPS	16
static struct {
	phys_bytes pa;
	size_t size;
	uint8_t *mem;
	uint8_t *was;			/* as mapped, presets and all */
} maps[MAX_MAPS];
static unsigned nmaps;

/* Qualcomm: the boot loader's block, and where its parts are. */
#define Q_NONE		0
#define Q_ON		1
#define Q_OFF		2
#define Q_NOLOCK	3
static int qmode, qsession;
#ifndef OLD_API
static struct { phys_bytes gcc, qs, phy, core; } qpa;

/* What the sdmmc stand's <minix/log.h> points a log's function at. */
void default_log(void) { }
#endif

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

static void
w32(uint8_t *m, unsigned off, uint32_t v)
{
	memcpy(m + off, &v, 4);
}

#ifndef OLD_API
/* The GCC as the boot loader leaves it (on), or with USB_30_GDSC off. */
static void
gcc_preset(uint8_t *m, size_t size)
{
	unsigned i;

	for (i = 0; i < sizeof(sdm660_gcc) / sizeof(sdm660_gcc[0]); i++)
		if (sdm660_gcc[i].halt != 0 && sdm660_gcc[i].halt + 4 <= size)
			w32(m, sdm660_gcc[i].halt, 0);	/* CLK_OFF clear */
	for (i = 0; i < sizeof(sdm660_gdsc) / sizeof(sdm660_gdsc[0]); i++)
		if (sdm660_gdsc[i].gdscr + 4 <= size)
			w32(m, sdm660_gdsc[i].gdscr, qmode == Q_OFF ? 0 :
			    0x80000000u);
	w32(m, 0xf014 + 4, 0x109);		/* core root: GPLL0 / 5 */
}
#endif

/* What each block holds before the driver looks: hw-net.md, step 2. */
static void
preset(phys_bytes pa, uint8_t *m, size_t len)
{
	if (pa == xhci.info.base && len > DWC3_GUSB2PHYCFG0 + 4) {
		/*
		 * What a DWC3 3.00a says it is, and the two registers the glue
		 * changes.  Out of reset (the CB2): SUSPHY, ENBLSLPM and
		 * U2_FREECLK_EXISTS set, an 8-bit interface, the device role -
		 * from which the CB2's trace ends in GUSB2PHYCFG 0x00101408,
		 * what the board reads back (port/cb2-usb/reference-regs.txt).
		 * After fastboot boot (the Mi Pad 4): what the tablet showed.
		 */
		w32(m, DWC3_GSNPSID, 0x5533300a);
		w32(m, DWC3_GCTL, qmode ? 0x00112000 : 0x30c12004);
		w32(m, DWC3_GUSB2PHYCFG0, qmode ? 0x00002400 : 0x40102540);
		if (qmode)
			w32(m, 0xc408, 0x00001000);	/* GEVNTSIZ, armed */
	}
#ifndef OLD_API
	if (qmode == Q_NONE)
		return;
	if (pa == qpa.gcc)
		gcc_preset(m, len);
	if (pa == qpa.qs && len >= 0x34) {
		w32(m, 0x08, 0x0000000d);		/* utmi as pipe */
		if (qsession) {
			w32(m, 0x10, 0x10100000);	/* VBUS valid, sessvld */
			w32(m, 0x30, 0x01000000);	/* lane0 power present */
		}
	}
	if (pa == qpa.phy && len >= 0xb8) {
		w32(m, 0x38, qmode == Q_NOLOCK ? 0x00 : 0x30);	/* PLL_STATUS */
		w32(m, 0xb4, 0x22);				/* POWERDOWN */
		w32(m, 0x80, 0xf8);
		w32(m, 0x84, 0xa3);
	}
#endif
}

void *
vm_map_phys(endpoint_t who, void *phys, size_t len)
{
	phys_bytes pa = (phys_bytes)phys;

	(void)who;
	printf("map 0x%lx+0x%zx\n", (unsigned long)pa, len);
	if (nmaps == MAX_MAPS || len == 0 || len > 0x1000000)
		return MAP_FAILED;
	maps[nmaps].pa = pa;
	maps[nmaps].size = len;
	if ((maps[nmaps].mem = calloc(1, len)) == NULL ||
	    (maps[nmaps].was = malloc(len)) == NULL)
		return MAP_FAILED;
	preset(pa, maps[nmaps].mem, len);
	memcpy(maps[nmaps].was, maps[nmaps].mem, len);
	return maps[nmaps++].mem;
}

int
vm_unmap_phys(endpoint_t who, void *vaddr, size_t len)
{
	(void)who;
	(void)vaddr;
	(void)len;
	return 0;
}

static unsigned
block_of(vir_bytes b, unsigned off)
{
	unsigned i;

	for (i = 0; i < nmaps; i++)
		if (b >= (vir_bytes)maps[i].mem &&
		    b + off + 4 <= (vir_bytes)maps[i].mem + maps[i].size)
			return i;
	fprintf(stderr, "access to 0x%lx+0x%x is in no mapping\n",
	    (unsigned long)b, off);
	exit(2);
}

uint32_t
trace_rd(vir_bytes b, unsigned off)
{
	(void)block_of(b, off);
	return *(volatile uint32_t *)(b + off);
}

void
trace_wr(vir_bytes b, unsigned off, uint32_t v)
{
	unsigned i = block_of(b, off);

	printf("wr 0x%lx+0x%04lx = 0x%08x\n", (unsigned long)maps[i].pa,
	    (unsigned long)(b + off - (vir_bytes)maps[i].mem), v);
	*(volatile uint32_t *)(b + off) = v;
}

void
micro_delay(unsigned long us)
{
	printf("delay %lu\n", us);
}

/* The sdmmc stand's <minix/log.h> prints at this level and below. */
int stub_log_level = 0;

/* Every word of every block but the core that is not what it was. */
static void
finals(void)
{
	unsigned i;
	size_t off;
	uint32_t was, now;

	/* The Rockchip glue writes through xhci_wr() only: nothing to add. */
	if (qmode == Q_NONE)
		return;
	for (i = 0; i < nmaps; i++) {
		if (maps[i].pa == xhci.info.base)
			continue;
		for (off = 0; off + 4 <= maps[i].size; off += 4) {
			memcpy(&was, maps[i].was + off, 4);
			memcpy(&now, maps[i].mem + off, 4);
			if (was != now)
				printf("final 0x%lx+0x%04lx = 0x%08x (was "
				    "0x%08x)\n", (unsigned long)maps[i].pa,
				    (unsigned long)off, now, was);
		}
	}
}

#ifndef OLD_API
static int
find_gcc(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	u64_t base, size;

	(void)depth;
	(void)name;
	if (fdt_node_is_compatible(node, "qcom,gcc-sdm660") &&
	    fdt_node_reg(node, 0, &base, &size) == 0)
		*(phys_bytes *)cookie = (phys_bytes)base;
	return 0;
}

/* Where the Qualcomm blocks are, from the tree, as the glue will find them. */
static int
qcom_where(void)
{
	static struct dwc3_dt d;
	void *dtb;

	if ((dtb = fdt_fetch()) == NULL || dwc3_find(dtb, &d) != OK) {
		fprintf(stderr, "no qcom,dwc3 in %s\n", dtb_path);
		return -1;
	}
	(void)fdt_walk(dtb, find_gcc, &qpa.gcc);
	free(dtb);
	qpa.qs = (phys_bytes)d.qscratch_pa;
	qpa.phy = (phys_bytes)d.phy_pa;
	qpa.core = (phys_bytes)d.core_pa;
	return qpa.gcc != 0 ? 0 : -1;
}
#endif

int
main(int argc, char **argv)
{
	int r, down = 0, instance;

	for (;;) {
		if (argc > 1 && strcmp(argv[1], "-v") == 0) {
			verbose = 1;
			stub_log_level = LEVEL_INFO;
		} else if (argc > 1 && strcmp(argv[1], "-d") == 0)
			down = 1;
		else if (argc > 1 && strcmp(argv[1], "-s") == 0)
			qsession = 1;
		else if (argc > 2 && strcmp(argv[1], "-q") == 0) {
			qmode = strcmp(argv[2], "off") == 0 ? Q_OFF :
			    strcmp(argv[2], "nolock") == 0 ? Q_NOLOCK : Q_ON;
			argc--;
			argv++;
		} else
			break;
		argc--;
		argv++;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: gluetest [-v] [-d] [-q on|off|nolock "
		    "[-s]] DTB INSTANCE\n");
		return 2;
	}
	dtb_path = argv[1];
	instance = atoi(argv[2]);
#ifndef OLD_API
	if (qmode != Q_NONE && qcom_where() != 0)
		return 2;
#endif

	if ((r = xhci_find(&xhci.info, instance)) != OK) {
		printf("find %d\n", r);
		finals();
		return 0;
	}
#ifdef OLD_API
	if ((r = xhci_rk_map()) != OK || (r = xhci_rk_power_domain()) != OK) {
		printf("up %d\n", r);
		return 0;
	}
	xhci_rk_clocks_on();
	xhci_rk_phy_init();
	if ((r = xhci_rk_dwc3_init()) != OK) {
		printf("dwc3 %d\n", r);
		return 0;
	}
	printf("report\n");
	xhci_rk_report();
	(void)down;
#else
	if ((r = xhci.info.glue->up()) != OK) {
		printf("up %d\n", r);
		finals();
		return 0;
	}
	if ((r = xhci_dwc3_init()) != OK) {
		printf("dwc3 %d\n", r);
		finals();
		return 0;
	}
	if (xhci.info.glue->host != NULL)
		xhci.info.glue->host();
	printf("report\n");
	if (xhci.info.glue->report != NULL)
		xhci.info.glue->report();
	if (down && xhci.info.glue->down != NULL) {
		printf("down\n");
		xhci.info.glue->down();
	}
	finals();
#endif
	return 0;
}
