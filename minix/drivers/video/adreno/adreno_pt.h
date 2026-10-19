/*
 * adreno_pt.c's interface: one address space as an ARM SMMU v2 context
 * bank walks it (LPAE stage 1, 4 KiB granule).  Its own header so that
 * other services behind an SMMU of the same kind build the same file - the
 * IPA's (minix/drivers/net/qcom_ipa) does; such a service names its log
 * with -DPT_LOG=its_log.
 */
#ifndef _ADRENO_PT_H
#define _ADRENO_PT_H

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>

#ifndef PT_LOG
#define PT_LOG	ad_log
#endif
extern struct log PT_LOG;

#define AD_PT_READ	1
#define AD_PT_WRITE	2
struct ad_ptab;
struct ad_pt {
	struct ad_ptab *root;
	unsigned ias, oas;		/* input and output address bits */
	int start;			/* the level the walk starts at */
};
int pt_init(struct ad_pt *pt, unsigned ias, unsigned oas);
int pt_map(struct ad_pt *pt, uint64_t va, uint64_t pa, int prot);
int pt_unmap(struct ad_pt *pt, uint64_t va);
uint64_t pt_root(const struct ad_pt *pt);
void pt_free(struct ad_pt *pt);

#endif /* _ADRENO_PT_H */
