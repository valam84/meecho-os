/*
 * cpuprobe: what the Mi Pad 4's boot chain leaves in the blocks that set
 * the CPU clusters' clock and voltage - read, and only read
 * (port/clover/hw-cpu.md, step 1).  Started as a service, it runs one
 * stage, prints what it read to the kernel log and leaves; a run that
 * started nothing reports ENXIO to RS, as the adreno stages do.
 *
 *	minix-service up /root/cpuprobe/cpuprobe \
 *	    -config /root/cpuprobe/cpuprobe.conf -args stage=fuse
 *
 *   stage=fuse   the qfprom rows the vendor's CPRh and OSM drivers read:
 *                row 38 (speed bin) and rows 65..71 (open-loop voltages,
 *                quotients, CPR revision)
 *   stage=pmic   PM660 S1 (VDD_APC0, silver) and S2/S3 (VDD_APC1, gold)
 *                through the SPMI arbiter's observer channels: type,
 *                voltage, mode, enable, and which EE owns each one
 *   stage=apcs   GCC's CPR and GPLL0 roots; each cluster's RCG, PLL and
 *                L2 SAW
 *   stage=apm    the array power mux and the APCS common block (OSM clock)
 *   stage=osm    the OSM of both clusters - only if its clock root runs
 *                (force=1 reads it anyway)
 *   stage=cprh   both CPRh controllers - only if GCC_HMSS_RBCPR_CLK runs
 *                (force=1 reads them anyway); only the registers the
 *                vendor driver reads
 *   stage=rails  PM660L S1-S5, among them VDD_CX (S3+S4) and VDD_MX (S5),
 *                the same way as stage=pmic
 *   tag=N        printed in the stage's first line, so that a reader of
 *                the kernel's ring tells this run from an earlier one
 *
 * Every register read here is one the vendor kernel reads or writes on
 * this SoC (clk-cpu-osm.c, cprh-kbss-regulator.c, cpr3-regulator.c,
 * msm-spm.c, qcom_spmi-regulator.c), and nothing is written: not a
 * register, not a PMIC byte.  The SPMI reads use the observer channels of
 * the three regulators only, which qcom_pmic never uses, so the two
 * services do not share a channel.
 *
 * The system.conf entry keeps it off the boot processor ("cpu 1"), as the
 * adreno probe: a read that stalled the bus would stall this core, and the
 * boot processor's tick - with the boot watchdog - would go on.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/sysutil.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#include "spmi.h"

#define SID_PM660_REGS	1		/* PM660's regulators (hw-rpm.md) */
#define SID_PM660L_REGS	3		/* PM660L's */

static int force;
static long tag;
static void *dtb;

/*===========================================================================*
 *    the device tree and the mappings                                       *
 *===========================================================================*/
#define MAX_REGS	12

struct node {
	const char *compat;
	int found;
	unsigned n;
	u64_t addr[MAX_REGS], size[MAX_REGS];
	char name[MAX_REGS][24];
	volatile uint8_t *v[MAX_REGS];
};

static int
find_node(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct node *nd = cookie;
	const char *names, *p;
	unsigned len, i;

	if (!fdt_node_is_compatible(node, nd->compat))
		return 0;
	for (i = 0; i < MAX_REGS &&
	    fdt_node_reg(node, i, &nd->addr[i], &nd->size[i]) == 0; i++)
		nd->name[i][0] = '\0';
	nd->n = i;
	if ((names = fdt_getprop(node, "reg-names", &len)) != NULL)
		for (p = names, i = 0; p < names + len && i < nd->n;
		    p += strlen(p) + 1, i++)
			strlcpy(nd->name[i], p, sizeof(nd->name[i]));
	nd->found = 1;
	return 1;
}

static int
lookup(struct node *nd, const char *compat)
{
	memset(nd, 0, sizeof(*nd));
	nd->compat = compat;
	fdt_walk(dtb, find_node, nd);
	if (!nd->found) {
		printf("cpuprobe: no node \"%s\" in the tree\n", compat);
		return ENOENT;
	}
	return OK;
}

