/*
 * smp2p: 32-bit values between two processors in SMEM (smp2p.c).  Each
 * side writes one item and reads the other's; an item has up to 16 named
 * entries.  From Linux's drivers/soc/qcom/smp2p.c (the layout and the
 * order, not the code).
 *
 * For the modem: our "master-kernel" entry carries bit 0, "stop"; the
 * modem's "slave-kernel" carries its state - bit 0 fatal, 1 ready, 2
 * handover, 3 stop acknowledged (the tree's interrupt numbers of the
 * remoteproc node).  Polled: no interrupt is taken here.
 */
#ifndef _QCOM_SMP2P_H
#define _QCOM_SMP2P_H

#include "smem.h"

struct smp2p {
	/* The caller's. */
	struct smem *smem;
	unsigned local_pid, remote_pid;
	unsigned out_item, in_item;	/* the tree's qcom,smem */
	void (*kick)(void);		/* ring the remote */

	/* smp2p_init()'s and smp2p_poll()'s. */
	size_t out;			/* our item, an offset into SMEM */
	unsigned nout;			/* its entries */
	size_t in;			/* theirs, once there; 0: not yet */
	int negotiated;
};

/* Allocate our item with the named outbound entries; 0 or -1. */
int smp2p_init(struct smp2p *p, const char *const *names, unsigned n);

/* Find their item and negotiate; 1 when there is one, 0 not yet. */
int smp2p_poll(struct smp2p *p);

/* Their entry by name: its value in *v; 0, or -1 (no item or entry yet). */
int smp2p_in(struct smp2p *p, const char *name, uint32_t *v);

/* Change bits of our entry number i (in the order smp2p_init() had). */
void smp2p_set(struct smp2p *p, unsigned i, uint32_t mask, uint32_t value);

#endif /* _QCOM_SMP2P_H */
