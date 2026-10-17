/*
 * The GPU's SMMU, written (port/clover/hw-gpu.md, §4 and §13).  The first
 * round on the tablet found it on and unforgiving - unmatched streams
 * fault (sCR0.USFCFG), no SMR routes the GPU's stream, context bank 0 all
 * zeros - so the GPU can reach no memory at all until this driver routes
 * its stream through a bank of its own.  Before the command processor is
 * ever started, three questions, each its own stage and each only after
 * the one before it answered:
 *
 *   smr    Can this side write the stream-matching table at all, or does
 *          someone above it (a hypervisor, TrustZone) drop or refuse the
 *          writes?  Linux's own probe question (arm_smmu_test_smr_masks()):
 *          a free SMR - one without VALID, so that nothing is ever routed
 *          by it - written with every ID bit, then every MASK bit, its
 *          S2CR with another value; each read back, all put back.   (Q13)
 *
 *   cb0    Context bank 0 set up as Linux sets it up for the GPU, over
 *          page tables in this service's memory (adreno_pt.c) - and the
 *          SMMU asked, by its own address translation operations (ATS1*),
 *          what the tables say.  These need no GPU and no command
 *          processor: the SMMU walks the tables in memory itself, so a
 *          right answer proves the format, the bank's registers, the
 *          cleaning of the tables to memory and the attributes.  A mapping
 *          is then removed and the TLB invalidated, and the operation must
 *          fault.  Then the GPU's stream (SID 0) is routed to the bank for
 *          10 ms and taken back: the SMR with VALID, read back.    (Q14,Q15)
 *
 *   fault  The bank's fault line: quiet while nothing is wrong; a fault
 *          raised in the bank by FSRRESTORE (the architecture's register
 *          for restoring the fault state, and the way Qualcomm's own
 *          interrupt self-test raises a bank's line) must come in on the
 *          line the device tree names for bank 0 (INTID 361), and the line
 *          must fall quiet again once the fault is cleared.  No GPU access
 *          is behind this fault - one through the GPU needs the command
 *          processor (step 5 of the plan).                           (Q16)
 *
 * Everything written is remembered and put back by smmu_undo(), which then
 * reads it all again against what was found (Q17), whatever the outcome -
 * the SMMU may keep its registers when the GX domain goes off.
 *
 * Registers and values: Linux's arm-smmu.c, arm-smmu-qcom.c and
 * io-pgtable-arm.c (GPL; the offsets, bits and order, not the code).
 */

#include "adreno.h"
#include "adreno_regs.h"

#include <minix/cachectl.h>

#include <string.h>

#define NELEM(a)	(sizeof(a) / sizeof((a)[0]))

#define GR0(r)		ad_rd(&ad.smmu, (r))
#define CB(r)		ad_rd(&ad.smmu, g.cb0 + (r))
#define CB_WR(r, v)	ad_wr(&ad.smmu, g.cb0 + (r), (v))

/* Linux's MAIR for a stage-1 bank: 0 normal non-cacheable (0x44), 1 write-
 * back (0xff), 2 device (0x04), 3 inner non-cacheable, outer write-back
 * (0xf4).  The GPU's buffers are index 0. */
#define MAIR0_LINUX	0xf404ff44u

#define GPU_ASID	0		/* bank 0's, as Linux gives it */
#define VA_A		0x0000000001000000ull	/* 16 MiB, where Linux starts */
#define VA_B		0x0000a5a5a5a5a000ull	/* every level's index odd */
#define VA_C		(VA_A + 0x1000)		/* never mapped */
#define VA_D		0x0000400000000000ull	/* never mapped, its root
						 * entry in a line of its own */

/* The SMMU as its identification registers describe it. */
static struct {
	int ready;
	uint32_t id0, id1, id2;
	unsigned pgshift, nsmr, ncb, sidbits;
	unsigned ias, oas, pasize;
	uint32_t gr1, cb0;		/* offsets of global space 1, bank 0 */
} g;

/* What was found, for smmu_undo() to put back. */
static struct {
	int smr;			/* the entry written, -1 none */
	uint32_t smr_v, s2cr_v;
	int sme_touched, cb_touched;
	uint32_t cba2r, cbar, tcr2, tcr, mair0, mair1, sctlr;
	uint64_t ttbr0, ttbr1;
	struct ad_pt pt;
	void *page[2];
	phys_bytes page_pa[2];
	int hook, hooked;
} u = { .smr = -1 };