/* Region i of a node, mapped (Device memory, as VM maps all of this). */
static volatile uint8_t *
region(struct node *nd, unsigned i)
{
	void *v;

	if (i >= nd->n)
		return NULL;
	if (nd->v[i] != NULL)
		return nd->v[i];
	v = vm_map_phys(SELF, (void *)(phys_bytes)nd->addr[i],
	    (size_t)nd->size[i]);
	if (v == MAP_FAILED) {
		printf("cpuprobe: cannot map %s 0x%llx+0x%llx: not granted?\n",
		    nd->name[i][0] ? nd->name[i] : "region",
		    (unsigned long long)nd->addr[i],
		    (unsigned long long)nd->size[i]);
		return NULL;
	}
	return nd->v[i] = v;
}

static volatile uint8_t *
named(struct node *nd, const char *name)
{
	unsigned i;

	for (i = 0; i < nd->n; i++)
		if (strcmp(nd->name[i], name) == 0)
			return region(nd, i);
	printf("cpuprobe: %s has no region \"%s\"\n", nd->compat, name);
	return NULL;
}

static uint32_t
rd(volatile uint8_t *base, unsigned off)
{
	return *(volatile uint32_t *)(base + off);
}

/* One line: the offsets given, label and value each. */
static void
dump(const char *what, volatile uint8_t *base, const unsigned *off,
	unsigned n)
{
	unsigned i;

	printf("cpuprobe: %s", what);
	for (i = 0; i < n; i++) {
		if (i > 0 && i % 4 == 0)
			printf("\ncpuprobe: %s", what);
		printf(" %03x=%08x", off[i], rd(base, off[i]));
	}
	printf("\n");
}

/* A range, 32-bit words; only the non-zero ones when sparse. */
static void
dump_range(const char *what, volatile uint8_t *base, unsigned from,
	unsigned to, int sparse)
{
	unsigned off, col = 0, zeros = 0;
	uint32_t v;

	for (off = from; off < to; off += 4) {
		v = rd(base, off);
		if (sparse && v == 0) {
			zeros++;
			continue;
		}
		if (col % 4 == 0)
			printf("%scpuprobe: %s", col ? "\n" : "", what);
		printf(" %04x=%08x", off, v);
		col++;
	}
	if (col)
		printf("\n");
	if (sparse)
		printf("cpuprobe: %s 0x%04x..0x%04x: %u word(s) zero\n", what,
		    from, to - 4, zeros);
}

/* A long range in two lines: how many words are not zero, and the first. */
static void
summary(const char *what, volatile uint8_t *base, unsigned from, unsigned to)
{
	unsigned off, nz = 0, shown = 0;
	uint32_t v;

	for (off = from; off < to; off += 4)
		if (rd(base, off) != 0)
			nz++;
	printf("cpuprobe: %s 0x%04x..0x%04x: %u of %u word(s) not zero\n",
	    what, from, to - 4, nz, (to - from) / 4);
	printf("cpuprobe: %s first:", what);
	for (off = from; off < to && shown < 8; off += 4)
		if ((v = rd(base, off)) != 0) {
			printf(" %04x=%08x", off, v);
			shown++;
		}
	printf("\n");
}

/*===========================================================================*
 *    stages                                                                 *
 *===========================================================================*/
/* qfprom@780000; the CPR drivers' fuse base is +0x4000 (0x784000). */
#define FUSE_BASE	0x4000
#define FUSE_ROW(r)	(FUSE_BASE + 8 * (r))

static int
stage_fuse(void)
{
	static const unsigned rows[] = { 38, 65, 66, 67, 68, 69, 70, 71 };
	volatile uint8_t *q;
	struct node nd;
	uint64_t v;
	unsigned i;

	if (lookup(&nd, "qcom,qfprom") != OK || (q = region(&nd, 0)) == NULL)
		return ENXIO;
	if (nd.size[0] < FUSE_ROW(72)) {
		printf("cpuprobe: qfprom is 0x%llx bytes: rows not in it\n",
		    (unsigned long long)nd.size[0]);
		return ENXIO;
	}
	for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		v = (uint64_t)rd(q, FUSE_ROW(rows[i]) + 4) << 32 |
		    rd(q, FUSE_ROW(rows[i]));
		printf("cpuprobe: fuse row %u (0x%x) = 0x%016llx\n", rows[i],
		    0x780000 + FUSE_ROW(rows[i]), (unsigned long long)v);
	}
	v = rd(q, FUSE_ROW(38));
	printf("cpuprobe: speed bin (row 38, bits 31:29) = %u\n",
	    (unsigned)(v >> 29) & 7);
	return OK;
}

