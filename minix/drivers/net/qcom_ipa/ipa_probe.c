/*
 * What the device tree says about the IPA, and the probe that only reads:
 * the apps SMMU the IPA's DMA goes through (how the boot loader and the
 * hypervisor left the IPA's streams - bypass or translated - decides what
 * an address in a descriptor is), and the IPA's own registers once its
 * clock is voted.  Nothing is written here.
 */

#include "ipa.h"

#include <minix/fdt.h>

#include <sys/mman.h>

#include <string.h>

static int found;
static uint32_t smmu_ph;

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	const uint8_t *p;
	unsigned len, off;
	uint32_t ph;

	(void)cookie; (void)depth; (void)name;
	if (!fdt_node_is_compatible(n, "qcom,ipa-lite-v2.6"))
		return 0;
	found = 1;
	if (fdt_node_reg(n, 0, &ipa.pa, &ipa.sz) != 0) {
		found = -1;
		return 1;
	}
	/* iommus: the SMMU's phandle and one cell, the stream ID. */
	if ((p = fdt_getprop(n, "iommus", &len)) != NULL) {
		for (off = 0; off + 8 <= len && ipa.nsid < IPA_MAXSID;
		    off += 8) {
			ph = (uint32_t)fdt_read_cells(p + off, 1);
			if (smmu_ph != 0 && ph != smmu_ph)
				continue;	/* one SMMU only */
			smmu_ph = ph;
			ipa.sid[ipa.nsid++] =
			    (uint32_t)fdt_read_cells(p + off + 4, 1);
		}
	}
	return 1;
}

int
ipa_find(const void *dtb)
{
	(void)fdt_walk(dtb, find_cb, NULL);
	if (found <= 0) {
		log_info(&ilog, "no qcom,ipa-lite-v2.6 node%s; nothing to do\n",
		    found < 0 ? " with a reg" : "");
		return ENXIO;
	}
	if (ipa.sz < IPA_SRAM + 0x100) {
		log_warn(&ilog, "the IPA's window is %llx bytes, too small\n",
		    (unsigned long long)ipa.sz);
		return ENXIO;
	}
	if (smmu_ph != 0 && fdt_phandle_reg(dtb, smmu_ph, 0, &ipa.smmu_pa,
	    &ipa.smmu_sz) != 0) {
		log_warn(&ilog, "the IPA's iommus phandle %u leads nowhere\n",
		    smmu_ph);
		return ENXIO;
	}
	log_info(&ilog, "ipa %llx+%llx; smmu %llx+%llx, %u stream(s): "
	    "%x %x\n", (unsigned long long)ipa.pa, (unsigned long long)ipa.sz,
	    (unsigned long long)ipa.smmu_pa, (unsigned long long)ipa.smmu_sz,
	    ipa.nsid, ipa.nsid > 0 ? ipa.sid[0] : 0,
	    ipa.nsid > 1 ? ipa.sid[1] : 0);
	return OK;
}

volatile uint8_t *
ipa_map(u64_t pa, u64_t sz, const char *what)
{
	void *v;

	v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)sz);
	if (v == MAP_FAILED) {
		log_warn(&ilog, "cannot map %s (%llx+%llx); not granted by "
		    "RS?\n", what, (unsigned long long)pa,
		    (unsigned long long)sz);
		return NULL;
	}
	return v;
}

static const char *
s2cr_type(uint32_t v)
{
	switch ((v >> 16) & 3) {
	case 0: return "translate";
	case 1: return "bypass";
	case 2: return "fault";
	default: return "reserved";
	}
}

static const char *
cbar_type(uint32_t v)
{
	switch ((v >> 16) & 3) {
	case 0: return "s2";
	case 1: return "s1, s2 bypass";
	case 2: return "s1, s2 fault";
	default: return "s1+s2";
	}
}

/* Whose stream an SMR matches, of the ones this probe cares about. */
static const char *
whose(uint32_t smr)
{
	static const struct { uint32_t sid; const char *who; } known[] = {
		{ 0x19c0, "ipa" }, { 0x19c2, "ipa uc" },
		{ 0x1a00, "wifi" }, { 0x1a01, "wifi" },
	};
	uint32_t id = smr & 0x7fff, mask = (smr >> 16) & 0x7fff;
	unsigned i;

	for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
		if (((known[i].sid ^ id) & ~mask & 0x7fff) == 0)
			return known[i].who;
	return "";
}

