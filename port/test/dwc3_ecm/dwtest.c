/*
 * dwc3_ecm, run on the Mi Pad 4's real device tree.
 *
 * dwc3_ecm.c is included whole and its ndr_init runs as on the tablet: the
 * tree is the mainline sdm660-xiaomi-clover.dtb (built by
 * port/clover/mkboot.sh; GPL-2.0-only, so not kept in this tree - the path
 * is the first argument), read by libsys's own fdt.c.  The GCC, Qualcomm's
 * wrapper (QSCRATCH), the QUSB2 PHY and the QFPROM are plain memory.
 *
 * With probe=1 (step 2):
 *
 *   the tree      what dwc3_find() read, against values written down from
 *                 the tree's source separately (sdm630.dtsi, the clover
 *                 dtsi): addresses, the interrupt, the clocks with the
 *                 GCC's names for them, resets, the power domain, the
 *                 supplies, the fuse cell
 *   the guard     for six states of the GCC, which blocks were mapped at
 *                 all: none of the wrapper, core or PHY unless the power
 *                 domain, resets and clocks say they may be read
 *   no writes     after every run, every model byte for byte as before
 *   bad trees     a tree without the wrapper, with the core naming no
 *                 usb2-phy, or with a PHY this driver does not know is
 *                 refused, and nothing is mapped
 *
 * Then the take-over (step 3a) and the Ethernet function (step 4), with
 * the DWC3 core a model behind dwc3_rd()/dwc3_wr() (dev-hook.h): endpoint
 * commands are carried out on the spot, the soft reset ends at once,
 * events are put in the buffer the driver gave and counted in GEVNTCOUNT,
 * and the host's side of each transfer is played on the TRBs.  Checked:
 *
 *   order         events masked before the boot loader's buffer is
 *                 replaced, and only after the soft reset; LPM_CAP gone;
 *                 ep0 set up as Linux does it; a SETUP TRB; run
 *   caches        every TRB cleaned before its StartTransfer; every event
 *                 read after an invalidate of the buffer
 *   enumeration   a Linux host's requests, one control transfer at a time
 *                 through all three phases: the descriptors byte for byte
 *                 against values written down here (f_ecm's layout), the
 *                 address in DCFG before the status phase, BOS and vendor
 *                 requests stalled
 *   ECM           SET_CONFIGURATION: DEPSTARTCFG(2) and the notification
 *                 endpoint; SET_INTERFACE(1, 1): the bulk pair, a receive
 *                 TRB, link up, NETWORK_CONNECTION then SPEED_CHANGE; the
 *                 packet filter request; a frame each way, a frame of 512
 *                 bytes with its zero-length packet, the second frame
 *                 waiting for the first; alt 0 and a bus reset take the
 *                 link down and the endpoints away
 *   no writes     the GCC, the PHY and the QFPROM untouched; the wrapper
 *                 only in its session bits
 *   refusal       a PHY whose PLL is not locked: nothing taken over
 */
#include <sys/mman.h>

#define main dwc3_main
#include "dwc3_ecm.c"
#undef main

#include "sdm660_gcc.h"
#include "dwc3_regs.h"
#include <minix/cachectl.h>

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

static long stub_probe = 1;		/* probe=1 until the take-over */

int
env_parse(const char *n, const char *f, int fl, long *p, long lo, long hi)
{
	(void)f; (void)fl; (void)lo; (void)hi;
	if (strcmp(n, "probe") == 0) {
		*p = stub_probe;
		return EP_SET;
	}
	return 0;
}
void env_setargs(int argc, char **argv) { (void)argc; (void)argv; }
int sys_setalarm(clock_t t, int a) { (void)t; (void)a; return 0; }
clock_t sys_hz(void) { return 100; }

/* libnetdriver, as far as the driver calls it. */
static struct { int recv, send, link; unsigned oerr, ierr; } N;
void netdriver_task(const struct netdriver *ndp) { (void)ndp; }
void netdriver_recv(void) { N.recv++; }
void netdriver_send(void) { N.send++; }
void netdriver_link(void) { N.link++; }
void netdriver_stat_oerror(uint32_t c) { N.oerr += c; }
void netdriver_stat_ierror(uint32_t c) { N.ierr += c; }
void
netdriver_copyin(struct netdriver_data *d, size_t off, void *p, size_t n)
{
	memcpy(p, d->buf + off, n);
}
void
netdriver_copyout(struct netdriver_data *d, size_t off, const void *p,
	size_t n)
{
	memcpy(d->buf + off, p, n);
}

static int
init(void)
{
	netdriver_addr_t addr;
	uint32_t caps;
	unsigned int ticks;

	return dwc3_init(0, &addr, &caps, &ticks);
}

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

static int
same(unsigned i)
{
	return memcmp(reg[i].snap, reg[i].mem, reg[i].size) == 0;
}