static struct spmi spmi;

static void
delay_us(unsigned us)
{
	(void)micro_delay(us);
}

struct smps {
	const char *name, *rail;
	unsigned sid, base;
};

static int
spmi_up(void)
{
	static const char *const want[4] = { "core", "chnls", "obsrvr",
	    "cnfg" };
	volatile uint8_t *m;
	struct node nd;
	unsigned i, k;
	int r;

	if (lookup(&nd, "qcom,spmi-pmic-arb") != OK)
		return ENXIO;
	memset(&spmi, 0, sizeof(spmi));
	for (k = 0; k < 4; k++) {
		for (i = 0; i < nd.n && strcmp(nd.name[i], want[k]) != 0; i++)
			;
		if (i == nd.n || (m = region(&nd, i)) == NULL)
			return ENXIO;
		switch (k) {
		case 0: spmi.core = m; spmi.core_size = nd.size[i]; break;
		case 1: spmi.chnls = m; spmi.chnls_size = nd.size[i]; break;
		case 2: spmi.obsrvr = m; spmi.obsrvr_size = nd.size[i]; break;
		case 3: spmi.cnfg = m; spmi.cnfg_size = nd.size[i]; break;
		}
	}
	spmi.ee = 0;
	spmi.delay_us = delay_us;
	if ((r = spmi_init(&spmi)) != OK_SPMI) {
		printf("cpuprobe: SPMI arbiter 0x%08x: not v2/v3 (%d)\n",
		    spmi.version, r);
		return ENXIO;
	}
	printf("cpuprobe: SPMI arbiter 0x%08x, %u channels\n", spmi.version,
	    spmi.napid);
	return OK;
}

/*
 * Each regulator's three peripherals (CTRL, PS, FREQ): channel, owner,
 * type; then the CTRL registers, and the set point decoded where the
 * layout is known.  Reads through the observer channels only.
 */
static int
smps_report(const struct smps *s, unsigned n)
{
	static const unsigned regs[] = { 0x01, 0x04, 0x05, 0x08, 0x09, 0x40,
	    0x41, 0x45, 0x46, 0x48, 0x4c, 0x61, 0x68, 0x69 };
	unsigned i, j, k, a;
	uint8_t b[sizeof(regs) / sizeof(regs[0])], t[2];

	if (spmi_up() != OK)
		return ENXIO;
	for (i = 0; i < n; i++) {
		for (k = 0; k < 3; k++) {
			unsigned base = s[i].base + 0x100 * k;

			a = spmi.apid_of[(s[i].sid << 8) | ((base >> 8) & 0xff)];
			if (a == SPMI_NO_APID) {
				printf("cpuprobe: %s 0x%04x: no channel\n",
				    s[i].name, base);
				continue;
			}
			if (spmi_read(&spmi, s[i].sid, base + 0x04, t, 2)
			    != OK_SPMI)
				t[0] = t[1] = 0xff;
			printf("cpuprobe: %s 0x%04x: apid %u, owner EE %u, "
			    "type 0x%02x subtype 0x%02x\n", s[i].name, base, a,
			    spmi.owner[a], t[0], t[1]);
		}
		for (j = 0; j < sizeof(regs) / sizeof(regs[0]); j++)
			if (spmi_read(&spmi, s[i].sid, s[i].base + regs[j],
			    &b[j], 1) != OK_SPMI)
				b[j] = 0xff;	/* printed; tells by its look */
		printf("cpuprobe: %s %s:", s[i].name, s[i].rail);
		for (j = 0; j < sizeof(regs) / sizeof(regs[0]); j++)
			printf(" %02x=%02x", regs[j], b[j]);
		printf("\n");
		/*
		 * Type 0x1c subtype 0x0a is FTS426, type 0x03 subtype 0x0a
		 * HFS430: both keep a 16-bit set point in mV at 0x40/0x41
		 * (qcom_spmi-regulator.c).  FTS and FTS2.5: range and VSET.
		 */
		if (b[2] == 0x0a && (b[1] == 0x1c || b[1] == 0x03))
			printf("cpuprobe: %s %s set point %u mV, %s, mode "
			    "0x%02x\n", s[i].name, b[1] == 0x1c ? "FTS426" :
			    "HFS430", (unsigned)b[6] << 8 | b[5],
			    (b[8] & 0x80) ? "enabled" : "disabled", b[7]);
		else if (b[1] == 0x1c && (b[2] == 0x08 || b[2] == 0x09))
			printf("cpuprobe: %s FTS%s range %u vset %u, %s\n",
			    s[i].name, b[2] == 0x09 ? "2.5" : "", b[5], b[6],
			    (b[8] & 0x80) ? "enabled" : "disabled");
		else
			printf("cpuprobe: %s type 0x%02x subtype 0x%02x not "
			    "decoded\n", s[i].name, b[1], b[2]);
	}
	return OK;
}

