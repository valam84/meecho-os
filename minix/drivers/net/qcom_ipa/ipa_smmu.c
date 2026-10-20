/*
 * The IPA's streams on the apps SMMU (iommu@16c0000): SMR 0 and bank 0 by
 * agreement with the Wi-Fi (SMR 1, bank 1), over page tables of this
 * service's memory, so that the addresses in the IPA's descriptors are
 * IOVAs below 4 GiB this service chose and nothing else of the AP's memory
 * is reachable from the IPA or from its microcontroller, which the modem
 * drives (port/clover/hw-modem.md, section 9).  The route itself is the
 * shared code in minix/drivers/iommu/arm_smmu (smmu_route.c), which says
 * why it never reads the apps SMMU's banks.
 */

#include "ipa.h"
#include "smmu_route.h"

#define IPA_SMR		0
#define IPA_CB		0
#define IPA_ASID	1
#define IPA_IAS		32		/* the IPA's addresses are 32-bit */

int smmu_check;

static struct smmu_route route;

int
ipa_smmu_init(void)
{
	if (ipa.smmu == NULL)
		return ENXIO;
	route.base = ipa.smmu;
	route.size = (size_t)ipa.smmu_sz;
	route.log = &ilog;
	route.smr = IPA_SMR;
	route.cb = IPA_CB;
	route.asid = IPA_ASID;
	route.ias = IPA_IAS;
	route.check = smmu_check;
	return smmu_route_init(&route);
}

int
ipa_smmu_map(uint32_t iova, phys_bytes pa, size_t len, int writable)
{
	return smmu_route_map(&route, iova, pa, len, writable);
}

int
ipa_smmu_route(void)
{
	if (ipa.nsid == 0)
		return ENXIO;
	return smmu_route_on(&route, ipa.sid, ipa.nsid);
}

void
ipa_smmu_report(void)
{
	smmu_route_report(&route);
}