/* One probe=1 run: its result and which of core, qscratch, phy mapped. */
static void
probe(const char *what, int want_r, int want_core, int want_phy)
{
	int r;

	printf("--- %s\n", what);
	snapshot();
	r = init();
	CHECK(r == want_r, "%s: init %d, expected %d", what, r, want_r);
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

/*===========================================================================*
 *    DMA memory and caches                                                   *
 *===========================================================================*/
#define FAKE_PA		0x9a000000ul	/* below 4 GiB, as on the tablet */
static uint8_t arena[DWC3_DMA_SIZE] __attribute__((aligned(4096)));

void *
alloc_contig(size_t len, int flags, phys_bytes *phys)
{
	(void)flags;
	if (len > sizeof(arena))
		return NULL;
	*phys = FAKE_PA;
	return arena;
}

void free_contig(void *addr, size_t len) { (void)addr; (void)len; }

static uint8_t *
pa2host(uint64_t pa, size_t len)
{
	if (pa < FAKE_PA || pa + len > FAKE_PA + sizeof(arena))
		return NULL;
	return arena + (pa - FAKE_PA);
}

/* The model's view of what the caches were told. */
static struct {
	const uint8_t *given[8];	/* cleaned since the last StartTransfer */
	size_t given_len[8];
	unsigned ngiven;
	int ev_taken;		/* the events invalidated since the last ack */
} C;

static int
covers(const void *a, size_t alen, const void *b, size_t blen)
{
	return (const uint8_t *)a <= (const uint8_t *)b &&
	    (const uint8_t *)a + alen >= (const uint8_t *)b + blen;
}

int
sys_cachectl(int op, void *addr, size_t len)
{
	if ((op == CACHE_CLEAN || op == CACHE_CLEAN_INVALIDATE) &&
	    C.ngiven < 8) {
		C.given[C.ngiven] = addr;
		C.given_len[C.ngiven++] = len;
	}
	if (dwc.ev != NULL && op == CACHE_INVALIDATE &&
	    covers(addr, len, dwc.ev, DWC3_EVBUF_SIZE))
		C.ev_taken = 1;
	return 0;
}

static int
was_given(const void *p, size_t len)
{
	unsigned i;

	for (i = 0; i < C.ngiven; i++)
		if (covers(C.given[i], C.given_len[i], p, len))
			return 1;
	return 0;
}

/*===========================================================================*
 *    the core, as a model                                                    *
 *===========================================================================*/
#define R(off)		(*(uint32_t *)(coremem + (off)))
#define NEP		DWC3_NEP

static struct {
	uint32_t pending;	/* event bytes not yet acked */
	unsigned evw;		/* where the next event goes */
	int reset;		/* the soft reset happened */
	int started[NEP];	/* a transfer is running on the endpoint */
	uint64_t trb_pa[NEP];	/* its TRB */
	int stalled;		/* ep0 stalled since the host last looked */
	uint32_t dcfg_at_status;	/* DCFG when the last status began */
	unsigned errors;
	struct { unsigned ep, cmd; uint32_t p0, p1; } log[64];
	unsigned nlog;
} M;

#define MERR(...) do { M.errors++; printf("  model: "); printf(__VA_ARGS__);\
	printf("\n"); } while (0)

uint32_t
model_rd(unsigned off)
{
	if (off == DWC3_GEVNTCOUNT)
		return M.pending;
	return R(off);
}