/* PM660 S1-S3: the CPU rails. */
static int
stage_pmic(void)
{
	static const struct smps s[] = {
		{ "S1", "VDD_APC0 (silver)", SID_PM660_REGS, 0x1400 },
		{ "S2", "VDD_APC1 (gold)", SID_PM660_REGS, 0x1700 },
		{ "S3", "VDD_APC1 (gold, second phase)", SID_PM660_REGS,
		    0x1a00 },
	};

	return smps_report(s, sizeof(s) / sizeof(s[0]));
}

/*
 * PM660L S1-S5 (SID 3): S3+S4 are VDD_CX, S5 VDD_MX - the rail the CPU
 * clusters' memory arrays run from while APM is in MX mode.
 */
static int
stage_rails(void)
{
	static const struct smps s[] = {
		{ "PM660L-S1", "1.125 V", SID_PM660L_REGS, 0x1400 },
		{ "PM660L-S2", "1.05 V", SID_PM660L_REGS, 0x1700 },
		{ "PM660L-S3", "VDD_CX", SID_PM660L_REGS, 0x1a00 },
		{ "PM660L-S4", "VDD_CX (second phase)", SID_PM660L_REGS,
		    0x1d00 },
		{ "PM660L-S5", "VDD_MX", SID_PM660L_REGS, 0x2000 },
	};

	return smps_report(s, sizeof(s) / sizeof(s[0]));
}

/* GCC: the CPR controllers' clock and the 300 MHz alternate source. */
#define GCC_HMSS_RBCPR_CBCR	0x48008	/* bit31 CLK_OFF, bit0 enable */
#define GCC_HMSS_RBCPR_CMD	0x48044
#define GCC_HMSS_GPLL0_CMD	0x4805c
#define CBCR_CLK_OFF		(1u << 31)
#define RCG_ROOT_OFF		(1u << 31)

static int
rbcpr_on(uint32_t *cbcr)
{
	volatile uint8_t *g;
	struct node nd;

	if (lookup(&nd, "qcom,gcc-sdm660") != OK || (g = region(&nd, 0)) == NULL)
		return -1;
	*cbcr = rd(g, GCC_HMSS_RBCPR_CBCR);
	return !(*cbcr & CBCR_CLK_OFF);
}

static int
stage_apcs(void)
{
	static const unsigned gcc[] = { GCC_HMSS_RBCPR_CBCR,
	    GCC_HMSS_RBCPR_CMD, GCC_HMSS_RBCPR_CMD + 4,
	    GCC_HMSS_GPLL0_CMD, GCC_HMSS_GPLL0_CMD + 4 };
	static const unsigned rcg[] = { 0x50, 0x54 };
	static const unsigned saw[] = { 0x000, 0x004, 0x00c, 0x040, 0x044,
	    0x048, 0x04c, 0x050, 0x054, 0x900, 0x904, 0x908, 0x90c, 0xc00,
	    0xc04, 0xc0c, 0xc10, 0xc14, 0xc18, 0xfd0 };
	static const char *const cl[2] = { "silver", "gold" };
	volatile uint8_t *g, *v;
	struct node nd, dv;
	char what[32];
	unsigned c;

	if (lookup(&nd, "qcom,gcc-sdm660") != OK || (g = region(&nd, 0)) == NULL)
		return ENXIO;
	dump("gcc", g, gcc, sizeof(gcc) / sizeof(gcc[0]));
	if (lookup(&dv, "meecho,sdm660-cpu-dvfs") != OK)
		return ENXIO;
	for (c = 0; c < 2; c++) {
		snprintf(what, sizeof(what), "%s-glb", cl[c]);
		if ((v = named(&dv, what)) != NULL)
			dump(what, v, rcg, 2);
		snprintf(what, sizeof(what), "%s-pll", cl[c]);
		if ((v = named(&dv, what)) != NULL)
			dump_range(what, v, 0x00, 0x40, 0);
		snprintf(what, sizeof(what), "%s-saw", cl[c]);
		if ((v = named(&dv, what)) != NULL)
			dump(what, v, saw, sizeof(saw) / sizeof(saw[0]));
	}
	return OK;
}