static unsigned
size_bits(unsigned code)
{
	static const unsigned bits[] = { 32, 36, 40, 42, 44, 48 };

	return code < NELEM(bits) ? bits[code] : 48;
}

static int
geom(void)
{
	unsigned numpage;

	if (g.ready)
		return OK;
	g.id0 = GR0(SMMU_ID0);
	g.id1 = GR0(SMMU_ID1);
	g.id2 = GR0(SMMU_ID2);
	g.pgshift = (g.id1 & ID1_PAGESIZE) ? 16 : 12;
	numpage = 1u << (ID1_NUMPAGENDXB(g.id1) + 1);
	g.nsmr = ID0_NUMSMRG(g.id0);
	g.ncb = ID1_NUMCB(g.id1);
	g.sidbits = ID0_NUMSIDB(g.id0);
	g.gr1 = 1u << g.pgshift;
	g.cb0 = numpage << g.pgshift;
	/* Stage 1 with stage 2 bypassed: in, the bus's width (UBS); out, the
	 * SMMU's intermediate width (IAS), as Linux takes them. */
	g.ias = size_bits(ID2_UBS(g.id2));
	if (g.ias > 48)
		g.ias = 48;
	g.pasize = ID2_IAS(g.id2);
	g.oas = size_bits(g.pasize);
	if (g.cb0 + (1u << g.pgshift) > ad.smmu.size) {
		log_warn(&ad_log, "smmu: bank 0 at +0x%x is outside the window "
		    "(0x%zx); refusing\n", g.cb0, ad.smmu.size);
		return EINVAL;
	}
	g.ready = 1;
	return OK;
}

/* A routing entry that routes nothing: the first SMR without VALID. */
static int
free_smr(void)
{
	unsigned i;

	for (i = 0; i < g.nsmr && i < 128; i++)
		if (!(GR0(SMMU_SMR(i)) & SMR_VALID))
			return (int)i;
	return -1;
}

/* A valid SMR that matches the stream sid, or -1. */
static int
smr_for(unsigned sid)
{
	uint32_t smr;
	unsigned i;

	for (i = 0; i < g.nsmr && i < 128; i++) {
		smr = GR0(SMMU_SMR(i));
		if ((smr & SMR_VALID) &&
		    ((SMR_ID(smr) ^ sid) & ~SMR_MASK(smr) & 0x7fff) == 0)
			return (int)i;
	}
	return -1;
}

/*===========================================================================*
 *    Q13: may this side write the stream-matching table?                   *
 *===========================================================================*/