void
model_wr(unsigned off, uint32_t v)
{
	struct dwc3_trb *t;
	unsigned ep, cmd;
	uint32_t ty;

	switch (off) {
	case DWC3_DCTL:
		if (v & DWC3_DCTL_CSFTRST) {
			M.reset = 1;
			v &= ~DWC3_DCTL_CSFTRST;	/* done at once */
		}
		R(off) = v;
		if (v & DWC3_DCTL_RUN_STOP)
			R(DWC3_DSTS) &= ~DWC3_DSTS_DEVCTRLHLT;
		else
			R(DWC3_DSTS) |= DWC3_DSTS_DEVCTRLHLT;
		return;
	case DWC3_GEVNTADRLO:
	case DWC3_GEVNTADRHI:
		if (!(R(DWC3_GEVNTSIZ) & DWC3_GEVNTSIZ_INTMASK))
			MERR("event buffer moved with events unmasked");
		if (!M.reset)
			MERR("event buffer moved before the soft reset");
		R(off) = v;
		return;
	case DWC3_GEVNTCOUNT:
		if (v > M.pending)
			MERR("acked %u event bytes, %u pending", v, M.pending);
		else if (v != 0 && !C.ev_taken)
			MERR("events acked without an invalidate of the buffer");
		M.pending -= v <= M.pending ? v : M.pending;
		C.ev_taken = 0;
		return;
	}
	if (off >= DWC3_DEPCMD(0) && off <= DWC3_DEPCMD(NEP - 1) &&
	    (off - DWC3_DEP_BASE(0)) % 0x10 == 0xc) {
		ep = (off - DWC3_DEP_BASE(0)) / 0x10;
		cmd = v & 0xf;
		if (!(v & DWC3_DEPCMD_CMDACT))
			MERR("ep%u command %u without CMDACT", ep, cmd);
		if (M.nlog < 64) {
			M.log[M.nlog].ep = ep;
			M.log[M.nlog].cmd = cmd | (v & 0xffff0000u);
			M.log[M.nlog].p0 = R(DWC3_DEPCMDPAR0(ep));
			M.log[M.nlog].p1 = R(DWC3_DEPCMDPAR1(ep));
			M.nlog++;
		}
		v &= ~DWC3_DEPCMD_CMDACT;
		switch (cmd) {
		case DWC3_DEPCMD_STARTTRANSFER:
			M.trb_pa[ep] = (uint64_t)R(DWC3_DEPCMDPAR0(ep)) << 32 |
			    R(DWC3_DEPCMDPAR1(ep));
			t = (struct dwc3_trb *)pa2host(M.trb_pa[ep], 16);
			if (t == NULL)
				MERR("ep%u StartTransfer of 0x%llx", ep,
				    (unsigned long long)M.trb_pa[ep]);
			else if (!was_given(t, 16))
				MERR("ep%u StartTransfer of a TRB not cleaned",
				    ep);
			C.ngiven = 0;
			if (M.started[ep])
				MERR("ep%u StartTransfer while one runs", ep);
			if (!(R(DWC3_DALEPENA) & (1u << ep)))
				MERR("ep%u StartTransfer, not enabled", ep);
			M.started[ep] = 1;
			ty = t != NULL ? t->ctrl & 0x3f0 : 0;
			if (ty == DWC3_TRBCTL_STATUS2 ||
			    ty == DWC3_TRBCTL_STATUS3)
				M.dcfg_at_status = R(DWC3_DCFG);
			v = (v & 0xffff) | (ep + 1) << 16;	/* resource */
			break;
		case DWC3_DEPCMD_ENDTRANSFER:
			M.started[ep] = 0;
			break;
		case DWC3_DEPCMD_SETSTALL:
			M.stalled = 1;
			break;
		}
		R(off) = v;
		return;
	}
	R(off) = v;
}

static void
inject(uint32_t e)
{
	uint8_t *ev = pa2host((uint64_t)R(DWC3_GEVNTADRHI) << 32 |
	    R(DWC3_GEVNTADRLO), DWC3_EVBUF_SIZE);

	if (ev == NULL) {
		MERR("event buffer 0x%x not the driver's DMA memory",
		    R(DWC3_GEVNTADRLO));
		return;
	}
	memcpy(ev + M.evw, &e, 4);
	M.evw = (M.evw + 4) % DWC3_EVBUF_SIZE;
	M.pending += 4;
	dwc3_dev_poll();
}

#define EV_RESET		0x101u
#define EV_CONNECT_DONE		0x201u
#define EV_COMPLETE(ep)		((ep) << 1 | DWC3_DEPEVT_XFERCOMPLETE << 6)
#define EV_NOTREADY(ep, ph)	((ep) << 1 | DWC3_DEPEVT_XFERNOTREADY << 6 | \
				 (ph) << 12)

/* The TRB running on ep, if it is of type type and owned by the core. */
static struct dwc3_trb *
trb_on(unsigned ep, uint32_t type, const char *what)
{
	struct dwc3_trb *t;

	if (!M.started[ep] || (t = (struct dwc3_trb *)pa2host(M.trb_pa[ep],
	    32)) == NULL) {
		MERR("%s: no transfer on ep%u", what, ep);
		return NULL;
	}
	if ((t->ctrl & 0x3f0) != type || !(t->ctrl & DWC3_TRB_CTRL_HWO)) {
		MERR("%s: ep%u TRB ctrl 0x%x, wanted type 0x%x", what, ep,
		    t->ctrl, type);
		return NULL;
	}
	return t;
}

static uint8_t *
trb_buf(const struct dwc3_trb *t, size_t len)
{
	return pa2host((uint64_t)t->bph << 32 | t->bpl, len);
}

/* The core finished the transfer on ep: n bytes of the TRB not moved. */
static void
finish(unsigned ep, struct dwc3_trb *t, uint32_t left)
{
	t->ctrl &= ~DWC3_TRB_CTRL_HWO;
	t->size = (t->size & ~DWC3_TRB_SIZE_MASK) | left;
	M.started[ep] = 0;
	inject(EV_COMPLETE(ep));
}

/* The link is up at high speed: DSTS says so, then Connect Done. */
static void
connect_hs(void)
{
	R(DWC3_DSTS) &= ~DWC3_DSTS_CONNECTSPD;
	inject(EV_CONNECT_DONE);
}

/*
 * One control transfer from the host: SETUP, a data phase to the host if
 * the request has one, status.  The bytes the device sent go to in; the
 * result is their number, or -1 for a stall.
 */
