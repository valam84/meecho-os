/*
 * qcom_mss: the modem subsystem (MSS) of a Qualcomm SoC, loaded and
 * started - Linux's qcom_q6v5_mss.c for the SDM660, in its order
 * (port/clover/hw-modem.md, section 4).
 */
#ifndef _QCOM_MSS_H
#define _QCOM_MSS_H

#include <minix/drivers.h>
#include <minix/log.h>

#include <stdint.h>

#include "smem.h"
#include "smp2p.h"

#define MSS_MAXCLK	10

/* Published in DS (1) once the modem says ready; qrtr waits for it. */
#define MSS_DS_UP	"qcom_mss.up"

/* What the tree says, and what is mapped of it. */
struct mss {
	/* The remoteproc node. */
	u64_t qdsp6_pa, qdsp6_sz, rmb_pa, rmb_sz;
	struct { uint32_t ph, id; char name[16]; } clk[MSS_MAXCLK];
	unsigned nclk;
	uint32_t reset_ph, reset_id;
	uint32_t halt_ph, halt_q6, halt_modem, halt_nc;
	u64_t mba_pa, mba_sz, mpss_pa, mpss_sz;
	char fw_mba[32], fw_mdt[32];
	uint32_t stop_ph;			/* qcom,smem-states */

	/* SMEM, its lock, the smp2p edge to the modem and its mailbox. */
	u64_t smem_pa, smem_sz;
	u64_t lock_pa, lock_sz;
	uint32_t lock_ph, lock_id;
	unsigned smp2p_out, smp2p_in, smp2p_remote, smp2p_local;
	uint32_t mbox_ph, mbox_bit;
	u64_t mbox_pa, mbox_sz;
	u64_t halt_pa, halt_sz;

	/* Mapped. */
	volatile uint8_t *qdsp6, *rmb, *halt, *mba, *mpss, *mbox;
	volatile uint8_t *smem_base, *lock_base;

	/* Ownership of the two regions, as masks of VM ids. */
	uint32_t mba_perm, mpss_perm;

	struct smem smem;
	struct smp2p smp2p;
};

extern struct mss mss;
extern struct log mlog;
extern char mss_fwdir[64];

/* mss_find.c */
int mss_find(const void *dtb);

/* mss_fw.c */
int fw_read(const char *name, void **buf, size_t *len, size_t max);

/* mss_boot.c */
int mss_probe(void);
int mss_boot(int stop_after_mba);
void mss_watch(void);

#endif /* _QCOM_MSS_H */