/*
 * The apps SMMU, read only: the global registers, every stream match and
 * where it goes, every context bank's type and whether it is on.
 */
int
ipa_probe_smmu(int banks)
{
	uint32_t scr0, idr0, idr1, idr2, smr, s2cr, cbar, sctlr;
	unsigned nsmr, ncb, pgshift, numpage, i, hits = 0, free_smr = 0;
	u64_t cb_base;

	if (ipa.smmu_sz == 0) {
		log_info(&ilog, "no SMMU in iommus; DMA addresses are "
		    "physical\n");
		return OK;
	}
	if (ipa.smmu == NULL && (ipa.smmu = ipa_map(ipa.smmu_pa, ipa.smmu_sz,
	    "the SMMU")) == NULL)
		return ENOMEM;

	scr0 = rd32(ipa.smmu, SMMU_SCR0);
	idr0 = rd32(ipa.smmu, SMMU_IDR0);
	idr1 = rd32(ipa.smmu, SMMU_IDR1);
	idr2 = rd32(ipa.smmu, SMMU_IDR2);
	nsmr = idr0 & 0xff;
	ncb = idr1 & 0xff;
	pgshift = (idr1 & (1u << 31)) ? 16 : 12;
	numpage = 1u << (((idr1 >> 28) & 7) + 1);
	cb_base = (u64_t)numpage << pgshift;
	log_info(&ilog, "smmu: sCR0 %08x (CLIENTPD %u, USFCFG %u), IDR0 %08x "
	    "IDR1 %08x IDR2 %08x IDR7 %08x, sGFSR %08x; %u SMR, %u banks of "
	    "%u KiB at +%llx\n", scr0, scr0 & 1, (scr0 >> 10) & 1, idr0, idr1,
	    idr2, rd32(ipa.smmu, SMMU_IDR7), rd32(ipa.smmu, SMMU_SGFSR), nsmr,
	    ncb, (1u << pgshift) / 1024, (unsigned long long)cb_base);
	if (nsmr > 128 || ncb > 128 ||
	    cb_base + ((u64_t)ncb << pgshift) > ipa.smmu_sz) {
		log_warn(&ilog, "smmu: the IDRs do not fit the window; not "
		    "reading further\n");
		return EIO;
	}

	for (i = 0; i < nsmr; i++) {
		smr = rd32(ipa.smmu, SMMU_SMR(i));
		s2cr = rd32(ipa.smmu, SMMU_S2CR(i));
		if (!(smr & (1u << 31)) && s2cr == 0x000200ff &&
		    whose(smr)[0] == '\0') {
			free_smr++;
			continue;	/* free, as the hypervisor leaves it */
		}
		log_info(&ilog, "smr %2u %08x (id %04x mask %04x%s) s2cr "
		    "%08x %s cb %u %s\n", i, smr, smr & 0x7fff,
		    (smr >> 16) & 0x7fff, (smr & (1u << 31)) ? " valid" : "",
		    s2cr, s2cr_type(s2cr), s2cr & 0xff, whose(smr));
		if ((smr & (1u << 31)) && whose(smr)[0] != '\0')
			hits++;
	}
	log_info(&ilog, "smmu: %u valid stream match(es) for ipa/wifi, %u of "
	    "%u SMR free (s2cr 000200ff); unmatched streams go by USFCFG "
	    "(%s)\n", hits, free_smr, nsmr,
	    (scr0 >> 10) & 1 ? "fault" : "bypass");
	if (!banks)
		return OK;

	/*
	 * The banks.  The first read of each kind is said before it is made:
	 * on the Mi Pad 4 one of them did not come back (the core it was on
	 * stopped; port/clover/hw-modem.md, section 9).
	 */
	log_info(&ilog, "smmu: reading CBAR 0 (+%x)\n", 1u << pgshift);
	cbar = rd32(ipa.smmu, (1u << pgshift) + SMMU_CBAR(0));
	log_info(&ilog, "smmu: CBAR 0 %08x; reading bank 0's SCTLR (+%llx)\n",
	    cbar, (unsigned long long)cb_base);
	sctlr = rd32(ipa.smmu, (uint32_t)cb_base + SMMU_CB_SCTLR);
	log_info(&ilog, "smmu: bank 0 SCTLR %08x\n", sctlr);
	for (i = 0; i < ncb; i++) {
		cbar = rd32(ipa.smmu, (1u << pgshift) + SMMU_CBAR(i));
		sctlr = rd32(ipa.smmu, (uint32_t)(cb_base + ((u64_t)i <<
		    pgshift)) + SMMU_CB_SCTLR);
		if (cbar == 0 && sctlr == 0)
			continue;
		log_info(&ilog, "cb %2u cbar %08x (%s, vmid %u, irpt %u) sctlr "
		    "%08x tcr %08x fsr %08x\n", i, cbar, cbar_type(cbar),
		    cbar & 0xff, cbar >> 24, sctlr,
		    rd32(ipa.smmu, (uint32_t)(cb_base + ((u64_t)i << pgshift))
		    + SMMU_CB_TCR),
		    rd32(ipa.smmu, (uint32_t)(cb_base + ((u64_t)i << pgshift))
		    + SMMU_CB_FSR));
	}
	return OK;
}