int
smmu_smr_test(void)
{
	const char *why;
	uint32_t r_id, r_mask, r_s2, test_s2, b_smr, b_s2;
	int i, ok;

	if (!cc_smmu_clocked(&why)) {
		log_warn(&ad_log, "Q13 smmu not written: %s is off; refusing\n",
		    why);
		return EIO;
	}
	if (geom() != OK)
		return EINVAL;
	if (!(g.id0 & ID0_SMS)) {
		log_warn(&ad_log, "Q13 the SMMU indexes streams, no SMRs to "
		    "test; refusing\n");
		return ENOTSUP;
	}
	if ((i = free_smr()) < 0) {
		log_warn(&ad_log, "Q13 all %u SMRs are valid, none free to "
		    "test; refusing\n", g.nsmr);
		return EBUSY;
	}
	u.smr = i;
	u.smr_v = GR0(SMMU_SMR(i));
	u.s2cr_v = GR0(SMMU_S2CR(i));
	u.sme_touched = 1;

	/* Never VALID: whatever is written, it routes nothing. */
	ad_wr(&ad.smmu, SMMU_SMR(i), 0x7fffu);
	r_id = GR0(SMMU_SMR(i));
	ad_wr(&ad.smmu, SMMU_SMR(i), 0x7fffu << 16);
	r_mask = GR0(SMMU_SMR(i));
	/*
	 * Its S2CR: "fault, bank 0" - the one value that cannot route
	 * anything anywhere, whatever the SMR.  Whether it sticks is only
	 * reported.  On the tablet it does not, and Linux says why
	 * (arm-smmu-qcom.c, qcom_smmu_cfg_probe()): with some firmware the
	 * hypervisor traps the stream-matching registers and ignores a
	 * fault-type S2CR.  Never "translate" here: an S2CR that names a bank
	 * routes into whatever that bank is, and an unset bank 0 is a
	 * stage-2 one (CBAR 0) - the hypervisor's kind.  On the tablet
	 * (2026-10-17) writing "translate, bank 0" before the bank was set
	 * up stopped the whole machine (hw-gpu.md, §15); Linux sets the bank
	 * up first, and so does smmu_cb0().
	 */
	test_s2 = (uint32_t)S2CR_TYPE_FAULT << 16;
	ad_wr(&ad.smmu, SMMU_S2CR(i), test_s2);
	r_s2 = GR0(SMMU_S2CR(i));

	ad_wr(&ad.smmu, SMMU_SMR(i), u.smr_v);
	ad_wr(&ad.smmu, SMMU_S2CR(i), u.s2cr_v);
	b_smr = GR0(SMMU_SMR(i));
	b_s2 = GR0(SMMU_S2CR(i));

	ok = !(r_id & SMR_VALID) && SMR_ID(r_id) != 0 && SMR_MASK(r_id) == 0 &&
	    !(r_mask & SMR_VALID) && SMR_MASK(r_mask) != 0 &&
	    SMR_ID(r_mask) == 0 && b_smr == u.smr_v && b_s2 == u.s2cr_v;
	log_info(&ad_log, "Q13 smr %d (%08x %08x): id bits %x mask bits %x "
	    "(numsidb %u)\n", i, u.smr_v, u.s2cr_v,
	    SMR_ID(r_id), SMR_MASK(r_mask), g.sidbits);
	log_info(&ad_log, "Q13 s2cr wrote %08x read %08x (%s); back %08x "
	    "%08x\n", test_s2, r_s2 & S2CR_FIELDS,
	    (r_s2 & S2CR_FIELDS) == test_s2 ? "taken" :
	    "ignored: a hypervisor's", b_smr, b_s2);
	if (ok)
		log_info(&ad_log, "Q13 the SMRs take writes\n");
	else
		log_warn(&ad_log, "Q13 the SMRs do NOT keep what is written; "
		    "stopping\n");
	return ok ? OK : EIO;
}

/*===========================================================================*
 *    Q14..Q16: bank 0                                                       *
 *===========================================================================*/
/* Invalidate the bank's ASID and wait for the SMMU to say it is done. */
static int
tlbi(void)
{
	unsigned t;

	CB_WR(SMMU_CB_TLBIASID, GPU_ASID);
	CB_WR(SMMU_CB_TLBSYNC, 0);
	for (t = 0; t <= 10000; t++) {
		if (!(CB(SMMU_CB_TLBSTATUS) & TLBSTATUS_SACTIVE))
			return OK;
		micro_delay(1);
	}
	log_warn(&ad_log, "Q14 smmu: TLB sync never finished\n");
	return ETIMEDOUT;
}

/* One translation operation: the PAR it leaves, or a timeout. */
static int
ats(uint32_t op, uint64_t va, uint64_t *par)
{
	unsigned t;

	ad_wr64(&ad.smmu, g.cb0 + op, va & ~0xfffull);
	for (t = 0; t <= 1000; t++) {
		if (!(CB(SMMU_CB_ATSR) & ATSR_ACTIVE)) {
			*par = ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_PAR);
			return OK;
		}
		micro_delay(1);
	}
	return ETIMEDOUT;
}

/* A translation and what it should give: a page and its attributes, or a
 * fault.  Returns 1 when as expected. */