static int
control(const uint8_t s[8], uint8_t *in)
{
	struct dwc3_trb *t;
	unsigned len = s[6] | s[7] << 8;
	uint8_t *b;
	int n = 0;

	if ((t = trb_on(0, DWC3_TRBCTL_SETUP, "setup")) == NULL)
		return -2;
	if ((t->size & DWC3_TRB_SIZE_MASK) != 8 || (b = trb_buf(t, 8)) ==
	    NULL) {
		MERR("setup TRB: size %u, buffer 0x%x", t->size, t->bpl);
		return -2;
	}
	memcpy(b, s, 8);
	M.stalled = 0;
	finish(0, t, 0);
	if (M.stalled) {
		if (trb_on(0, DWC3_TRBCTL_SETUP, "after a stall") == NULL)
			return -2;
		return -1;
	}
	if (len != 0 && (s[0] & 0x80)) {
		if ((t = trb_on(1, DWC3_TRBCTL_DATA, "data")) == NULL)
			return -2;
		n = (int)(t->size & DWC3_TRB_SIZE_MASK);
		if ((b = trb_buf(t, (size_t)n)) == NULL)
			return -2;
		memcpy(in, b, (size_t)n);
		finish(1, t, 0);
		inject(EV_NOTREADY(0, DWC3_DEPEVT_CONTROL_STATUS));
		if ((t = trb_on(0, DWC3_TRBCTL_STATUS3, "status")) == NULL)
			return -2;
		finish(0, t, 0);
	} else {
		inject(EV_NOTREADY(1, DWC3_DEPEVT_CONTROL_STATUS));
		if ((t = trb_on(1, DWC3_TRBCTL_STATUS2, "status")) == NULL)
			return -2;
		finish(1, t, 0);
	}
	if (trb_on(0, DWC3_TRBCTL_SETUP, "the next setup") == NULL)
		return -2;
	return n;
}

static int
req(uint8_t type, uint8_t r, unsigned value, unsigned index, unsigned len,
	uint8_t *in)
{
	const uint8_t s[8] = { type, r, value & 0xff, value >> 8, index & 0xff,
	    index >> 8, len & 0xff, len >> 8 };

	return control(s, in);
}

/* Commands logged since mark, on ep, of command cmd. */
static unsigned
logged(unsigned mark, unsigned ep, unsigned cmd, unsigned *at)
{
	unsigned i, n = 0;

	for (i = mark; i < M.nlog; i++)
		if (M.log[i].ep == ep && (M.log[i].cmd & 0xf) == cmd) {
			if (at != NULL && n == 0)
				*at = i;
			n++;
		}
	return n;
}

/*===========================================================================*
 *    the take-over and the enumeration                                       *
 *===========================================================================*/
/* What the boot loader left: the tablet's photograph of step 2. */
static void
as_left(void)
{
	blocks();
	gcc_state(1, 1);
	w32(coremem, DWC3_GSNPSID, 0x5533300a);
	w32(coremem, DWC3_GCTL, 0x00112000);
	w32(coremem, DWC3_GUSB2PHYCFG, 0x00002400);
	w32(coremem, DWC3_DCFG, 0x00480880);
	w32(coremem, DWC3_DCTL, 0x10f00000);
	w32(coremem, DWC3_DSTS, 0x00d30641);
	w32(coremem, DWC3_GEVNTADRLO, 0x9b566000);
	w32(coremem, DWC3_GEVNTSIZ, 0x00001000);
	w32(qsmem, QSCRATCH_GENERAL_CFG, 0x0000000d);
	w32(phymem, 0x38, 0x30);
	w32(phymem, 0xb4, 0x22);
	memset(&M, 0, sizeof(M));
	memset(&C, 0, sizeof(C));
	memset(&N, 0, sizeof(N));
	running = 0;
	stub_probe = 0;
	snapshot();
}

static const uint8_t want_dev[18] = {
	0x12, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, 0x40,
	0x25, 0x05, 0xa1, 0xa4, 0x00, 0x01, 0x01, 0x02, 0x00, 0x01
};
/* f_ecm's high-speed layout, written down from f_ecm.c by hand. */
static const uint8_t want_cfg[80] = {
	0x09, 0x02, 0x50, 0x00, 0x02, 0x01, 0x00, 0xc0, 0xfa,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x06, 0x00, 0x00,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x0d, 0x24, 0x0f, 0x03, 0x00, 0x00, 0x00, 0x00, 0xea, 0x05, 0x00,
	0x00, 0x00,
	0x07, 0x05, 0x82, 0x03, 0x10, 0x00, 0x09,
	0x09, 0x04, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
	0x09, 0x04, 0x01, 0x01, 0x02, 0x0a, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
	0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00
};

