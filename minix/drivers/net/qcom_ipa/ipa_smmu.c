/*
 * The IPA's streams on the apps SMMU (iommu@16c0000), routed through a
 * context bank of the IPA's own over page tables of this service's
 * memory (adreno_pt.c), so that the addresses in the IPA's descriptors
 * are IOVAs below 4 GiB this service chose, and nothing else of the AP's
 * memory is reachable from the IPA or from its microcontroller (which the
 * modem drives) - port/clover/hw-modem.md, section 9.
 *
 * The SMMU is shared with the Wi-Fi (wcn): by agreement each service
 * writes only its own SMR, S2CR and bank - the IPA SMR 0, S2CR 0, bank 0,
 * the Wi-Fi SMR 1, S2CR 1, bank 1 - and nobody writes sCR0
 * (side-session notes, apps-smmu-snapshot.md).  The firmware leaves every
 * SMR free and unmatched streams faulting (sCR0.USFCFG).
 *
 * Order and values are Linux's (arm-smmu.c arm_smmu_write_context_bank(),
 * arm_smmu_write_sme(); GPL - the offsets, bits and order), as the
 * Adreno's service writes them on its own SMMU (adreno_smmu.c,
 * port/clover/hw-gpu.md, sections 15-16): the bank first, then where the
 * stream goes (S2CR), then what matches it (SMR).  Every access is one
 * ldr/str with the value in a real register: the hypervisor emulates the
 * stream-matching registers by the trapped instruction.
 *
 * A read of the apps SMMU's bank registers stopped a core in the probe's
 * first round, and Linux never reads them; so nothing here reads a bank
 * back unless smmu_check is set (read=banks found them safe).
 */

#include "ipa.h"
#include "adreno_pt.h"

#include <string.h>

#define SMMU_CBA2R(n)		(0x800 + 4 * (n))	/* in the second page */
#define SMMU_CB_TCR2		0x010
#define SMMU_CB_TTBR1		0x028
#define SMMU_CB_MAIR0		0x038
#define SMMU_CB_MAIR1		0x03c
#define SMMU_CB_FAR		0x060
#define SMMU_CB_FSYNR0		0x068
#define SMMU_CB_TLBIASID	0x610
#define SMMU_CB_TLBSYNC		0x7f0
#define SMMU_CB_TLBSTATUS	0x7f4

#define SMR_VALID		(1u << 31)
#define S2CR_ROUTE(cb)		(0x00000300u | (cb))	/* translate, NSH */
#define CBAR_S1_BANK		0x0001f300u	/* S1, S2 bypass (Linux's) */
#define CBA2R_VA64		(1u << 0)
#define SCTLR_M			(1u << 0)
#define SCTLR_TRE		(1u << 1)
#define SCTLR_AFE		(1u << 2)
#define SCTLR_CFRE		(1u << 5)
#define SCTLR_CFIE		(1u << 6)
#define SCTLR_HUPCF		(1u << 8)
#define SCTLR_ASIDPNE		(1u << 12)
#define TCR_T0SZ(n)		((uint32_t)(n) & 0x3f)
#define TCR_SH0_OS		(2u << 12)
#define TCR_EPD1		(1u << 23)
#define TCR2_AS			(1u << 4)
#define TCR2_SEP_UPSTREAM	(7u << 15)
#define TLBSTATUS_SACTIVE	(1u << 0)
#define MAIR0_LINUX		0xf404ff44u	/* 0: normal non-cacheable */

#define IPA_SMR		0
#define IPA_CB		0
#define IPA_ASID	1
#define IPA_IAS		32		/* the IPA's addresses are 32-bit */

int smmu_check;

static struct {
	unsigned pgshift, nsmr, ncb, oas, pasize;
	uint32_t gr1, cb;		/* second page; the IPA's bank */
	struct ad_pt pt;
	int routed;
} s;

#define CB_WR(r, v)	wr32(ipa.smmu, s.cb + (r), (v))
#define CB_RD(r)	rd32(ipa.smmu, s.cb + (r))

static void
wr64(volatile uint8_t *b, uint32_t off, uint64_t v)
{
	__asm__ volatile("str %0, [%1]" : : "r" (v), "r" (b + off) :
	    "memory");
}

static unsigned
size_bits(unsigned code)
{
	static const unsigned bits[] = { 32, 36, 40, 42, 44, 48 };

	return code < sizeof(bits) / sizeof(bits[0]) ? bits[code] : 48;
}

/*
 * The geometry from the identification registers (global page 0, which
 * the probe read without harm), and the tables.
 */
int
ipa_smmu_init(void)
{
	uint32_t idr0, idr1, idr2;
	unsigned numpage;
	int r;

	if (ipa.smmu == NULL)
		return ENXIO;
	idr0 = rd32(ipa.smmu, SMMU_IDR0);
	idr1 = rd32(ipa.smmu, SMMU_IDR1);
	idr2 = rd32(ipa.smmu, SMMU_IDR2);
	s.pgshift = (idr1 & (1u << 31)) ? 16 : 12;
	numpage = 1u << (((idr1 >> 28) & 7) + 1);
	s.nsmr = idr0 & 0xff;
	s.ncb = idr1 & 0xff;
	s.gr1 = 1u << s.pgshift;
	s.cb = (numpage << s.pgshift) + (IPA_CB << s.pgshift);
	/* Stage 1, stage 2 bypassed: out, the SMMU's intermediate width. */
	s.pasize = idr2 & 0xf;
	s.oas = size_bits(s.pasize);
	if (s.nsmr <= IPA_SMR || s.ncb <= IPA_CB ||
	    s.cb + (1u << s.pgshift) > ipa.smmu_sz) {
		log_warn(&ilog, "smmu: no SMR %u or bank %u here; refusing\n",
		    IPA_SMR, IPA_CB);
		return EINVAL;
	}
	if ((r = pt_init(&s.pt, IPA_IAS, s.oas)) != OK) {
		log_warn(&ilog, "smmu: no page table: %d\n", r);
		return r;
	}
	return OK;
}

