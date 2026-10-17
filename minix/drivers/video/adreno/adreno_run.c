/*
 * The stages (adreno.h), in order, each one only if the one before it
 * answered: what the tree says; the clock blocks as found (Q1-Q3); then,
 * from stage cx on, domains and clocks brought up (Q4), the GPU (Q5-Q8) and
 * the SMMU (Q9, Q10) read; from stage smr on, the SMMU written (Q13-Q16);
 * at stage zap, the zap shader to TrustZone while the GPU is powered (Q18);
 * at stage cp, the command processor started, the zap shader, secure mode
 * left, a first command (Q19-Q22) and the CP stopped (Q23); the SMMU put
 * back (Q17); everything undone (Q11); TrustZone (Q12) when asked.  The
 * questions are port/clover/hw-gpu.md, §12, §14 and §17.
 */

#include "adreno.h"
#include "qcom_soc.h"

#include <minix/vm.h>
#include <sys/mman.h>

struct adreno ad;

static int
map_region(struct ad_region *r, const char *name, u64_t pa, u64_t sz)
{
	void *v;

	r->name = name;
	r->pa = pa;
	r->size = (size_t)sz;
	v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)sz);
	if (v == MAP_FAILED) {
		log_warn(&ad_log, "cannot map %s at 0x%llx+0x%llx; refusing\n",
		    name, (unsigned long long)pa, (unsigned long long)sz);
		r->base = NULL;
		return EPERM;
	}
	r->base = v;
	return OK;
}

/*
 * Mapping a block touches none of it; reading does.  The GCC is qcom_soc's
 * too - mapped there for its looks, here for this driver's own writes -
 * and must be the one the GPU's clocks name.
 */
int
adreno_map(const void *dtb)
{
	int r;

	if ((r = qsoc_init_gcc(dtb)) != OK) {
		log_warn(&ad_log, "no GCC this driver knows (sdm660/sdm630): "
		    "%d; refusing\n", r);
		return r;
	}
	if (qsoc_gcc_phandle() != ad.dt.gcc_ph) {
		log_warn(&ad_log, "the GPU's clocks name another GCC than the "
		    "SoC's; refusing\n");
		return EINVAL;
	}
	if ((r = map_region(&ad.gcc, "gcc", ad.dt.gcc_pa, ad.dt.gcc_sz)) != OK ||
	    (r = map_region(&ad.gpucc, "gpucc", ad.dt.gpucc_pa,
	    ad.dt.gpucc_sz)) != OK ||
	    (r = map_region(&ad.gpu, "gpu", ad.dt.gpu_pa, ad.dt.gpu_sz)) != OK ||
	    (r = map_region(&ad.smmu, "smmu", ad.dt.smmu_pa,
	    ad.dt.smmu_sz)) != OK)
		return r;
	return OK;
}

int
adreno_run(const void *dtb, int stage, int tz)
{
	int r;

	/* TrustZone first: it does not depend on the GPU's node, and a
	 * machine without the node (QEMU) still answers it - ENODEV. */
	if (tz)
		tz_report();

	if ((r = adreno_find(dtb, &ad.dt)) != OK)
		return r;
	adreno_find_report(&ad.dt);
	if ((r = adreno_map(dtb)) != OK)
		return r;

	if ((r = cc_report()) != OK)
		return r;

	if (stage == AD_PROBE) {
		log_info(&ad_log, "stage probe: nothing written\n");
		return OK;
	}

	r = cc_power_up(stage);
	if (r == OK && stage >= AD_GPU)
		hw_gpu_report();
	if (r == OK && stage >= AD_SMMU)
		hw_smmu_report();
	if (r == OK && stage >= AD_SMR)
		r = smmu_smr_test();
	if (r == OK && stage >= AD_CB0)
		r = smmu_cb0(stage >= AD_FAULT);
	/* Stage cp has the zap shader at Linux's point: after CP_ME_INIT. */
	if (r == OK && stage == AD_ZAP)
		r = zap_load();
	if (r == OK && stage >= AD_CP)
		r = cp_run();
	cp_stop();		/* while the GPU is powered and routed */
	smmu_undo();		/* while the SMMU is still powered */
	cc_power_down();
	cp_free();		/* no table points at it any more */
	return r;
}