static void
takeover(void)
{
	static const struct { unsigned ep, cmd; } want_cmds[] = {
		{ 0, DWC3_DEPCMD_DEPSTARTCFG },
		{ 0, DWC3_DEPCMD_SETEPCONFIG },
		{ 0, DWC3_DEPCMD_SETTRANSFRESOURCE },
		{ 1, DWC3_DEPCMD_SETEPCONFIG },
		{ 1, DWC3_DEPCMD_SETTRANSFRESOURCE },
		{ 0, DWC3_DEPCMD_STARTTRANSFER },
	};
	unsigned i, k;
	int r;

	printf("--- take-over, as the boot loader left the block\n");
	as_left();
	r = init();
	CHECK(r == OK && running, "take-over: init %d, running %d", r,
	    running);
	CHECK(M.reset, "take-over: no soft reset");
	CHECK(R(DWC3_GEVNTADRLO) == FAKE_PA + DWC3_OFF_EVENTS &&
	    R(DWC3_GEVNTADRHI) == 0, "events at 0x%x", R(DWC3_GEVNTADRLO));
	CHECK(R(DWC3_GEVNTSIZ) == (DWC3_GEVNTSIZ_INTMASK | 4096),
	    "GEVNTSIZ 0x%x", R(DWC3_GEVNTSIZ));
	CHECK(!(R(DWC3_DCFG) & DWC3_DCFG_LPM_CAP) &&
	    (R(DWC3_DCFG) & 7) == 0 && (R(DWC3_DCFG) & DWC3_DCFG_DEVADDR_MASK)
	    == 0, "DCFG 0x%x", R(DWC3_DCFG));
	CHECK(R(DWC3_DCTL) & DWC3_DCTL_RUN_STOP, "DCTL 0x%x", R(DWC3_DCTL));
	CHECK((R(DWC3_DEVTEN) & (DWC3_DEVTEN_USBRSTEN |
	    DWC3_DEVTEN_CONNECTDONEEN | DWC3_DEVTEN_DISCONNEVTEN)) ==
	    (DWC3_DEVTEN_USBRSTEN | DWC3_DEVTEN_CONNECTDONEEN |
	    DWC3_DEVTEN_DISCONNEVTEN), "DEVTEN 0x%x", R(DWC3_DEVTEN));
	CHECK((R(DWC3_DALEPENA) & 0xff) == 3, "DALEPENA 0x%x",
	    R(DWC3_DALEPENA));
	CHECK(DWC3_GCTL_PRTCAP(R(DWC3_GCTL)) == 2, "GCTL 0x%x",
	    R(DWC3_GCTL));
	CHECK(M.nlog == 6, "%u commands at start", M.nlog);
	for (i = 0; i < 6 && i < M.nlog; i++)
		CHECK(M.log[i].ep == want_cmds[i].ep &&
		    (M.log[i].cmd & 0xf) == want_cmds[i].cmd,
		    "command %u: ep%u %u", i, M.log[i].ep, M.log[i].cmd & 0xf);
	if (M.nlog >= 5) {
		CHECK((M.log[0].cmd >> 16) == 0, "DEPSTARTCFG param %u",
		    M.log[0].cmd >> 16);
		for (k = 1; k <= 3; k += 2)
			CHECK(((M.log[k].p0 >> 3) & 0x7ff) == 512 &&
			    (M.log[k].p0 >> 30) == 0 &&
			    ((M.log[k].p0 >> 1) & 3) == 0 &&
			    ((M.log[k].p0 >> 17) & 0x1f) == 0 &&
			    (M.log[k].p1 >> 25) == M.log[k].ep &&
			    (M.log[k].p1 & (1u << 8)) &&
			    (M.log[k].p1 & (1u << 10)),
			    "SETEPCONFIG ep%u: 0x%x 0x%x", M.log[k].ep,
			    M.log[k].p0, M.log[k].p1);
		CHECK(M.log[2].p0 == 1 && M.log[4].p0 == 1,
		    "transfer resources %u %u", M.log[2].p0, M.log[4].p0);
	}
	CHECK(trb_on(0, DWC3_TRBCTL_SETUP, "start") != NULL, "no SETUP");
	CHECK(same(GCC) && same(PHY) && same(FUSE), "GCC, PHY or QFPROM "
	    "written");
	CHECK(*(uint32_t *)(qsmem + QSCRATCH_HS_PHY_CTRL) ==
	    (QSCRATCH_UTMI_OTG_VBUS_VALID | QSCRATCH_SW_SESSVLD_SEL) &&
	    *(uint32_t *)(qsmem + QSCRATCH_SS_PHY_CTRL) ==
	    QSCRATCH_LANE0_PWR_PRESENT &&
	    *(uint32_t *)(qsmem + QSCRATCH_GENERAL_CFG) == 0xd,
	    "qscratch %x %x %x", *(uint32_t *)(qsmem + QSCRATCH_GENERAL_CFG),
	    *(uint32_t *)(qsmem + QSCRATCH_HS_PHY_CTRL),
	    *(uint32_t *)(qsmem + QSCRATCH_SS_PHY_CTRL));
}

