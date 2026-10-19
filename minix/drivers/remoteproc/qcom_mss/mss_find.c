/*
 * What the device tree says about the modem (mss.h): the remoteproc node
 * ("qcom,sdm660-mss-pil" - the one sequence written here), SMEM and its
 * hardware lock, the smp2p edge whose remote is the modem (qcom,remote-pid
 * 1) and the mailbox that rings it.  Every address by phandle, as Linux
 * resolves them; nothing is assumed of the SoC that the tree can say.
 */

#include "mss.h"

#include <minix/fdt.h>

#include <string.h>

#define MODEM_PID	1

static uint32_t
prop_u32(const struct fdt_node *n, const char *name, unsigned i, uint32_t def)
{
	const uint8_t *p;
	unsigned len;

	if ((p = fdt_getprop(n, name, &len)) == NULL || len < 4 * (i + 1))
		return def;
	return (uint32_t)fdt_read_cells(p + 4 * i, 1);
}

/* The i-th string of a list property, or NULL. */
static const char *
prop_str(const struct fdt_node *n, const char *name, unsigned i)
{
	const char *p, *end;
	unsigned len;

	if ((p = fdt_getprop(n, name, &len)) == NULL || len == 0)
		return NULL;
	end = p + len;
	while (i-- > 0) {
		p += strnlen(p, (size_t)(end - p)) + 1;
		if (p >= end)
			return NULL;
	}
	return p;
}

static int found_mss, found_smem, found_smp2p;

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	unsigned i, len;
	const char *s;

	(void)cookie; (void)depth; (void)name;
	if (fdt_node_is_compatible(n, "qcom,sdm660-mss-pil")) {
		found_mss = 1;
		if (fdt_node_reg(n, 0, &mss.qdsp6_pa, &mss.qdsp6_sz) != 0 ||
		    fdt_node_reg(n, 1, &mss.rmb_pa, &mss.rmb_sz) != 0)
			found_mss = -1;
		if (fdt_getprop(n, "clocks", &len) != NULL) {
			for (i = 0; i < len / 8 && i < MSS_MAXCLK; i++) {
				mss.clk[i].ph = prop_u32(n, "clocks", 2 * i, 0);
				mss.clk[i].id = prop_u32(n, "clocks", 2 * i + 1,
				    0);
				s = prop_str(n, "clock-names", i);
				strlcpy(mss.clk[i].name, s ? s : "?",
				    sizeof(mss.clk[i].name));
			}
			mss.nclk = i;
		}
		mss.reset_ph = prop_u32(n, "resets", 0, 0);
		mss.reset_id = prop_u32(n, "resets", 1, 0);
		mss.halt_ph = prop_u32(n, "qcom,halt-regs", 0, 0);
		mss.halt_q6 = prop_u32(n, "qcom,halt-regs", 1, 0);
		mss.halt_modem = prop_u32(n, "qcom,halt-regs", 2, 0);
		mss.halt_nc = prop_u32(n, "qcom,halt-regs", 3, 0);
		mss.stop_ph = prop_u32(n, "qcom,smem-states", 0, 0);
		/* memory-region: the MBA's, then the image's (a third, the
		 * metadata's, has no fixed place; ours is alloc_contig()). */
		mss.mba_pa = prop_u32(n, "memory-region", 0, 0);
		mss.mpss_pa = prop_u32(n, "memory-region", 1, 0);
		mss.mdata_pa = prop_u32(n, "memory-region", 2, 0);
		s = prop_str(n, "firmware-name", 0);
		strlcpy(mss.fw_mba, s ? s : "mba.mbn", sizeof(mss.fw_mba));
		s = prop_str(n, "firmware-name", 1);
		strlcpy(mss.fw_mdt, s ? s : "modem.mdt", sizeof(mss.fw_mdt));
		return 0;
	}
	if (fdt_node_is_compatible(n, "qcom,smem")) {
		found_smem = 1;
		mss.smem_pa = prop_u32(n, "memory-region", 0, 0);
		mss.lock_ph = prop_u32(n, "hwlocks", 0, 0);
		mss.lock_id = prop_u32(n, "hwlocks", 1, 0);
		return 0;
	}
	if (fdt_node_is_compatible(n, "qcom,smp2p") &&
	    prop_u32(n, "qcom,remote-pid", 0, ~0u) == MODEM_PID) {
		found_smp2p = 1;
		/* Inbound first, then outbound (Linux's smp2p.c reads them
		 * into smem_items[SMP2P_INBOUND = 0, SMP2P_OUTBOUND = 1]):
		 * on the SDM660 the modem writes 435 and we write 428.  The
		 * other way round, we wrote our item into the modem's, the
		 * modem wrote its header over ours, and never said ready. */
		mss.smp2p_in = prop_u32(n, "qcom,smem", 0, 0);
		mss.smp2p_out = prop_u32(n, "qcom,smem", 1, 0);
		mss.smp2p_local = prop_u32(n, "qcom,local-pid", 0, 0);
		mss.smp2p_remote = MODEM_PID;
		mss.mbox_ph = prop_u32(n, "mboxes", 0, 0);
		mss.mbox_bit = prop_u32(n, "mboxes", 1, ~0u);
		return 0;
	}
	return 0;
}

