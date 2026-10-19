#ifndef _WCN_CE_H
#define _WCN_CE_H

/*
 * The copy engines (CE) of the WCN3990: twelve DMA rings between the host
 * and the WLAN core, through which every HTC message - WMI, HTT, data -
 * goes (port/clover/hw-wifi.md, "Copy engine").
 *
 * No MINIX here.  Registers, memory and cache maintenance come through
 * struct wcn_ce_ops; the host stand (port/test/wcn) puts a model of the
 * engines and of a write-back cache behind it.
 *
 * Caches.  Linux keeps the rings in uncached memory; MEECHO's DMA memory
 * is ordinary cacheable RAM, maintained by hand (sys_cachectl).  A
 * descriptor is 16 bytes and a cache line 64, so four descriptors share a
 * line - and the engine writes the receive descriptors it fills.  Hence the
 * rules this file keeps (and the stand checks):
 *
 *  - a line of a receive ring is written by the host only when none of its
 *    descriptors is posted to the engine: the host never cleans its stale
 *    copy over a descriptor the engine has just filled;
 *  - the host does not write a filled descriptor back (Linux zeroes
 *    "nbytes" there): a descriptor is rewritten whole when posted again;
 *  - before reading a receive descriptor, the RRI word or a received
 *    buffer, the host invalidates it; a buffer is cleaned and invalidated
 *    before it is posted, so that no dirty line of it is written back
 *    over what the engine puts there;
 *  - a send descriptor and the bytes it points to are cleaned before the
 *    write index moves.
 *
 * Every buffer the engine writes must therefore start and end on a line:
 * WCN_CE_LINE.
 */

#include <stddef.h>
#include <stdint.h>

#define WCN_CE_COUNT		12
#define WCN_CE_LINE		64
#define WCN_CE_DESC		16	/* struct ce_desc_64 */

/* Register map, offsets in the 8 MB "membase" window. */
#define WCN_CE_BASE(id)		(0x240000 + 0x1000 * (id))
#define WCN_CE_SR_BASE_LO	0x00
#define WCN_CE_SR_BASE_HI	0x04	/* address bits 36:32 in 4:0 */
#define WCN_CE_SR_SIZE		0x08
#define WCN_CE_DR_BASE_LO	0x0c
#define WCN_CE_DR_BASE_HI	0x10
#define WCN_CE_DR_SIZE		0x14
#define WCN_CE_CTRL1		0x18
#define   WCN_CE_CTRL1_DMAX	0x0000ffff
#define   WCN_CE_CTRL1_SR_SWAP	0x00020000
#define   WCN_CE_CTRL1_DR_SWAP	0x00040000
#define   WCN_CE_CTRL1_RRI_UPD	0x00080000	/* RRI in host memory */
#define WCN_CE_HOST_IE		0x2c
#define WCN_CE_HOST_IS		0x30	/* write 1 to clear */
#define   WCN_CE_IS_COPY	0x01
#define   WCN_CE_IS_WM		0x1e	/* watermarks: never enabled */
#define WCN_CE_MISC_IE		0x34
#define   WCN_CE_MISC_ERR	0x3e0
#define WCN_CE_SR_WR		0x3c
#define WCN_CE_DR_WR		0x40
#define WCN_CE_SR_RD		0x44
#define WCN_CE_DR_RD		0x48
#define WCN_CE_SR_WM		0x4c
#define WCN_CE_DR_WM		0x50

#define WCN_CE_WRAP		0x24c000
#define WCN_CE_RRI_LO		0x24c004
#define WCN_CE_RRI_HI		0x24c008

/* A source ring's write index goes to its shadow register, not 0x3c. */
#define WCN_CE_SHADOW_SR_WR(id)	(0x32000 + 4 * (id))

/* Descriptor flags (bytes 10-11), and the gather bit of the high word. */
#define WCN_CE_DF_GATHER	0x0001
#define WCN_CE_DF_META_SHIFT	4	/* transfer id: bits 15:4 */
#define WCN_CE_DF_META_MASK	0xfff0
#define WCN_CE_HI_GATHER	0x80000000u

/* The engine's reach: 35 bits (Linux's DMA mask for this chip). */
#define WCN_CE_DMA_LIMIT	((uint64_t)1 << 35)

/* Host-side attributes of a pipe (the host table of hw-wifi.md). */
#define WCN_CE_A_NOINTR		0x0008	/* CE_ATTR_DIS_INTR: polled */
#define WCN_CE_A_QUIET		0x10000	/* no handler: interrupt left off */

