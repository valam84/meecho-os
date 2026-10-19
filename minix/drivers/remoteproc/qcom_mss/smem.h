/*
 * Qualcomm's shared memory (SMEM), read and allocated from (smem.c): the
 * heap every processor of the SoC shares, where the modem and this side
 * keep their smp2p items and GLINK's FIFOs.  Facts, not code, from Linux's
 * drivers/soc/qcom/smem.c - port/clover/hw-modem.md, section 3.
 *
 * Plain C over a pointer to mapped memory and a pointer to the TCSR mutex
 * register, so that a host stand runs it against a model; the memory is
 * mapped as a device, and every access here is an aligned one of its own
 * width.
 *
 * Two services use it - qcom_mss (the smp2p items of the modem) and qrtr
 * (GLINK's) - and both allocate under the same hardware lock, which takes
 * the applications processor's id: it keeps the modem out, not the other
 * service.  They allocate only while they start, and rc starts them one
 * after the other.
 */
#ifndef _QCOM_SMEM_H
#define _QCOM_SMEM_H

#include <stddef.h>
#include <stdint.h>

#define SMEM_HOST_APPS		0
#define SMEM_HOST_ANY		0xffffffffu
#define SMEM_HOST_COUNT		25

struct smem_part {
	size_t off;		/* from the start of SMEM; 0: none */
	size_t size;
	size_t cacheline;
};

struct smem {
	/* The caller's. */
	volatile uint8_t *base;		/* all of SMEM, mapped */
	size_t size;
	volatile uint32_t *lock;	/* the TCSR mutex SMEM's lock is */
	void (*delay_us)(unsigned us);

	/* smem_init()'s. */
	unsigned version;		/* 11: the global heap; 12: partitions */
	unsigned item_count;
	struct smem_part global;	/* version 12's global partition */
	struct smem_part part[SMEM_HOST_COUNT];	/* apps <-> host n */
	const char *why;		/* the last refusal, for the log */
};

/* Read the header and the partition table; 0, or -1 with s->why. */
int smem_init(struct smem *s);

/*
 * An item's place, as an offset from s->base (0: not there), and its size
 * in *size (may be NULL).  host: the remote processor whose private
 * partition with this side holds it, or SMEM_HOST_ANY for the global heap.
 */
size_t smem_get(struct smem *s, unsigned host, unsigned item, size_t *size);

/*
 * Allocate an item of size bytes, under the lock.  0, 1 if it existed
 * already (Linux's -EEXIST, which its callers take as success), or -1
 * with s->why.
 */
int smem_alloc(struct smem *s, unsigned host, unsigned item, size_t size);

/* Accessors of the mapped memory, aligned to their width. */
static inline uint32_t
smem_rd32(const struct smem *s, size_t off)
{
	return *(volatile uint32_t *)(s->base + off);
}

static inline void
smem_wr32(const struct smem *s, size_t off, uint32_t v)
{
	*(volatile uint32_t *)(s->base + off) = v;
}

static inline uint16_t
smem_rd16(const struct smem *s, size_t off)
{
	return *(volatile uint16_t *)(s->base + off);
}

static inline void
smem_wr16(const struct smem *s, size_t off, uint16_t v)
{
	*(volatile uint16_t *)(s->base + off) = v;
}

/* Byte copies in and out, words where both sides allow. */
void smem_read(const struct smem *s, size_t off, void *buf, size_t n);
void smem_write(const struct smem *s, size_t off, const void *buf, size_t n);

#endif /* _QCOM_SMEM_H */
