/*
 * xHCI glue stand: what the driver does to the SoC before its xHCI is
 * spoken to, written down as a trace.
 *
 * The driver's own files - the tree (xhci_find.c), the glue (xhci_rk.c,
 * xhci_qcom.c), the core (xhci_dwc3.c) - run on a real device tree, the
 * same one the machine boots with, and every mapping, register write and
 * delay they make is printed on stdout, one per line, in order:
 *
 *	map 0xfcc00000+0x400000
 *	wr 0xfdd20000+0x0484 = 0x07030000
 *	delay 100
 *
 * Reads come from memory behind each mapping, as the last write left it;
 * the DWC3's identity register reads what a DWC3 3.00a says.  Nothing
 * here knows what the right sequence is - gluetrace.sh compares the trace
 * of the tree against the trace of the driver as it was before a change,
 * which is what a glue moved from one file to another must not change.
 *
 *	gluetest DTB INSTANCE		trace of the glue bringing it up
 *	gluetest -d DTB INSTANCE	and of going down afterwards
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

struct xhci xhci;
struct log xhci_log = { .name = "xhci" };

static const char *dtb_path;
static int verbose;

#define MAX_MAPS	16
static struct {
	phys_bytes pa;
	size_t size;
	uint8_t *mem;
} maps[MAX_MAPS];
static unsigned nmaps;

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
	if ((maps[nmaps].mem = calloc(1, len)) == NULL)
		return MAP_FAILED;
	/*
	 * What a DWC3 3.00a says it is, wherever its core is mapped, and the
	 * two registers the glue changes as they come out of reset - with
	 * SUSPHY, ENBLSLPM and U2_FREECLK_EXISTS set, an 8-bit interface and
	 * the device role.  From these the CB2's trace ends in GUSB2PHYCFG
	 * 0x00101408, which is what the board reads back
	 * (port/cb2-usb/reference-regs.txt).
	 */
	if (pa == xhci.info.base && len > DWC3_GUSB2PHYCFG0 + 4) {
		*(uint32_t *)(maps[nmaps].mem + DWC3_GSNPSID) = 0x5533300a;
		*(uint32_t *)(maps[nmaps].mem + DWC3_GCTL) = 0x30c12004;
		*(uint32_t *)(maps[nmaps].mem + DWC3_GUSB2PHYCFG0) = 0x40102540;
	}
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

int
main(int argc, char **argv)
{
	int r, down = 0, instance;

	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		verbose = 1;
		stub_log_level = LEVEL_INFO;
		argc--;
		argv++;
	}
	if (argc > 1 && strcmp(argv[1], "-d") == 0) {
		down = 1;
		argc--;
		argv++;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: gluetest [-v] [-d] DTB INSTANCE\n");
		return 2;
	}
	dtb_path = argv[1];
	instance = atoi(argv[2]);

	if ((r = xhci_find(&xhci.info, instance)) != OK) {
		printf("find %d\n", r);
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
		return 0;
	}
	if ((r = xhci_dwc3_init()) != OK) {
		printf("dwc3 %d\n", r);
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
#endif
	return 0;
}
