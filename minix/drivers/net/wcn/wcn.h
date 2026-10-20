#ifndef _WCN_H
#define _WCN_H

/*
 * The wcn service's own declarations (the pure layers have their headers:
 * wcn_ce.h, wcn_htc.h, wcn_wlfw.h, wcn_fw.h).
 */

#include <minix/drivers.h>
#include <minix/log.h>

#include <minix/netdriver.h>

#include "wcn_ce.h"
#include "wcn_htc.h"
#include "wcn_ioc.h"

extern struct log wlog;

/* What the tree says. */
struct wcn_dt {
	int found;
	u64_t regs_pa, regs_sz;
	u32_t msa_ph;
	u64_t msa_pa, msa_sz;
	int irq[WCN_CE_COUNT];
	unsigned nirq;
	/* iommus: the apps SMMU and the WLAN's stream ids (0x1a00, 0x1a01). */
	u32_t smmu_ph;
	u64_t smmu_pa, smmu_sz;
	uint32_t sid[4];
	unsigned nsid;
};
extern struct wcn_dt dt;

/* wcn_hw.c: power, registers, DMA memory, lines, copy engines, HTC. */
int hw_power(int on);
int hw_start(void);		/* CE up, HTC READY, services connected */
void hw_stop(void);
void hw_intr(unsigned long mask);
void hw_smmu_report(void);
void hw_poll(void);		/* look at every ring now */
int hw_take(size_t len, void **va, uint64_t *da);	/* DMA memory */

/* wcn_data.c: HTT and the frames. */
void data_htt(const uint8_t *m, size_t len);
int data_start(void);
int data_send(const uint8_t *eth, size_t len, int no_encrypt);
int data_link(const struct wcn_link *l);
int data_link_up(void);
ssize_t data_net_recv(struct netdriver_data *data, size_t max);
int data_net_send(struct netdriver_data *data, size_t size);
int data_eapol_send(const uint8_t *eth, size_t len);
ssize_t data_eapol_recv(uint8_t *buf, size_t size, unsigned ms);
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


#endif /* _WCN_H */