/*
 * The IPA's own registers - only the ones ipa2-lite reads itself, and
 * the AP's six pipes - with its clock voted: an unclocked block does not
 * answer, and a read that stalls stalls this core.
 */
int
ipa_probe_regs(void)
{
	uint32_t smem, v;
	unsigned i;
	int r;

	if ((r = ipa_rpm_clock(40000)) != OK) {
		log_warn(&ilog, "the IPA's clock: RPM vote failed (%d); not "
		    "reading the IPA\n", r);
		return r;
	}
	STEP("ipa: clock voted (40 MHz)\n");
	if (ipa.base == NULL && (ipa.base = ipa_map(ipa.pa, ipa.sz,
	    "the IPA")) == NULL) {
		(void)ipa_rpm_clock(0);
		return ENOMEM;
	}
	STEP("ipa: reading VERSION\n");
	v = rd32(ipa.base, IPA_VERSION);
	log_info(&ilog, "ipa: VERSION %08x; reading the BAM's REVISION\n", v);
	(void)rd32(ipa.base, BAM_REVISION);
	smem = rd32(ipa.base, IPA_SHARED_MEM);
	log_info(&ilog, "ipa: VERSION %08x COMP_HW_VERSION %08x COMP_CFG %08x "
	    "ROUTE %08x SHARED_MEM %08x (restricted %u, size %u) "
	    "ENABLED_PIPES %08x\n", v, rd32(ipa.base, IPA_COMP_HW_VERSION),
	    rd32(ipa.base, IPA_COMP_CFG), rd32(ipa.base, IPA_ROUTE), smem,
	    smem >> 16, smem & 0xffff, rd32(ipa.base, IPA_ENABLED_PIPES));
	log_info(&ilog, "ipa: IRQ_STTS %08x IRQ_EN %08x, uC RESP %08x PARAM "
	    "%08x\n", rd32(ipa.base, IPA_IRQ_STTS_EE0),
	    rd32(ipa.base, IPA_IRQ_EN_EE0), rd32(ipa.base, IPA_UC_RESP),
	    rd32(ipa.base, IPA_UC_RESP_PARAM));
	log_info(&ilog, "bam: CTRL %08x REVISION %08x NUM_PIPES %08x\n",
	    rd32(ipa.base, BAM_CTRL), rd32(ipa.base, BAM_REVISION),
	    rd32(ipa.base, BAM_NUM_PIPES));
	for (i = 0; i < EP_NUM; i++)
		log_info(&ilog, "bam pipe %u: P_CTRL %08x\n", i,
		    rd32(ipa.base, BAM_P_CTRL(i)));
	if (v == 0)
		log_warn(&ilog, "ipa: VERSION reads 0 - no access to the "
		    "block\n");
	return OK;
}