int
mss_find(const void *dtb)
{
	uint32_t mba_ph, mpss_ph, smem_ph, mdata_ph;

	(void)fdt_walk(dtb, find_cb, NULL);
	if (found_mss <= 0) {
		log_info(&mlog, "no qcom,sdm660-mss-pil node%s; nothing to do\n",
		    found_mss < 0 ? " with two reg ranges" : "");
		return ENXIO;
	}
	if (!found_smem || !found_smp2p) {
		log_warn(&mlog, "the tree has the modem but no %s\n",
		    !found_smem ? "SMEM" : "smp2p edge to it");
		return ENXIO;
	}

	/* The phandles, to addresses. */
	mba_ph = (uint32_t)mss.mba_pa;
	mpss_ph = (uint32_t)mss.mpss_pa;
	mdata_ph = (uint32_t)mss.mdata_pa;
	/*
	 * The metadata's carveout: no-map, so that no cacheable mapping of
	 * the kernel's covers it while it is the modem's (a speculative fetch
	 * there would be an XPU violation, and TrustZone resets the SoC for
	 * that).  Mainline leaves its place to Linux (alloc-ranges); MEECHO's
	 * tree fixes it (port/clover/sdm660-xiaomi-clover-meecho.dtsi).
	 */
	if (mdata_ph == 0 || fdt_phandle_reg(dtb, mdata_ph, 0, &mss.mdata_pa,
	    &mss.mdata_sz) != 0 || mss.mdata_sz < 4096) {
		log_warn(&mlog, "no place for the metadata: the modem's third "
		    "memory-region has no reg; not starting it\n");
		return ENXIO;
	}
	smem_ph = (uint32_t)mss.smem_pa;
	if (fdt_phandle_reg(dtb, mba_ph, 0, &mss.mba_pa, &mss.mba_sz) != 0 ||
	    fdt_phandle_reg(dtb, mpss_ph, 0, &mss.mpss_pa, &mss.mpss_sz) != 0 ||
	    fdt_phandle_reg(dtb, smem_ph, 0, &mss.smem_pa, &mss.smem_sz) != 0 ||
	    fdt_phandle_reg(dtb, mss.lock_ph, 0, &mss.lock_pa,
	    &mss.lock_sz) != 0 ||
	    fdt_phandle_reg(dtb, mss.halt_ph, 0, &mss.halt_pa,
	    &mss.halt_sz) != 0 ||
	    fdt_phandle_reg(dtb, mss.mbox_ph, 0, &mss.mbox_pa,
	    &mss.mbox_sz) != 0) {
		log_warn(&mlog, "a phandle of the modem's leads nowhere (mba "
		    "%u mpss %u smem %u lock %u halt %u mbox %u)\n", mba_ph,
		    mpss_ph, smem_ph, mss.lock_ph, mss.halt_ph, mss.mbox_ph);
		return ENXIO;
	}
	if (mss.mba_sz == 0 || mss.mpss_sz == 0 || mss.mbox_bit > 31 ||
	    mss.lock_sz < (mss.lock_id + 1) * 0x1000u ||
	    mss.halt_sz < mss.halt_modem + 0x10 || mss.mbox_sz < 0x10) {
		log_warn(&mlog, "the modem's tree is not one this reads (mba "
		    "%llx mpss %llx bit %u lock %u)\n",
		    (unsigned long long)mss.mba_sz,
		    (unsigned long long)mss.mpss_sz, mss.mbox_bit, mss.lock_id);
		return ENXIO;
	}
	log_info(&mlog, "metadata %llx+%llx\n", (unsigned long long)mss.mdata_pa,
	    (unsigned long long)mss.mdata_sz);
	log_info(&mlog, "qdsp6 %llx rmb %llx; mba %llx+%llx, mpss %llx+%llx; "
	    "smem %llx+%llx (lock %u), smp2p %u/%u, mailbox %llx bit %u; "
	    "firmware %s, %s; %u clocks\n",
	    (unsigned long long)mss.qdsp6_pa, (unsigned long long)mss.rmb_pa,
	    (unsigned long long)mss.mba_pa, (unsigned long long)mss.mba_sz,
	    (unsigned long long)mss.mpss_pa, (unsigned long long)mss.mpss_sz,
	    (unsigned long long)mss.smem_pa, (unsigned long long)mss.smem_sz,
	    mss.lock_id, mss.smp2p_out, mss.smp2p_in,
	    (unsigned long long)mss.mbox_pa, mss.mbox_bit, mss.fw_mba,
	    mss.fw_mdt, mss.nclk);
	return OK;
}
