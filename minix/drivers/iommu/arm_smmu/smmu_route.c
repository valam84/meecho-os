/*
 * A device's streams through its own bank of an ARM SMMU v2 (smmu_route.h
 * says why each step is the way it is).  Offsets, bits and order are
 * Linux's arm-smmu.c and io-pgtable-arm.c (GPL; the facts, not the code),
 * as the Adreno's service writes them on its own SMMU (adreno_smmu.c).
 */

#include "smmu_route.h"

#define SMMU_IDR0		0x020
#define SMMU_IDR1		0x024
#define SMMU_IDR2		0x028
#define SMMU_SGFAR		0x040		/* 64 bits */
#define SMMU_SGFSR		0x048
#define SMMU_SGFSYNR0		0x050
#define SMMU_SGFSYNR1		0x054
#define SMMU_SMR(n)		(0x800 + 4 * (n))
#define SMMU_S2CR(n)		(0xc00 + 4 * (n))
#define SMMU_CBAR(n)		(4 * (n))		/* page 1 */
#define SMMU_CBA2R(n)		(0x800 + 4 * (n))	/* page 1 */
#define CB_SCTLR		0x000
#define CB_TCR2			0x010
#define CB_TTBR0		0x020
#define CB_TTBR1		0x028
#define CB_TCR			0x030
#define CB_MAIR0		0x038
#define CB_MAIR1		0x03c
#define CB_FSR			0x058
#define CB_FAR			0x060
#define CB_FSYNR0		0x068
#define CB_TLBIASID		0x610
#define CB_TLBSYNC		0x7f0
#define CB_TLBSTATUS		0x7f4

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

static inline uint32_t
rd(struct smmu_route *r, uint32_t off)
{
	uint32_t v;

	__asm__ volatile("ldr %w0, [%1]" : "=r" (v) : "r" (r->base + off) :
	    "memory");
	return v;
}

static inline uint64_t
rd64(struct smmu_route *r, uint32_t off)
{
	uint64_t v;

	__asm__ volatile("ldr %0, [%1]" : "=r" (v) : "r" (r->base + off) :
	    "memory");
	return v;
}

static inline void
wr(struct smmu_route *r, uint32_t off, uint32_t v)
{
	__asm__ volatile("str %w0, [%1]" : : "r" (v), "r" (r->base + off) :
	    "memory");
}

static inline void
wr64(struct smmu_route *r, uint32_t off, uint64_t v)
{
	__asm__ volatile("str %0, [%1]" : : "r" (v), "r" (r->base + off) :
	    "memory");
}

static unsigned
size_bits(unsigned code)
{
	static const unsigned bits[] = { 32, 36, 40, 42, 44, 48 };

	return code < sizeof(bits) / sizeof(bits[0]) ? bits[code] : 48;
}

int
smmu_route_init(struct smmu_route *r)
{
	uint32_t idr0, idr1, idr2;
	unsigned numpage;
	int e;

	idr0 = rd(r, SMMU_IDR0);
	idr1 = rd(r, SMMU_IDR1);
	idr2 = rd(r, SMMU_IDR2);
	r->pgshift = (idr1 & (1u << 31)) ? 16 : 12;
	numpage = 1u << (((idr1 >> 28) & 7) + 1);
	r->nsmr = idr0 & 0xff;
	r->ncb = idr1 & 0xff;
	r->gr1 = 1u << r->pgshift;
	r->cbo = (numpage << r->pgshift) + (r->cb << r->pgshift);
	/* Stage 1, stage 2 bypassed: out, the SMMU's intermediate width. */
	r->pasize = idr2 & 0xf;
	r->oas = size_bits(r->pasize);
	if (r->smr >= r->nsmr || r->cb >= r->ncb ||
	    r->cbo + (1u << r->pgshift) > r->size || r->ias < 32 ||
	    r->ias > 48) {
		log_warn(r->log, "smmu: no SMR %u or bank %u here (%u, %u), or "
		    "ias %u; refusing\n", r->smr, r->cb, r->nsmr, r->ncb,
		    r->ias);
		return EINVAL;
	}
	if ((e = pt_init(&r->pt, r->ias, r->oas)) != OK) {
		log_warn(r->log, "smmu: no page table: %d\n", e);
		return e;
	}
	return OK;
}

int
smmu_route_map(struct smmu_route *r, uint64_t iova, phys_bytes pa,
	size_t len, int writable)
{
	size_t o;
	int e;

	for (o = 0; o < len; o += 4096)
		if ((e = pt_map(&r->pt, iova + o, (uint64_t)pa + o,
		    AD_PT_READ | (writable ? AD_PT_WRITE : 0))) != OK)
			return e;
	return OK;
}

int
smmu_route_unmap(struct smmu_route *r, uint64_t iova, size_t len)
{
	size_t o;

	for (o = 0; o < len; o += 4096)
		(void)pt_unmap(&r->pt, iova + o);
	return r->routed ? smmu_route_tlbi(r) : OK;
}

/* Invalidate the bank's ASID; without check, trust the sync to finish
 * (TLBSTATUS is a bank register) and give it a moment. */
int
smmu_route_tlbi(struct smmu_route *r)
{
	unsigned t;

	wr(r, r->cbo + CB_TLBIASID, r->asid);
	wr(r, r->cbo + CB_TLBSYNC, 0);
	if (!r->check) {
		micro_delay(100);
		return OK;
	}
	for (t = 0; t <= 10000; t++) {
		if (!(rd(r, r->cbo + CB_TLBSTATUS) & TLBSTATUS_SACTIVE))
			return OK;
		micro_delay(1);
	}
	log_warn(r->log, "smmu: TLB sync never finished\n");
	return ETIMEDOUT;
}

