#ifndef _WCN_H
#define _WCN_H

/*
 * The wcn service's own declarations (the pure layers have their headers:
 * wcn_ce.h, wcn_htc.h, wcn_wlfw.h, wcn_fw.h).
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include "wcn_ce.h"
#include "wcn_htc.h"

extern struct log wlog;

/* What the tree says. */
struct wcn_dt {
	int found;
	u64_t regs_pa, regs_sz;
	u32_t msa_ph;
	u64_t msa_pa, msa_sz;
	int irq[WCN_CE_COUNT];
	unsigned nirq;
};
extern struct wcn_dt dt;

/* wcn_hw.c: power, registers, DMA memory, lines, copy engines, HTC. */
int hw_power(int on);
int hw_start(void);		/* CE up, HTC READY, services connected */
void hw_stop(void);
void hw_intr(unsigned long mask);
int hw_send(unsigned eid, const void *msg, size_t len);
/* Wait up to "ms" for a message on endpoint "eid" (copied): its length,
 * or -ETIMEDOUT. */
ssize_t hw_wait(unsigned eid, void *buf, size_t size, unsigned ms);
/* A WMI command, waiting up to a second for the credit. */
int hw_wmi_send(const void *msg, size_t len);
/* A frame into one of 16 DMA slots: its address for the engine. */
int hw_frame(const void *frame, size_t len, uint64_t *da);

extern struct htc htc;
extern int wmi_ep, htt_ep;
extern int smmu_ready;

#endif /* _WCN_H */