int
ipa_smmu_map(uint32_t iova, phys_bytes pa, size_t len, int writable)
{
	size_t o;
	int r;

	for (o = 0; o < len; o += 4096)
		if ((r = pt_map(&s.pt, (uint64_t)iova + o, (uint64_t)pa + o,
		    AD_PT_READ | (writable ? AD_PT_WRITE : 0))) != OK)
			return r;
	return OK;
}

static int
tlbi(void)
{
	unsigned t;

	CB_WR(SMMU_CB_TLBIASID, IPA_ASID);
	CB_WR(SMMU_CB_TLBSYNC, 0);
	if (!smmu_check)
		return OK;	/* TLBSTATUS is a bank read */
	for (t = 0; t <= 10000; t++) {
		if (!(CB_RD(SMMU_CB_TLBSTATUS) & TLBSTATUS_SACTIVE))
			return OK;
		micro_delay(1);
	}
	log_warn(&ilog, "smmu: TLB sync never finished\n");
	return ETIMEDOUT;
}

/*
 * The bank over the tables, then the IPA's streams to it.  Linux's order;
 * a line before each kind of write, so that a stop says where.
 */
int
ipa_smmu_route(void)
{
	uint64_t ttbr0;
	uint32_t sctlr, smr;
	unsigned i;
	int r;

	if (ipa.nsid == 0)
		return ENXIO;
	/* One SMR for the IPA's streams: the ID bits they share, a mask
	 * over the ones in which they differ. */
	smr = 0;
	for (i = 1; i < ipa.nsid; i++)
		smr |= (ipa.sid[i] ^ ipa.sid[0]) & 0x7fff;
	smr = SMR_VALID | (smr << 16) | (ipa.sid[0] & ~smr & 0x7fff);

	log_info(&ilog, "smmu: writing bank %u (tables at %llx, ias %u oas "
	    "%u)\n", IPA_CB, (unsigned long long)pt_root(&s.pt), IPA_IAS,
	    s.oas);
	wr32(ipa.smmu, s.gr1 + SMMU_CBA2R(IPA_CB), CBA2R_VA64);
	wr32(ipa.smmu, s.gr1 + SMMU_CBAR(IPA_CB), CBAR_S1_BANK);
	CB_WR(SMMU_CB_TCR2, (s.pasize & 7) | TCR2_SEP_UPSTREAM | TCR2_AS);
	CB_WR(SMMU_CB_TCR, TCR_EPD1 | TCR_SH0_OS | TCR_T0SZ(64 - IPA_IAS));
	ttbr0 = pt_root(&s.pt) | ((uint64_t)IPA_ASID << 48);
	wr64(ipa.smmu, s.cb + SMMU_CB_TTBR0, ttbr0);
	wr64(ipa.smmu, s.cb + SMMU_CB_TTBR1, (uint64_t)IPA_ASID << 48);
	CB_WR(SMMU_CB_MAIR0, MAIR0_LINUX);
	CB_WR(SMMU_CB_MAIR1, 0);
	if ((r = tlbi()) != OK)
		return r;
	sctlr = SCTLR_CFIE | SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M |
	    SCTLR_ASIDPNE | SCTLR_HUPCF;
	CB_WR(SMMU_CB_SCTLR, sctlr);
	if (smmu_check && (CB_RD(SMMU_CB_SCTLR) != sctlr ||
	    rd64(ipa.smmu, s.cb + SMMU_CB_TTBR0) != ttbr0)) {
		log_warn(&ilog, "smmu: bank %u does not keep what was "
		    "written\n", IPA_CB);
		return EIO;
	}

	log_info(&ilog, "smmu: writing s2cr %u = %08x\n", IPA_SMR,
	    S2CR_ROUTE(IPA_CB));
	wr32(ipa.smmu, SMMU_S2CR(IPA_SMR), S2CR_ROUTE(IPA_CB));
	log_info(&ilog, "smmu: s2cr %u reads %08x; writing smr %u = %08x\n",
	    IPA_SMR, rd32(ipa.smmu, SMMU_S2CR(IPA_SMR)), IPA_SMR, smr);
	wr32(ipa.smmu, SMMU_SMR(IPA_SMR), smr);
	log_info(&ilog, "smmu: smr %u reads %08x; streams %x..%x to bank "
	    "%u\n", IPA_SMR, rd32(ipa.smmu, SMMU_SMR(IPA_SMR)), ipa.sid[0],
	    ipa.sid[ipa.nsid - 1], IPA_CB);
	s.routed = 1;
	return rd32(ipa.smmu, SMMU_SMR(IPA_SMR)) == smr ? OK : EIO;
}

/* A fault of the IPA's bank, if smmu_check says the bank can be read. */
void
ipa_smmu_report(void)
{
	uint32_t fsr;

	if (!smmu_check || !s.routed)
		return;
	if ((fsr = CB_RD(SMMU_CB_FSR)) & 0x1ff) {
		log_warn(&ilog, "smmu: bank %u fault: fsr %08x far %llx fsynr0 "
		    "%08x\n", IPA_CB, fsr, (unsigned long long)rd64(ipa.smmu,
		    s.cb + SMMU_CB_FAR), CB_RD(SMMU_CB_FSYNR0));
		CB_WR(SMMU_CB_FSR, fsr);
	}
}