static int
expect(uint32_t op, const char *name, uint64_t va, int which,
	const char *fault_why)
{
	uint64_t par;
	int r, ok;

	if ((r = ats(op, va, &par)) != OK) {
		log_warn(&ad_log, "Q14 ats %s %llx: no answer (atsr %08x)\n",
		    name, (unsigned long long)va, CB(SMMU_CB_ATSR));
		return 0;
	}
	if (which >= 0) {
		/*
		 * The page decides, not PAR.ATTR: for MAIR 0x44 the tablet's
		 * SMMU reports 0x40 - the outer half; the inner it leaves out
		 * (2026-10-17, hw-gpu.md, §15).  Printed, not judged.
		 */
		ok = !(par & PAR_F) && PAR_PA(par) == u.page_pa[which];
		log_info(&ad_log, "Q14 ats %s %llx: pa %llx attr %02x - %s\n",
		    name, (unsigned long long)va,
		    (unsigned long long)PAR_PA(par), PAR_ATTR(par),
		    ok ? "as mapped" : "NOT as mapped");
	} else {
		ok = (par & PAR_F) != 0;
		log_info(&ad_log, "Q14 ats %s %llx: par %016llx - %s%s\n", name,
		    (unsigned long long)va, (unsigned long long)par,
		    ok ? "fault, " : "NO fault, wanted one: ", fault_why);
	}
	return ok;
}

/* Wait up to ticks for this driver's interrupt: 1 if it came, 0 if not,
 * -1 if waiting failed.  The wait is inside init, so nothing but the
 * kernel's notifications is expected; the alarm ends it as tickdelay()'s
 * does. */
static int
wait_irq(clock_t ticks)
{
	message m;
	clock_t up;
	int st;

	if (sys_setalarm2(ticks, 0, NULL, &up) != OK)
		return -1;
	for (;;) {
		if (ipc_receive(ANY, &m, &st) != OK)
			return -1;
		if (!is_ipc_notify(st))
			continue;
		if (_ENDPOINT_P(m.m_source) == HARDWARE &&
		    (m.m_notify.interrupts & (1ull << 0))) {
			(void)sys_setalarm(0, 0);
			return 1;
		}
		if (_ENDPOINT_P(m.m_source) == CLOCK &&
		    m.m_notify.timestamp >= up + ticks)
			return 0;
	}
}

/* Q16: bank 0's line, by a fault raised in the bank. */
static int
fault_line(void)
{
	clock_t quiet = (clock_t)sys_hz() / 20, wait = (clock_t)sys_hz();
	uint32_t fsr, fsynr0, fsr2;
	uint64_t far;
	int irq, r, before, got, after;

	if (ad.dt.smmu_nirq <= ad.dt.smmu_nglobal) {
		log_warn(&ad_log, "Q16 the tree names no context lines for the "
		    "SMMU; refusing\n");
		return EINVAL;
	}
	irq = ad.dt.smmu_irq[ad.dt.smmu_nglobal + 0];
	u.hook = 0;
	if ((r = sys_irqsetpolicy(irq, 0, &u.hook)) != OK) {
		log_warn(&ad_log, "Q16 line %d: irqsetpolicy %d; refusing\n",
		    irq, r);
		return r;
	}
	u.hooked = 1;
	if ((r = sys_irqenable(&u.hook)) != OK) {
		log_warn(&ad_log, "Q16 line %d: irqenable %d\n", irq, r);
		return r;
	}
	before = wait_irq(quiet);

	CB_WR(SMMU_CB_FSRRESTORE, FSR_TF);
	got = wait_irq(wait);
	fsr = CB(SMMU_CB_FSR);
	far = ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_FAR);
	fsynr0 = CB(SMMU_CB_FSYNR0);
	CB_WR(SMMU_CB_FSR, fsr);		/* write-one-to-clear */
	fsr2 = CB(SMMU_CB_FSR);
	(void)sys_irqenable(&u.hook);
	after = wait_irq(quiet);
	(void)sys_irqrmpolicy(&u.hook);
	u.hooked = 0;

	log_info(&ad_log, "Q16 line %d %s before; raised tf: %s, fsr %08x "
	    "far %llx fsynr0 %x\n", irq, before == 0 ? "quiet" : "NOT quiet",
	    got == 1 ? "it came" : "NOTHING came", fsr,
	    (unsigned long long)far, fsynr0);
	if (before == 0 && got == 1 && (fsr & FSR_TF) &&
	    (fsr2 & FSR_FAULT) == 0 &&
	    after == 0) {
		log_info(&ad_log, "Q16 cleared (fsr %08x), quiet after: the "
		    "line works\n", fsr2);
		return OK;
	}
	log_warn(&ad_log, "Q16 cleared (fsr %08x), %s after: the line does "
	    "NOT work as it should\n", fsr2, after == 0 ? "quiet" :
	    "NOT quiet");
	return EIO;
}