static void
enumeration(void)
{
	const char *prod = "MEECHO USB (dwc3_ecm)";
	uint8_t in[512];
	unsigned i;
	int n;

	printf("--- enumeration, as a Linux host does it\n");
	M.nlog = 0;
	inject(EV_RESET);
	connect_hs();
	CHECK(M.nlog == 2 && (M.log[0].cmd & 0xf) == DWC3_DEPCMD_SETEPCONFIG &&
	    (M.log[0].p0 >> 30) == 2 && ((M.log[0].p0 >> 3) & 0x7ff) == 64 &&
	    M.log[1].ep == 1 && ((M.log[1].p0 >> 3) & 0x7ff) == 64,
	    "connect done: %u commands, maxpacket %u", M.nlog,
	    M.nlog ? (M.log[0].p0 >> 3) & 0x7ff : 0);
	n = req(0x80, 6, 0x0100, 0, 64, in);
	CHECK(n == 18 && memcmp(in, want_dev, 18) == 0, "device (64): %d", n);
	inject(EV_RESET);
	connect_hs();
	n = req(0x00, 5, 5, 0, 0, in);
	CHECK(n == 0 && (R(DWC3_DCFG) >> 3 & 0x7f) == 5 &&
	    (M.dcfg_at_status >> 3 & 0x7f) == 5, "address: %d, DCFG 0x%x, "
	    "at status 0x%x", n, R(DWC3_DCFG), M.dcfg_at_status);
	n = req(0x80, 6, 0x0100, 0, 18, in);
	CHECK(n == 18 && memcmp(in, want_dev, 18) == 0, "device (18): %d", n);
	n = req(0x80, 6, 0x0200, 0, 9, in);
	CHECK(n == 9 && memcmp(in, want_cfg, 9) == 0, "config (9): %d", n);
	n = req(0x80, 6, 0x0200, 0, 80, in);
	CHECK(n == 80 && memcmp(in, want_cfg, 80) == 0, "config (80): %d", n);
	for (i = 0; n == 80 && i < 80; i++)
		if (in[i] != want_cfg[i])
			printf("  config byte %u: 0x%02x, wanted 0x%02x\n", i,
			    in[i], want_cfg[i]);
	n = req(0x80, 6, 0x0300, 0, 255, in);
	CHECK(n == 4 && in[0] == 4 && in[1] == 3 && in[2] == 0x09 &&
	    in[3] == 0x04, "languages: %d", n);
	n = req(0x80, 6, 0x0302, 0x0409, 255, in);
	CHECK(n == 2 + 2 * (int)strlen(prod) && in[0] == n && in[1] == 3,
	    "product: %d", n);
	for (i = 0; n > 0 && i < strlen(prod); i++)
		CHECK(in[2 + 2 * i] == (uint8_t)prod[i] && in[3 + 2 * i] == 0,
		    "product char %u", i);
	n = req(0x80, 6, 0x0301, 0x0409, 255, in);
	CHECK(n == 14 && in[2] == 'M' && in[12] == 'O', "manufacturer: %d", n);
	n = req(0x80, 6, 0x0303, 0x0409, 255, in);
	CHECK(n == 26 && memcmp(in + 2, "0\0006\0004\000D\0004\0005\000",
	    12) == 0 && in[24] == 'F', "MAC string: %d", n);
	n = req(0x80, 6, 0x0f00, 0, 5, in);
	CHECK(n == -1, "BOS: %d, wanted a stall", n);
	n = req(0x80, 6, 0x0600, 0, 10, in);
	CHECK(n == 10 && in[0] == 10 && in[1] == 6 && in[4] == 2 &&
	    in[7] == 64, "qualifier: %d", n);
	n = req(0x80, 0, 0, 0, 2, in);
	CHECK(n == 2 && in[0] == 1 && in[1] == 0, "status: %d %02x", n, in[0]);
	n = req(0xc0, 1, 0, 0, 4, in);
	CHECK(n == -1, "vendor request: %d, wanted a stall", n);
}

/*===========================================================================*
 *    the Ethernet function                                                   *
 *===========================================================================*/