/*
 * The bank over the tables, then the streams to it: one SMR for all of
 * them, with the ID bits they share and a mask over those that differ.
 * A line before each kind of write, so that a stop says where.
 */
int
smmu_route_on(struct smmu_route *r, const uint32_t *sids, unsigned n)
{
	uint64_t ttbr0;
	uint32_t sctlr, mask = 0, s2cr;
	unsigned i;
	int e;

	if (n == 0)
		return EINVAL;
	for (i = 1; i < n; i++)
		mask |= (sids[i] ^ sids[0]) & 0x7fff;
	r->smr_val = SMR_VALID | (mask << 16) | (sids[0] & ~mask & 0x7fff);

	log_info(r->log, "smmu: writing bank %u (tables at %llx, ias %u oas "
	    "%u, asid %u)\n", r->cb, (unsigned long long)pt_root(&r->pt),
	    r->ias, r->oas, r->asid);
	wr(r, r->gr1 + SMMU_CBA2R(r->cb), CBA2R_VA64);
	wr(r, r->gr1 + SMMU_CBAR(r->cb), CBAR_S1_BANK);
	wr(r, r->cbo + CB_TCR2, (r->pasize & 7) | TCR2_SEP_UPSTREAM | TCR2_AS);
	wr(r, r->cbo + CB_TCR, TCR_EPD1 | TCR_SH0_OS | TCR_T0SZ(64 - r->ias));
	ttbr0 = pt_root(&r->pt) | ((uint64_t)r->asid << 48);
	wr64(r, r->cbo + CB_TTBR0, ttbr0);
	wr64(r, r->cbo + CB_TTBR1, (uint64_t)r->asid << 48);
	wr(r, r->cbo + CB_MAIR0, MAIR0_LINUX);
	wr(r, r->cbo + CB_MAIR1, 0);	/* "r": a real register, not wzr */
	if ((e = smmu_route_tlbi(r)) != OK)
		return e;
	sctlr = SCTLR_CFIE | SCTLR_CFRE | SCTLR_AFE | SCTLR_TRE | SCTLR_M |
	    SCTLR_ASIDPNE | SCTLR_HUPCF;
	wr(r, r->cbo + CB_SCTLR, sctlr);
	if (r->check && (rd(r, r->cbo + CB_SCTLR) != sctlr ||
	    rd64(r, r->cbo + CB_TTBR0) != ttbr0)) {
		log_warn(r->log, "smmu: bank %u does not keep what was "
		    "written\n", r->cb);
		return EIO;
	}

	log_info(r->log, "smmu: bank %u written; writing s2cr %u = %08x\n",
	    r->cb, r->smr, S2CR_ROUTE(r->cb));
	wr(r, SMMU_S2CR(r->smr), S2CR_ROUTE(r->cb));
	s2cr = rd(r, SMMU_S2CR(r->smr));
	log_info(r->log, "smmu: s2cr %u reads %08x; writing smr %u = %08x\n",
	    r->smr, s2cr, r->smr, r->smr_val);
	wr(r, SMMU_SMR(r->smr), r->smr_val);
	/*
	 * Both must read back as written.  On the apps SMMU SMR 0 read 0
	 * after its write while SMR 1 kept it: a stream whose SMR was not
	 * taken stays unmatched, faults, and the IPA's first DMA after that
	 * reset the SoC (hw-modem.md, 9.4) - so a route that does not read
	 * back is refused, not tried.
	 */
	r->routed = (s2cr & 0x300ff) == r->cb &&
	    rd(r, SMMU_SMR(r->smr)) == r->smr_val;
	log_info(r->log, "smmu: smr %u reads %08x; streams %x..%x to bank %u%s"
	    "\n", r->smr, rd(r, SMMU_SMR(r->smr)), sids[0], sids[n - 1], r->cb,
	    r->routed ? "" : " - NOT as written; refusing");
	return r->routed ? OK : EIO;
}

/* The streams no longer matched (the S2CR cannot go back to "fault"). */
void
smmu_route_off(struct smmu_route *r)
{
	uint32_t v = r->smr_val & ~SMR_VALID;

	wr(r, SMMU_SMR(r->smr), v);
	r->routed = 0;
}

/*
 * Faults: the global ones always (page 0 reads are safe: an unmatched
 * stream lands here, with its ID in sGFSYNR1), the bank's only if its
 * registers may be read.
 */
void
smmu_route_report(struct smmu_route *r)
{
	uint32_t fsr;

	if ((fsr = rd(r, SMMU_SGFSR)) != 0) {
		log_warn(r->log, "smmu: global fault: sgfsr %08x sgfar %llx "
		    "sgfsynr0 %08x sgfsynr1 %08x (stream %x)\n", fsr,
		    (unsigned long long)rd64(r, SMMU_SGFAR), rd(r, SMMU_SGFSYNR0),
		    rd(r, SMMU_SGFSYNR1), rd(r, SMMU_SGFSYNR1) & 0xffff);
		wr(r, SMMU_SGFSR, fsr);
	} else
		log_info(r->log, "smmu: no global fault\n");
	if (!r->check || !r->routed)
		return;
	if ((fsr = rd(r, r->cbo + CB_FSR)) & 0x1ff) {
		log_warn(r->log, "smmu: bank %u fault: fsr %08x far %llx fsynr0 "
		    "%08x\n", r->cb, fsr, (unsigned long long)rd64(r,
		    r->cbo + CB_FAR), rd(r, r->cbo + CB_FSYNR0));
		wr(r, r->cbo + CB_FSR, fsr);
	}
}