/* Q15: the GPU's stream routed to bank 0 for 10 ms, and back. */
static int
route(void)
{
	uint32_t smr, s2cr, fsr, gfsr;
	int i, ok;

	if (u.smr < 0) {
		if ((i = free_smr()) < 0) {
			log_warn(&ad_log, "Q15 no free SMR; refusing\n");
			return EBUSY;
		}
		u.smr = i;
		u.smr_v = GR0(SMMU_SMR(i));
		u.s2cr_v = GR0(SMMU_S2CR(i));
	}
	i = u.smr;
	u.sme_touched = 1;
	/*
	 * Linux's order (arm_smmu_write_sme()): where to, then what.  On the
	 * tablet one of these two writes stopped the whole machine
	 * (hw-gpu.md, §15); a line before each, and time for the screen's
	 * console (10 Hz) to draw it, so that a photo can tell which.
	 */
	log_info(&ad_log, "Q15 writing s2cr %d = 00000000 (translate, cb0)\n",
	    i);
	micro_delay(300000);
	ad_wr(&ad.smmu, SMMU_S2CR(i), ((uint32_t)S2CR_TYPE_TRANS << 16) | 0);
	log_info(&ad_log, "Q15 s2cr %d reads %08x; writing smr %d = %08x\n",
	    i, GR0(SMMU_S2CR(i)), i, SMR_VALID | (ad.dt.sid & 0x7fff));
	micro_delay(300000);
	ad_wr(&ad.smmu, SMMU_SMR(i), SMR_VALID | (ad.dt.sid & 0x7fff));
	smr = GR0(SMMU_SMR(i));
	s2cr = GR0(SMMU_S2CR(i));
	micro_delay(10000);
	fsr = CB(SMMU_CB_FSR);
	gfsr = GR0(SMMU_SGFSR);
	/* Back: what first, then where to. */
	ad_wr(&ad.smmu, SMMU_SMR(i), u.smr_v);
	ad_wr(&ad.smmu, SMMU_S2CR(i), u.s2cr_v);

	ok = smr == (SMR_VALID | (ad.dt.sid & 0x7fff)) &&
	    (s2cr & S2CR_FIELDS) == 0 && smr_for(ad.dt.sid) < 0;
	log_info(&ad_log, "Q15 sid %u by smr %d to cb0: smr %08x s2cr %08x; "
	    "10 ms: fsr %x gfsr %x; %s\n", ad.dt.sid, i, smr, s2cr, fsr, gfsr,
	    ok ? "taken back" : "NOT as written");
	return ok ? OK : EIO;
}