/* APCS common: APM at +0x0, the OSM's clock root at +0x112c (LMH). */
#define APCS_OSM_CMD_RCGR	0x112c
#define APCS_ITM_PRESENT	0x143c

static int
osm_clock_on(struct node *dv, uint32_t *cmd)
{
	volatile uint8_t *v;

	if ((v = named(dv, "apcs-common")) == NULL)
		return -1;
	*cmd = rd(v, APCS_OSM_CMD_RCGR);
	return !(*cmd & RCG_ROOT_OFF);
}

static int
stage_apm(void)
{
	static const unsigned apm[] = { 0x000, 0x004, 0x008, 0x00c, 0x010,
	    0x014, 0x018, 0x01c };
	/*
	 * And the memory accelerator's settings, where the vendor's MSM8998
	 * tree has them (qcom,pwrcl/perfcl-apcs-mem-acc-cfg, 0x179d1360..70):
	 * the SDM660's tree names none, its TrustZone sets them; whether they
	 * are here too is a guess the reading tests.
	 */
	static const unsigned com[] = { APCS_OSM_CMD_RCGR,
	    APCS_OSM_CMD_RCGR + 4, APCS_ITM_PRESENT, 0x1360, 0x1364, 0x1368,
	    0x136c, 0x1370 };
	volatile uint8_t *v;
	struct node dv;

	if (lookup(&dv, "meecho,sdm660-cpu-dvfs") != OK ||
	    (v = named(&dv, "apcs-common")) == NULL)
		return ENXIO;
	dump("apm", v, apm, sizeof(apm) / sizeof(apm[0]));
	dump("apcs-common", v, com, sizeof(com) / sizeof(com[0]));
	return OK;
}

/* Per cluster: silver at +0, gold at +0x2000 (OSM_CORE_TABLE_SIZE). */
#define OSM_CLUSTER(c)		((c) * 0x2000)
#define OSM_LUT(i)		(0x1150 + 32 * (i))	/* index, freq, volt,
							 * override, -, spare */

static int
stage_osm(void)
{
	static const unsigned reg[] = { 0x0000, 0x1004, 0x100c, 0x101c,
	    0x1020, 0x1024, 0x1028, 0x102c, 0x1030, 0x1034, 0x1038, 0x103c,
	    0x1040, 0x1070, 0x10a4, 0x10c0, 0x1c00, 0x1c74, 0x1f00, 0x1f04,
	    0x1f10, 0x1f14, 0x1f2c, 0x1f38 };
	static const char *const cl[2] = { "osm-silver", "osm-gold" };
	volatile uint8_t *v;
	struct node dv;
	uint32_t cmd = 0, f, vl, o, s;
	unsigned c, i, rows;
	int on;

	if (lookup(&dv, "meecho,sdm660-cpu-dvfs") != OK ||
	    (on = osm_clock_on(&dv, &cmd)) < 0)
		return ENXIO;
	if (!on) {
		printf("cpuprobe: the OSM's clock root is off (CMD_RCGR "
		    "0x%08x): %s\n", cmd, force ? "reading anyway" :
		    "not reading the OSM (force=1 does)");
		if (!force)
			return OK;
	}
	if ((v = named(&dv, "osm")) == NULL)
		return ENXIO;
	for (c = 0; c < 2; c++) {
		dump(cl[c], v + OSM_CLUSTER(c), reg,
		    sizeof(reg) / sizeof(reg[0]));
		for (i = rows = 0; i < 40; i++) {
			f = rd(v, OSM_CLUSTER(c) + OSM_LUT(i) + 0x04);
			vl = rd(v, OSM_CLUSTER(c) + OSM_LUT(i) + 0x08);
			o = rd(v, OSM_CLUSTER(c) + OSM_LUT(i) + 0x0c);
			s = rd(v, OSM_CLUSTER(c) + OSM_LUT(i) + 0x14);
			if (f == 0 && vl == 0 && o == 0 && s == 0)
				continue;
			printf("cpuprobe: %s lut %2u: freq %08x volt %08x "
			    "override %08x spare %08x\n", cl[c], i, f, vl, o, s);
			rows++;
		}
		printf("cpuprobe: %s lut: %u row(s) not zero\n", cl[c], rows);
		/*
		 * The sequencer: its registers SEQ_REG(0..95), its branch
		 * table and its program.  Without qcom,osm-no-tz the vendor
		 * kernel leaves the program and most registers to TrustZone,
		 * and the program is the question: there, or not.
		 */
		dump_range(cl[c], v + OSM_CLUSTER(c), 0x300, 0x480, 1);
		dump_range(cl[c], v + OSM_CLUSTER(c), 0x170, 0x234, 1);
		summary(cl[c], v + OSM_CLUSTER(c), 0x500, 0x900);
	}
	return OK;
}