struct wcn_ce_attr {
	unsigned flags;
	unsigned src_n;			/* send descriptors, power of 2 */
	unsigned src_max;		/* largest send, DMAX */
	unsigned dst_n;			/* receive descriptors, power of 2 */
	unsigned rx_size;		/* receive buffer size; 0: none posted */
};

struct wcn_ce_ops {
	uint32_t (*rd)(void *arg, uint32_t off);
	void (*wr)(void *arg, uint32_t off, uint32_t v);
	void (*clean)(void *arg, void *va, size_t len);
	void (*inval)(void *arg, void *va, size_t len);
	void (*flush)(void *arg, void *va, size_t len);	/* clean + inval */
	void *arg;
};

/* DMA memory: a contiguous block the caller owns, handed out in lines. */
struct wcn_mem {
	uint8_t *va;
	uint64_t da;			/* the address the engine uses */
	size_t len;
	size_t used;
};

int wcn_mem_take(struct wcn_mem *m, size_t len, void **va, uint64_t *da);

struct wcn_ce_ring {
	uint8_t *desc;			/* n descriptors, line aligned */
	uint64_t da;
	unsigned n, mask;
	unsigned sw;			/* next completion to take */
	unsigned wr;			/* next slot to fill */
	unsigned pub;			/* receive: slots handed to the engine
					 * end here (its write index) */
	void **ctx;			/* per slot: the caller's context */
};

struct wcn_ce_pipe {
	unsigned id;
	struct wcn_ce_attr attr;
	struct wcn_ce_ring src, dst;
	int has_src, has_dst;
};

struct wcn_ce {
	struct wcn_ce_ops ops;
	volatile uint32_t *rri;		/* the engines' read indices */
	uint64_t rri_da;
	struct wcn_ce_pipe pipe[WCN_CE_COUNT];
	unsigned long stat_noroom;	/* sends refused: ring full */
};

/* The host table of Linux's snoc.c, as hw-wifi.md has it. */
extern const struct wcn_ce_attr wcn_ce_host_cfg[WCN_CE_COUNT];

/*
 * Set up all twelve engines from "cfg": rings and the RRI array out of
 * "mem", base and size registers, the RRI address, interrupts enabled
 * where the pipe is not polled.  After QMI's WLAN_CFG and WLAN_MODE.
 * "ctx" storage comes from "ctxmem" (wcn_ce_ctx_size() pointers).
 */
size_t wcn_ce_ctx_size(const struct wcn_ce_attr *cfg);
int wcn_ce_init(struct wcn_ce *ce, const struct wcn_ce_ops *ops,
	const struct wcn_ce_attr *cfg, struct wcn_mem *mem, void **ctxmem);

/*
 * Send [da, da + len) on pipe "id" with transfer id "xid"; "gather": more
 * pieces follow, to land in one receive buffer - the write index moves only
 * with the last piece.  The caller has cleaned the bytes.  0, or EAGAIN
 * (no room: ring full - completions not taken yet), EINVAL.
 */
int wcn_ce_send(struct wcn_ce *ce, unsigned id, uint64_t da, size_t len,
	unsigned xid, int gather, void *ctx);

/* Room in a send ring, counted from the completions taken. */
unsigned wcn_ce_send_room(struct wcn_ce *ce, unsigned id);

/* Take one finished send: 0 and *ctx, or EAGAIN. */
int wcn_ce_send_done(struct wcn_ce *ce, unsigned id, void **ctx);

/*
 * Post a receive buffer: [va, va + len) at engine address da, line
 * aligned.  Buffers are queued and handed to the engine a line of
 * descriptors at a time (wcn_ce_rx_commit); EAGAIN when the next slot's
 * line still holds a posted descriptor or the ring is full.
 */
int wcn_ce_rx_post(struct wcn_ce *ce, unsigned id, void *va, uint64_t da,
	size_t len, void *ctx);
void wcn_ce_rx_commit(struct wcn_ce *ce, unsigned id);

/*
 * Take one received buffer: 0, *ctx and *len; EAGAIN if none.  The caller
 * invalidates the buffer before reading it (a line may have been fetched
 * again since it was posted).
 */
int wcn_ce_rx_done(struct wcn_ce *ce, unsigned id, void **ctx, size_t *len);

/* Interrupt of engine "id": clear its status before looking at rings. */
void wcn_ce_intr_ack(struct wcn_ce *ce, unsigned id);
void wcn_ce_intr_enable(struct wcn_ce *ce, unsigned id, int on);

#endif /* _WCN_CE_H */
