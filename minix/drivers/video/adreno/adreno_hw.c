/*
 * The GPU's and the SMMU's own registers (port/clover/hw-gpu.md, §4 and
 * §6), read only once cc_gpu_clocked() / cc_smmu_clocked() say so.  One
 * write in all: CP_SCRATCH_REG0, put back as it was - the proof that a
 * write reaches the GPU before anything depends on one.
 */

#include "adreno.h"
#include "adreno_regs.h"

#define GPU(r)		ad_rd(&ad.gpu, A5XX(r))

/* The always-on counter: high, low, high again until the high half holds. */
static uint64_t
alwayson(void)
{
	uint32_t hi, lo, hi2;
	int n;

	for (n = 0; n < 4; n++) {
		hi = GPU(A5XX_RBBM_ALWAYSON_COUNTER_HI);
		lo = GPU(A5XX_RBBM_ALWAYSON_COUNTER_LO);
		hi2 = GPU(A5XX_RBBM_ALWAYSON_COUNTER_HI);
		if (hi == hi2)
			break;
	}
	return (uint64_t)hi2 << 32 | lo;
}

void
hw_gpu_report(void)
{
	const char *why;
	uint64_t t0, t1;
	uint32_t old, pat = 0x5ad0c0deu, got, back;

	if (!cc_gpu_clocked(&why)) {
		log_warn(&ad_log, "Q5 gpu not read: %s is off; refusing\n", why);
		return;
	}
	log_info(&ad_log, "Q5 gpu hw %08x %08x %08x vbif %08x\n",
	    GPU(A5XX_RBBM_HW0), GPU(A5XX_RBBM_HW0 + 1),
	    GPU(A5XX_RBBM_HW0 + 2), GPU(A5XX_VBIF_VERSION));
	log_info(&ad_log, "Q6 gpu status %08x int %08x secvid %08x "
	    "me_cntl %08x\n", GPU(A5XX_RBBM_STATUS),
	    GPU(A5XX_RBBM_INT_0_STATUS), GPU(A5XX_RBBM_SECVID_TRUST_CNTL),
	    GPU(A5XX_CP_PFP_ME_CNTL));

	t0 = alwayson();
	micro_delay(10000);
	t1 = alwayson();
	log_info(&ad_log, "Q7 gpu timer %llu ticks in 10 ms (192000 at "
	    "19.2 MHz); at %llu\n", (unsigned long long)(t1 - t0),
	    (unsigned long long)t1);

	old = GPU(A5XX_CP_SCRATCH_REG(0));
	ad_wr(&ad.gpu, A5XX(A5XX_CP_SCRATCH_REG(0)), pat);
	got = GPU(A5XX_CP_SCRATCH_REG(0));
	ad_wr(&ad.gpu, A5XX(A5XX_CP_SCRATCH_REG(0)), old);
	back = GPU(A5XX_CP_SCRATCH_REG(0));
	log_info(&ad_log, "Q8 gpu scratch0 %08x, wrote %08x read %08x, "
	    "restored %08x%s\n", old, pat, got, back,
	    got == pat && back == old ? ": writes reach the GPU" : "");
}

/* SMMU ID size codes, as arm-smmu.c reads them. */
static unsigned
size_bits(unsigned code)
{
	static const unsigned bits[] = { 32, 36, 40, 42, 44, 48 };

	return code < sizeof(bits) / sizeof(bits[0]) ? bits[code] : 48;
}

void
hw_smmu_report(void)
{
	const char *why;
	uint32_t id0, id1, id2, smr, s2cr, v;
	unsigned pgshift, numpage, nsmr, i, match = ~0u;
	size_t cb0;

	if (!cc_smmu_clocked(&why)) {
		log_warn(&ad_log, "Q9 smmu not read: %s is off; refusing\n",
		    why);
		return;
	}
	id0 = ad_rd(&ad.smmu, SMMU_ID0);
	id1 = ad_rd(&ad.smmu, SMMU_ID1);
	id2 = ad_rd(&ad.smmu, SMMU_ID2);
	pgshift = (id1 & ID1_PAGESIZE) ? 16 : 12;
	numpage = 1u << (ID1_NUMPAGENDXB(id1) + 1);
	nsmr = ID0_NUMSMRG(id0);
	log_info(&ad_log, "Q9 smmu scr0 %08x id %08x %08x %08x gfsr %08x: "
	    "%u cb, %u smr%s, %uK pages, ias %u oas %u ubs %u\n",
	    ad_rd(&ad.smmu, SMMU_SCR0), id0, id1, id2,
	    ad_rd(&ad.smmu, SMMU_SGFSR), ID1_NUMCB(id1), nsmr,
	    (id0 & ID0_SMS) ? "" : " (stream index)", 1u << (pgshift - 10),
	    size_bits(ID2_IAS(id2)), size_bits(ID2_OAS(id2)),
	    size_bits(ID2_UBS(id2)));

	/* Which entry routes the GPU's stream, and where. */
	if (id0 & ID0_SMS) {
		for (i = 0; i < nsmr && i < 128; i++) {
			smr = ad_rd(&ad.smmu, SMMU_SMR(i));
			if ((smr & SMR_VALID) && ((SMR_ID(smr) ^ ad.dt.sid) &
			    ~SMR_MASK(smr) & 0x7fff) == 0) {
				match = i;
				break;
			}
		}
	} else
		match = ad.dt.sid;
	if (match == ~0u)
		log_info(&ad_log, "Q10 sid %u: no valid SMR routes it\n",
		    ad.dt.sid);
	else {
		s2cr = ad_rd(&ad.smmu, SMMU_S2CR(match));
		v = ad_rd(&ad.smmu, (1u << pgshift) + SMMU_CBAR(
		    S2CR_CBNDX(s2cr)));
		log_info(&ad_log, "Q10 sid %u: smr %u s2cr %08x (%s cb %u) "
		    "cbar %08x\n", ad.dt.sid, match, s2cr,
		    S2CR_TYPE(s2cr) == 0 ? "translate" :
		    S2CR_TYPE(s2cr) == 1 ? "bypass" : "fault",
		    S2CR_CBNDX(s2cr), v);
	}

	/* Context bank 0, if it lies inside the window the tree gave. */
	cb0 = (size_t)numpage << pgshift;
	if (cb0 + 0x1000 > ad.smmu.size) {
		log_info(&ad_log, "Q10 cb0 at +0x%zx is outside the window "
		    "(0x%zx); not read\n", cb0, ad.smmu.size);
		return;
	}
	log_info(&ad_log, "Q10 cb0 +0x%zx sctlr %08x ttbr0 %08x%08x tcr %08x "
	    "fsr %08x\n", cb0, ad_rd(&ad.smmu, (uint32_t)cb0 + SMMU_CB_SCTLR),
	    ad_rd(&ad.smmu, (uint32_t)cb0 + SMMU_CB_TTBR0_HI),
	    ad_rd(&ad.smmu, (uint32_t)cb0 + SMMU_CB_TTBR0_LO),
	    ad_rd(&ad.smmu, (uint32_t)cb0 + SMMU_CB_TCR),
	    ad_rd(&ad.smmu, (uint32_t)cb0 + SMMU_CB_FSR));
}