static int
stage_cprh(void)
{
	/*
	 * Only what cpr3-regulator.c itself reads, or reads to modify:
	 * reading CONT_CMD (0x800), which it only ever writes, took an
	 * SError on the tablet (2026-10-18; hw-cpu.md, section 12), and the
	 * corner table (0x3a00) it also only writes.
	 */
	static const unsigned reg[] = { 0x000, 0x004, 0x008, 0x00c, 0x014,
	    0x700, 0x7a4, 0x7f8, 0x81c, 0x3004, 0x3008, 0x3aa0, 0x3aa4 };
	static const char *const cl[2] = { "cprh-silver", "cprh-gold" };
	volatile uint8_t *v;
	struct node dv;
	uint32_t cbcr = 0;
	unsigned c;

	if (rbcpr_on(&cbcr) != 1) {
		printf("cpuprobe: GCC_HMSS_RBCPR_CLK is off (CBCR 0x%08x): %s\n",
		    cbcr, force ? "reading anyway" :
		    "not reading the CPRh (force=1 does)");
		if (!force)
			return OK;
	}
	if (lookup(&dv, "meecho,sdm660-cpu-dvfs") != OK)
		return ENXIO;
	for (c = 0; c < 2; c++) {
		if ((v = named(&dv, cl[c])) == NULL)
			continue;
		dump(cl[c], v, reg, sizeof(reg) / sizeof(reg[0]));
	}
	return OK;
}

/*===========================================================================*
 *    the service                                                            *
 *===========================================================================*/
static const struct { const char *name; int (*run)(void); } stages[] = {
	{ "fuse", stage_fuse }, { "pmic", stage_pmic }, { "apcs", stage_apcs },
	{ "apm", stage_apm }, { "osm", stage_osm }, { "cprh", stage_cprh },
	{ "rails", stage_rails },
};

static int
cpuprobe_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	char name[16];
	unsigned i;
	int r;

	if (env_get_param("stage", name, sizeof(name)) != OK) {
		printf("cpuprobe: stage= is required\n");
		return EINVAL;
	}
	for (i = 0; i < sizeof(stages) / sizeof(stages[0]) &&
	    strcmp(stages[i].name, name) != 0; i++)
		;
	if (i == sizeof(stages) / sizeof(stages[0])) {
		printf("cpuprobe: no stage %s\n", name);
		return EINVAL;
	}
	if ((dtb = fdt_fetch()) == NULL) {
		printf("cpuprobe: no device tree\n");
		return ENXIO;
	}
	/* tag= tells this run's lines from an earlier one's in the ring. */
	printf("cpuprobe: stage %s (tag %ld)\n", name, tag);
	r = stages[i].run();
	free(dtb);
	printf("cpuprobe: stage %s: %s (%d)\n", name, r == OK ? "done" :
	    "failed", r);
	/* A stage is the whole job: ENXIO is what RS expects of a leaver. */
	return r == OK ? ENXIO : r;
}

int
main(int argc, char **argv)
{
	long v;

	env_setargs(argc, argv);
	if (env_parse("force", "d", 0, &v, 0, 1) == EP_SET)
		force = (int)v;
	if (env_parse("tag", "d", 0, &v, 0, 1000000) == EP_SET)
		tag = v;
	sef_setcb_init_fresh(cpuprobe_init);
	sef_startup();
	return 0;
}
