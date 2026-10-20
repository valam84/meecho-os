/*
 * One device's streams routed through a context bank of an ARM SMMU v2,
 * over LPAE stage-1 tables of the caller's memory (adreno_pt.c) - for the
 * Qualcomm apps SMMU (iommu@16c0000) that the IPA (qcom_ipa) and the Wi-Fi
 * (wcn) share on the Mi Pad 4, each with its own SMR and bank by agreement
 * (port/clover/hw-modem.md, section 9; hw-wifi.md, section 6).
 *
 * Source only: a service builds smmu_route.c and adreno_pt.c with .PATH
 * and -I to this directory and minix/drivers/video/adreno, and
 * -DPT_LOG=its_log.
 *
 * The rules this follows, each learnt on the tablet:
 *   - every register access is one ldr/str with the value in a real
 *     register: the hypervisor emulates the stream-matching registers by
 *     the trapped instruction, and a store of the zero register (wzr)
 *     stopped the machine (hw-gpu.md, sections 15-16);
 *   - the bank first, then where the stream goes (S2CR, 0x300 | bank, the
 *     downstream kernel's value), then what matches it (SMR) - Linux's
 *     order; a FAULT write to S2CR is ignored, so a route cannot be taken
 *     back to "fault", only its SMR invalidated;
 *   - on the apps SMMU a READ of a bank's registers stopped the core
 *     (hw-modem.md, 9.1): nothing here reads a bank unless check is set,
 *     and only the global page 0 (IDRs, SMR, S2CR) is read otherwise;
 *   - sCR0 is nobody's to write: the SMMU is shared.
 */
#ifndef _SMMU_ROUTE_H
#define _SMMU_ROUTE_H

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>

#include "adreno_pt.h"

struct smmu_route {
	/* The caller's. */
	volatile uint8_t *base;		/* the SMMU's window, mapped */
	size_t size;
	struct log *log;
	unsigned smr, cb;		/* the agreed entry and bank */
	unsigned asid;
	unsigned ias;			/* the device's address bits */
	int check;			/* read the bank back (not on apps) */

	/* Filled by smmu_route_init(). */
	unsigned pgshift, nsmr, ncb, oas, pasize;
	uint32_t gr1, cbo;		/* page 1; the bank's offset */
	struct ad_pt pt;
	uint32_t smr_val;
	int routed;
};

int smmu_route_init(struct smmu_route *r);
int smmu_route_map(struct smmu_route *r, uint64_t iova, phys_bytes pa,
	size_t len, int writable);
int smmu_route_unmap(struct smmu_route *r, uint64_t iova, size_t len);
int smmu_route_on(struct smmu_route *r, const uint32_t *sids, unsigned n);
void smmu_route_off(struct smmu_route *r);
int smmu_route_tlbi(struct smmu_route *r);
void smmu_route_report(struct smmu_route *r);

#endif /* _SMMU_ROUTE_H */