static void
ecm_checks(void)
{
	static const uint8_t conn[8] = { 0xa1, 0, 1, 0, 0, 0, 0, 0 };
	uint8_t in[512], frame[2048], got[2048];
	struct netdriver_data d;
	struct dwc3_trb *t;
	uint32_t media;
	unsigned at = 0, mark, i;
	int n;
	ssize_t r;

	printf("--- the Ethernet function\n");
	mark = M.nlog;
	n = req(0x00, 9, 1, 0, 0, in);
	CHECK(n == 0 && dwc.config == 1, "configuration: %d", n);
	CHECK(logged(mark, 0, DWC3_DEPCMD_DEPSTARTCFG, &at) == 1 &&
	    (M.log[at].cmd >> 16) == 2, "SET_CONFIGURATION: DEPSTARTCFG(2)");
	CHECK(logged(mark, DWC3_EP2_IN, DWC3_DEPCMD_SETEPCONFIG, &at) == 1 &&
	    ((M.log[at].p0 >> 1) & 3) == DWC3_EP_INTR &&
	    ((M.log[at].p0 >> 3) & 0x7ff) == 16 &&
	    ((M.log[at].p0 >> 17) & 0x1f) == 2 &&
	    ((M.log[at].p1 >> 16) & 0xff) == 8, "notification endpoint: "
	    "0x%x 0x%x", M.log[at].p0, M.log[at].p1);
	CHECK(R(DWC3_DALEPENA) & (1u << DWC3_EP2_IN), "ep5 not enabled");
	CHECK(ecm_get_link(&media) == NDEV_LINK_DOWN, "link up before alt 1");
	n = req(0x81, 10, 0, 1, 1, in);
	CHECK(n == 1 && in[0] == 0, "data interface alt: %d %u", n, in[0]);

	n = req(0x21, 0x43, 0x000e, 0, 0, in);
	CHECK(n == 0, "SET_ETHERNET_PACKET_FILTER: %d", n);

	mark = M.nlog;
	N.link = 0;
	n = req(0x01, 11, 1, 1, 0, in);
	CHECK(n == 0, "SET_INTERFACE(1, 1): %d", n);
	CHECK(logged(mark, DWC3_EP1_OUT, DWC3_DEPCMD_SETEPCONFIG, &at) == 1 &&
	    ((M.log[at].p0 >> 1) & 3) == DWC3_EP_BULK &&
	    ((M.log[at].p0 >> 3) & 0x7ff) == 512, "EP1 OUT config");
	CHECK(logged(mark, DWC3_EP1_IN, DWC3_DEPCMD_SETEPCONFIG, &at) == 1 &&
	    ((M.log[at].p0 >> 1) & 3) == DWC3_EP_BULK &&
	    ((M.log[at].p0 >> 17) & 0x1f) == 1, "EP1 IN config");
	CHECK(logged(mark, DWC3_EP1_OUT, DWC3_DEPCMD_SETTRANSFRESOURCE,
	    NULL) == 1 && logged(mark, DWC3_EP1_IN,
	    DWC3_DEPCMD_SETTRANSFRESOURCE, NULL) == 1, "bulk resources");
	CHECK((R(DWC3_DALEPENA) & 0x2c) == 0x2c, "DALEPENA 0x%x",
	    R(DWC3_DALEPENA));
	CHECK(N.link == 1 && ecm_get_link(&media) == NDEV_LINK_UP,
	    "link: %d calls", N.link);
	t = trb_on(DWC3_EP1_OUT, DWC3_TRBCTL_NORMAL, "receive");
	CHECK(t != NULL && (t->size & DWC3_TRB_SIZE_MASK) == 2048 &&
	    (t->ctrl & DWC3_TRB_CTRL_ISP_IMI) && (t->ctrl & DWC3_TRB_CTRL_LST)
	    && (t->ctrl & DWC3_TRB_CTRL_IOC), "receive TRB");
	t = trb_on(DWC3_EP2_IN, DWC3_TRBCTL_NORMAL, "connection");
	CHECK(t != NULL && (t->size & DWC3_TRB_SIZE_MASK) == 8 &&
	    trb_buf(t, 8) != NULL && memcmp(trb_buf(t, 8), conn, 8) == 0,
	    "NETWORK_CONNECTION");
	if (t != NULL)
		finish(DWC3_EP2_IN, t, 0);
	t = trb_on(DWC3_EP2_IN, DWC3_TRBCTL_NORMAL, "speed");
	CHECK(t != NULL && (t->size & DWC3_TRB_SIZE_MASK) == 16 &&
	    trb_buf(t, 16)[1] == 0x2a && trb_buf(t, 16)[6] == 8 &&
	    trb_buf(t, 16)[8] == (uint8_t)(13u * 512 * 64000) &&
	    trb_buf(t, 16)[11] == (uint8_t)((13u * 512 * 64000) >> 24),
	    "SPEED_CHANGE");
	if (t != NULL)
		finish(DWC3_EP2_IN, t, 0);
	CHECK(!M.started[DWC3_EP2_IN], "a third notification");

	/* A frame from the host: 98 bytes, a short packet. */
	for (i = 0; i < sizeof(frame); i++)
		frame[i] = (uint8_t)(i * 7 + 3);
	t = trb_on(DWC3_EP1_OUT, DWC3_TRBCTL_NORMAL, "receive");
	if (t != NULL) {
		memcpy(trb_buf(t, 98), frame, 98);
		N.recv = 0;
		finish(DWC3_EP1_OUT, t, 2048 - 98);
	}
	CHECK(N.recv == 1, "netdriver_recv() %d times", N.recv);
	d.buf = got;
	d.len = sizeof(got);
	memset(got, 0, sizeof(got));
	r = ecm_recv(&d, 1514);
	CHECK(r == 98 && memcmp(got, frame, 98) == 0, "received %zd", r);
	CHECK(ecm_recv(&d, 1514) == SUSPEND, "a second frame from nowhere");
	CHECK(trb_on(DWC3_EP1_OUT, DWC3_TRBCTL_NORMAL, "receive again") !=
	    NULL, "receive not queued again");
	/* A runt is dropped and the TRB queued again. */
	t = trb_on(DWC3_EP1_OUT, DWC3_TRBCTL_NORMAL, "receive");
	if (t != NULL) {
		N.recv = 0;
		finish(DWC3_EP1_OUT, t, 2048 - 5);
	}
	CHECK(N.recv == 0 && N.ierr == 1 && trb_on(DWC3_EP1_OUT,
	    DWC3_TRBCTL_NORMAL, "after a runt") != NULL, "runt: %d %u",
	    N.recv, N.ierr);

	/* A frame to the host, then one that waits for it. */
	d.buf = frame;
	CHECK(ecm_send(&d, 100) == OK, "send 100");
	t = trb_on(DWC3_EP1_IN, DWC3_TRBCTL_NORMAL, "transmit");
	CHECK(t != NULL && (t->size & DWC3_TRB_SIZE_MASK) == 100 &&
	    (t->ctrl & DWC3_TRB_CTRL_LST) && (t->ctrl & DWC3_TRB_CTRL_IOC) &&
	    memcmp(trb_buf(t, 100), frame, 100) == 0, "transmit TRB");
	CHECK(ecm_send(&d, 60) == SUSPEND, "a second frame did not wait");
	if (t != NULL) {
		N.send = 0;
		finish(DWC3_EP1_IN, t, 0);
	}
	CHECK(N.send == 1, "netdriver_send() %d times", N.send);
	/* 512 bytes: the frame, and a zero-length packet after it. */
	CHECK(ecm_send(&d, 512) == OK, "send 512");
	t = trb_on(DWC3_EP1_IN, DWC3_TRBCTL_NORMAL, "transmit 512");
	CHECK(t != NULL && (t[0].size & DWC3_TRB_SIZE_MASK) == 512 &&
	    !(t[0].ctrl & DWC3_TRB_CTRL_LST) &&
	    (t[1].size & DWC3_TRB_SIZE_MASK) == 0 &&
	    (t[1].ctrl & DWC3_TRB_CTRL_HWO) && (t[1].ctrl & DWC3_TRB_CTRL_LST)
	    && (t[1].ctrl & DWC3_TRB_CTRL_IOC) &&
	    (t[1].ctrl & 0x3f0) == DWC3_TRBCTL_NORMAL, "zero-length packet");
	if (t != NULL)
		finish(DWC3_EP1_IN, t, 0);

	/* Alt 0: the bulk pair goes, the link with it. */
	mark = M.nlog;
	N.link = 0;
	n = req(0x01, 11, 0, 1, 0, in);
	CHECK(n == 0 && logged(mark, DWC3_EP1_OUT, DWC3_DEPCMD_ENDTRANSFER,
	    NULL) == 1 && !(R(DWC3_DALEPENA) & 0x0c) && N.link == 1 &&
	    ecm_get_link(&media) == NDEV_LINK_DOWN, "SET_INTERFACE(1, 0)");
	mark = M.nlog;
	N.oerr = 0;
	CHECK(ecm_send(&d, 100) == OK && N.oerr == 1 &&
	    logged(mark, DWC3_EP1_IN, DWC3_DEPCMD_STARTTRANSFER, NULL) == 0,
	    "a frame with the link down");

	/* Open again, then a bus reset takes everything away. */
	n = req(0x01, 11, 1, 1, 0, in);
	CHECK(n == 0 && ecm_get_link(&media) == NDEV_LINK_UP, "alt 1 again");
	inject(EV_RESET);
	CHECK(ecm_get_link(&media) == NDEV_LINK_DOWN &&
	    !(R(DWC3_DALEPENA) & 0x2c), "bus reset: link %u, DALEPENA 0x%x",
	    ecm_get_link(&media), R(DWC3_DALEPENA));
	CHECK(M.errors == 0, "the model saw %u errors", M.errors);
	CHECK(same(GCC) && same(PHY) && same(FUSE), "GCC, PHY or QFPROM "
	    "written");
}