int
smmu_cb0(int fault)
{
	const char *why;
	uint32_t sctlr, tcr, tcr2;
	uint64_t ttbr0;
	unsigned i, good = 0, n = 0;
	int r, s;

	if (!cc_smmu_clocked(&why)) {
		log_warn(&ad_log, "Q14 smmu not written: %s is off; refusing\n",
		    why);
		return EIO;
	}
	if (geom() != OK)
		return EINVAL;
	if (!(g.id0 & ID0_SMS) || !(g.id2 & ID2_PTFS_4K) || g.ncb < 1 ||
	    (g.id0 & ID0_ATOSNS)) {
		log_warn(&ad_log, "Q14 smmu id %08x %08x %08x: no SMRs, 4K "
		    "tables, banks or translation operations; refusing\n",
		    g.id0, g.id1, g.id2);
		return ENOTSUP;
	}
	/* Bank 0 must be nobody's: off, no fault in it, no stream to it. */
	sctlr = CB(SMMU_CB_SCTLR);
	if ((sctlr & SCTLR_M) || (CB(SMMU_CB_FSR) & FSR_FAULT)) {
		log_warn(&ad_log, "Q14 cb0 is in use (sctlr %08x fsr %08x); "
		    "refusing\n", sctlr, CB(SMMU_CB_FSR));
		return EBUSY;
	}
	for (i = 0; i < g.nsmr && i < 128; i++) {
		uint32_t smr = GR0(SMMU_SMR(i)), s2 = GR0(SMMU_S2CR(i));

		if ((smr & SMR_VALID) && S2CR_TYPE(s2) == S2CR_TYPE_TRANS &&
		    S2CR_CBNDX(s2) == 0) {
			log_warn(&ad_log, "Q14 smr %u (%08x) routes to cb0; "
			    "refusing\n", i, smr);
			return EBUSY;
		}
	}
	if ((s = smr_for(ad.dt.sid)) >= 0) {
		log_warn(&ad_log, "Q14 sid %u is routed already (smr %d); "
		    "refusing\n", ad.dt.sid, s);
		return EBUSY;
	}

	/* What is there, for smmu_undo(). */
	u.cba2r = ad_rd(&ad.smmu, g.gr1 + SMMU_CBA2R(0));
	u.cbar = ad_rd(&ad.smmu, g.gr1 + SMMU_CBAR(0));
	u.tcr2 = CB(SMMU_CB_TCR2);
	u.tcr = CB(SMMU_CB_TCR);
	u.ttbr0 = ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0);
	u.ttbr1 = ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_TTBR1);
	u.mair0 = CB(SMMU_CB_MAIR0);
	u.mair1 = CB(SMMU_CB_MAIR1);
	u.sctlr = sctlr;

	/* The tables: one page writable, one read-only, one never mapped. */
	if ((r = pt_init(&u.pt, g.ias, g.oas)) != OK) {
		log_warn(&ad_log, "Q14 no page table: %d\n", r);
		return r;
	}
	for (i = 0; i < 2; i++) {
		if ((u.page[i] = alloc_contig(4096, AC_ALIGN4K,
		    &u.page_pa[i])) == NULL) {
			log_warn(&ad_log, "Q14 no memory for a test page\n");
			return ENOMEM;
		}
		memset(u.page[i], 0, 4096);
	}
	if ((r = pt_map(&u.pt, VA_A & ((1ull << g.ias) - 1), u.page_pa[0],
	    AD_PT_READ | AD_PT_WRITE)) != OK ||
	    (r = pt_map(&u.pt, VA_B & ((1ull << g.ias) - 1), u.page_pa[1],
	    AD_PT_READ)) != OK) {
		log_warn(&ad_log, "Q14 mapping the test pages: %d\n", r);
		return r;
	}

	/* The bank, in Linux's order (arm_smmu_write_context_bank()). */
	u.cb_touched = 1;
	ad_wr(&ad.smmu, g.gr1 + SMMU_CBA2R(0), CBA2R_VA64);
	ad_wr(&ad.smmu, g.gr1 + SMMU_CBAR(0), CBAR_S1_BANK);
	tcr2 = g.pasize | TCR2_SEP_UPSTREAM | TCR2_AS;
	tcr = TCR_EPD1 | TCR_SH0_OS | TCR_T0SZ(64 - g.ias);
	ttbr0 = pt_root(&u.pt) | ((uint64_t)GPU_ASID << 48);
	CB_WR(SMMU_CB_TCR2, tcr2);
	CB_WR(SMMU_CB_TCR, tcr);
	ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0, ttbr0);
	ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR1, (uint64_t)GPU_ASID << 48);
	CB_WR(SMMU_CB_MAIR0, MAIR0_LINUX);
	CB_WR(SMMU_CB_MAIR1, 0);
	if ((r = tlbi()) != OK)
		return r;
	/* Linux's SCTLR for the GPU's bank, without stalling on a fault
	 * (arm-smmu-qcom.c adds HUPCF: later accesses go on). */
	sctlr = SCTLR_CFIE | SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M |
	    SCTLR_ASIDPNE | SCTLR_HUPCF;
	CB_WR(SMMU_CB_SCTLR, sctlr);

	log_info(&ad_log, "Q14 cb0: tables at %llx, ias %u oas %u, %d "
	    "levels; sctlr %08x tcr %08x tcr2 %08x\n",
	    (unsigned long long)pt_root(&u.pt), g.ias, g.oas, 4 - u.pt.start,
	    CB(SMMU_CB_SCTLR), CB(SMMU_CB_TCR), CB(SMMU_CB_TCR2));
	if (CB(SMMU_CB_SCTLR) != sctlr || CB(SMMU_CB_TCR) != tcr ||
	    ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0) != ttbr0) {
		log_warn(&ad_log, "Q14 cb0 does not keep what was written "
		    "(ttbr0 %llx); stopping\n", (unsigned long long)ad_rd64(
		    &ad.smmu, g.cb0 + SMMU_CB_TTBR0));
		return EIO;
	}

	/* What the tables say, asked of the SMMU. */
	n += 5;
	good += expect(SMMU_CB_ATS1PR, "pr", VA_A, 0, "");
	good += expect(SMMU_CB_ATS1UW, "uw", VA_A, 0, "");
	good += expect(SMMU_CB_ATS1UR, "ur", VA_B & ((1ull << g.ias) - 1), 1,
	    "");
	good += expect(SMMU_CB_ATS1UW, "uw", VA_B & ((1ull << g.ias) - 1),
	    -1, "read-only");
	good += expect(SMMU_CB_ATS1PR, "pr", VA_C, -1, "not mapped");
	/* An entry no mapping ever wrote: zero only because the table was
	 * cleaned whole when it was made. */
	if ((VA_D >> g.ias) == 0) {
		n++;
		good += expect(SMMU_CB_ATS1PR, "pr", VA_D, -1, "not mapped");
	}
	/* A mapping taken away: gone once the TLB has been told. */
	n++;
	if (pt_unmap(&u.pt, VA_A) == OK && tlbi() == OK)
		good += expect(SMMU_CB_ATS1PR, "pr", VA_A, -1, "unmapped");
	r = good == n ? OK : EIO;
	/* Translation operations report in PAR; whether a fault of one also
	 * shows in FSR is the implementation's.  Cleared either way. */
	if ((s = (int)CB(SMMU_CB_FSR)) != 0)
		CB_WR(SMMU_CB_FSR, (uint32_t)s);
	log_info(&ad_log, "Q14 cb0: %u of %u translations as the tables "
	    "say; fsr after them %x\n", good, n, (unsigned)s);
	if (r != OK)
		return r;

	/* route=0: the line alone - FSRRESTORE needs no stream routed. */
	if (ad_route && (r = route()) != OK)
		return r;
	if (!ad_route)
		log_info(&ad_log, "Q15 route=0: sid %u not routed\n", ad.dt.sid);
	return fault ? fault_line() : OK;
}