static void
refusal(void)
{
	printf("--- the PHY's PLL not locked: nothing taken over\n");
	as_left();
	w32(phymem, 0x38, 0x00);
	snapshot();
	CHECK(init() == ENXIO && !running, "taken over anyway");
	CHECK(M.nlog == 0 && same(CORE) && same(QS), "the core or wrapper "
	    "written: %u commands", M.nlog);
	stub_probe = 1;
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

	/* probe=1: the report, and the driver leaves (ENXIO). */
	blocks();
	gcc_state(0, 0);
	probe("GCC: everything off", ENXIO, 0, 0);
	CHECK(reg[GCC].mapped && reg[FUSE].mapped, "GCC and QFPROM mapped");

	blocks();
	gcc_state(1, 1);
	probe("GCC: everything on", ENXIO, 1, 1);
	tree_checks();

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0x12000, 1);		/* the PHY's reset held */
	probe("GCC: on, the PHY held in reset", ENXIO, 1, 0);

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0xf000, 1);			/* the wrapper's reset held */
	probe("GCC: on, the wrapper held in reset", ENXIO, 0, 1);

	blocks();
	gcc_state(1, 1);
	w32(gccmem, 0xf010, 0x80000000u);	/* mock_utmi off */
	probe("GCC: on but mock_utmi", ENXIO, 0, 1);

	blocks();
	gcc_state(1, 0);
	probe("GCC: clocks on, power domain off", ENXIO, 0, 1);

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

	takeover();
	enumeration();
	ecm_checks();
	refusal();

	printf("dwtest: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}