/*===========================================================================*
 *    Q17: everything back                                                   *
 *===========================================================================*/
void
smmu_undo(void)
{
	char line[200], part[48];
	const char *why;
	unsigned differs = 0;
	int keep_bank;
	uint32_t v;
	int i;

	if (u.hooked) {
		(void)sys_irqrmpolicy(&u.hook);
		u.hooked = 0;
	}
	if (!u.sme_touched && !u.cb_touched)
		goto out;
	if (!cc_smmu_clocked(&why)) {
		log_warn(&ad_log, "Q17 smmu not put back: %s is off\n", why);
		goto out;
	}
	strlcpy(line, "Q17 smmu put back:", sizeof(line));
	keep_bank = 0;
	if (u.sme_touched && (i = u.smr) >= 0) {
		ad_wr(&ad.smmu, SMMU_SMR(i), u.smr_v);
		ad_wr(&ad.smmu, SMMU_S2CR(i), u.s2cr_v);
		v = GR0(SMMU_S2CR(i));
		if (GR0(SMMU_SMR(i)) != u.smr_v) {
			snprintf(part, sizeof(part), " smr %d differs", i);
			strlcat(line, part, sizeof(line));
			differs++;
		} else if (v != u.s2cr_v && S2CR_TYPE(v) == S2CR_TYPE_TRANS &&
		    S2CR_CBNDX(v) == 0 && !(u.smr_v & SMR_VALID)) {
			/*
			 * A fault-type S2CR is ignored (smmu_smr_test()): this
			 * one still names bank 0, unused, its SMR not valid.
			 * Then bank 0 stays a stage-1 bank, off - never the
			 * stage-2 kind an S2CR must not name (hw-gpu.md, §15).
			 */
			snprintf(part, sizeof(part), " smr %d (s2cr left %08x)",
			    i, v);
			strlcat(line, part, sizeof(line));
			keep_bank = 1;
		} else if (v != u.s2cr_v) {
			snprintf(part, sizeof(part), " s2cr %d differs", i);
			strlcat(line, part, sizeof(line));
			differs++;
		} else {
			snprintf(part, sizeof(part), " smr %d", i);
			strlcat(line, part, sizeof(line));
		}
	}
	if (u.cb_touched && keep_bank) {
		CB_WR(SMMU_CB_SCTLR, u.sctlr & ~SCTLR_M);
		(void)tlbi();
		/* It names no table of this service, which is freed below. */
		ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0, u.ttbr0);
		ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR1, u.ttbr1);
		if ((v = CB(SMMU_CB_FSR)) != 0)
			CB_WR(SMMU_CB_FSR, v);
		strlcat(line, " cb0 off, kept stage 1", sizeof(line));
		if (CB(SMMU_CB_SCTLR) & SCTLR_M)
			differs++;
	} else if (u.cb_touched) {
		CB_WR(SMMU_CB_SCTLR, u.sctlr);
		(void)tlbi();
		ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0, u.ttbr0);
		ad_wr64(&ad.smmu, g.cb0 + SMMU_CB_TTBR1, u.ttbr1);
		CB_WR(SMMU_CB_MAIR0, u.mair0);
		CB_WR(SMMU_CB_MAIR1, u.mair1);
		CB_WR(SMMU_CB_TCR, u.tcr);
		CB_WR(SMMU_CB_TCR2, u.tcr2);
		ad_wr(&ad.smmu, g.gr1 + SMMU_CBAR(0), u.cbar);
		ad_wr(&ad.smmu, g.gr1 + SMMU_CBA2R(0), u.cba2r);
		if ((v = CB(SMMU_CB_FSR)) != 0)
			CB_WR(SMMU_CB_FSR, v);
		{
			/* Each register against what was found, by name. */
			static const struct { const char *n; int gr1; uint32_t
			    off; } r32[] = {
				{ "sctlr", 0, SMMU_CB_SCTLR },
				{ "tcr", 0, SMMU_CB_TCR },
				{ "tcr2", 0, SMMU_CB_TCR2 },
				{ "mair0", 0, SMMU_CB_MAIR0 },
				{ "mair1", 0, SMMU_CB_MAIR1 },
				{ "cbar", 1, SMMU_CBAR(0) },
				{ "cba2r", 1, SMMU_CBA2R(0) },
			};
			const uint32_t was[] = { u.sctlr, u.tcr, u.tcr2, u.mair0,
			    u.mair1, u.cbar, u.cba2r };
			unsigned k, before = differs;
			uint64_t q;

			for (k = 0; k < NELEM(r32); k++) {
				v = ad_rd(&ad.smmu, (r32[k].gr1 ? g.gr1 : g.cb0) +
				    r32[k].off);
				if (v != was[k]) {
					snprintf(part, sizeof(part),
					    " cb0 %s %08x (was %08x)", r32[k].n,
					    v, was[k]);
					strlcat(line, part, sizeof(line));
					differs++;
				}
			}
			if ((q = ad_rd64(&ad.smmu, g.cb0 + SMMU_CB_TTBR0)) !=
			    u.ttbr0 || ad_rd64(&ad.smmu, g.cb0 +
			    SMMU_CB_TTBR1) != u.ttbr1) {
				snprintf(part, sizeof(part), " cb0 ttbr0 %llx",
				    (unsigned long long)q);
				strlcat(line, part, sizeof(line));
				differs++;
			}
			if ((v = CB(SMMU_CB_FSR)) & FSR_FAULT) {
				snprintf(part, sizeof(part), " cb0 fsr %08x", v);
				strlcat(line, part, sizeof(line));
				differs++;
			}
			if (differs == before)
				strlcat(line, " cb0", sizeof(line));
		}
	}
	strlcat(line, differs == 0 ? "; all as found" : "; NOT all as found",
	    sizeof(line));
	if (differs == 0)
		log_info(&ad_log, "%s\n", line);
	else
		log_warn(&ad_log, "%s\n", line);
out:
	/* Only now: the SMMU no longer points at them. */
	pt_free(&u.pt);
	for (i = 0; i < 2; i++)
		if (u.page[i] != NULL) {
			free_contig(u.page[i], 4096);
			u.page[i] = NULL;
		}
	u.sme_touched = u.cb_touched = 0;
	u.smr = -1;
	g.ready = 0;
}
