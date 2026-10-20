/*
 * The WCN3990's copy engines (wcn_ce.h).  Register order and values as
 * Linux's ce.c sets them for this chip (64-bit descriptors, shadow write
 * index of the send rings, read indices in host memory); the handling of
 * the cache is MEECHO's own - see the rules in wcn_ce.h.
 */

#include <errno.h>
#include <string.h>

#include "wcn_ce.h"

#define DPL	(WCN_CE_LINE / WCN_CE_DESC)	/* descriptors per line */

/*
 * Linux's host_ce_config_wlan.  CE7 (the diagnostic window) gets no
 * receive buffers here: nothing reads them, and a two-descriptor ring
 * never fills the line it lies in (wcn_ce_rx_commit).
 */
const struct wcn_ce_attr wcn_ce_host_cfg[WCN_CE_COUNT] = {
	/* 0: host->target, HTC control */
	{ 0,			16,	2048,	0,	0 },
	/* 1: target->host, HTT and HTC */
	{ 0,			0,	2048,	512,	2048 },
	/* 2: target->host, WMI */
	{ 0,			0,	2048,	64,	2048 },
	/* 3: host->target, WMI */
	{ 0,			32,	2048,	0,	0 },
	/* 4: host->target, HTT; no interrupt - polled */
	{ WCN_CE_A_NOINTR,	2048,	256,	0,	0 },
	/* 5: target->host, HTT (the IPA micro controller's path) */
	{ 0,			0,	512,	512,	512 },
	/* 6: the target's own memcpy */
	{ WCN_CE_A_QUIET,	0,	0,	0,	0 },
	/* 7: diagnostic window */
	{ WCN_CE_A_QUIET,	2,	2048,	2,	0 },
	/* 8: target -> uMC */
	{ WCN_CE_A_QUIET,	0,	2048,	128,	2048 },
	/* 9, 10: target->host, HTT */
	{ 0,			0,	2048,	512,	2048 },
	{ 0,			0,	2048,	512,	2048 },
	/* 11: target->host, packet log */
	{ 0,			0,	2048,	512,	2048 },
};

static uint32_t
rd(struct wcn_ce *ce, uint32_t off)
{

	return ce->ops.rd(ce->ops.arg, off);
}

static void
wr(struct wcn_ce *ce, uint32_t off, uint32_t v)
{

	ce->ops.wr(ce->ops.arg, off, v);
}

static void
rmw(struct wcn_ce *ce, uint32_t off, uint32_t clear, uint32_t set)
{

	wr(ce, off, (rd(ce, off) & ~clear) | set);
}

static void
put32(uint8_t *p, uint32_t v)
{

	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/* Is slot k in [a, b) of a ring with this mask? */
static int
in_range(unsigned mask, unsigned k, unsigned a, unsigned b)
{

	return ((k - a) & mask) < ((b - a) & mask);
}

int
wcn_mem_take(struct wcn_mem *m, size_t len, void **va, uint64_t *da)
{
	size_t at = (m->used + WCN_CE_LINE - 1) & ~(size_t)(WCN_CE_LINE - 1);

	len = (len + WCN_CE_LINE - 1) & ~(size_t)(WCN_CE_LINE - 1);
	if (len == 0 || at > m->len || m->len - at < len)
		return ENOMEM;
	if (((m->da + at) & (WCN_CE_LINE - 1)) != 0 ||
	    m->da + at + len > WCN_CE_DMA_LIMIT)
		return EINVAL;
	*va = m->va + at;
	*da = m->da + at;
	m->used = at + len;
	return 0;
}

size_t
wcn_ce_ctx_size(const struct wcn_ce_attr *cfg)
{
	size_t n = 0;
	unsigned i;

	for (i = 0; i < WCN_CE_COUNT; i++)
		n += cfg[i].src_n + cfg[i].dst_n;
	return n;
}

static int
pow2(unsigned n)
{

	return n != 0 && (n & (n - 1)) == 0;
}

static int
ring_new(struct wcn_ce_ring *r, unsigned n, struct wcn_mem *mem,
	void ***ctxp)
{
	void *va;
	int e;

	if (!pow2(n))
		return EINVAL;
	if ((e = wcn_mem_take(mem, (size_t)n * WCN_CE_DESC, &va, &r->da)) != 0)
		return e;
	r->desc = va;
	memset(r->desc, 0, (size_t)n * WCN_CE_DESC);
	r->n = n;
	r->mask = n - 1;
	r->sw = r->wr = r->pub = 0;
	r->ctx = *ctxp;
	memset(r->ctx, 0, n * sizeof(void *));
	*ctxp += n;
	return 0;
}

/* The engine's read index of a ring: from host memory or its register. */
static unsigned
src_hw(struct wcn_ce *ce, struct wcn_ce_pipe *p)
{

	if (p->attr.flags & WCN_CE_A_NOINTR) {
		ce->ops.inval(ce->ops.arg, (void *)(uintptr_t)&ce->rri[p->id], 4);
		return ce->rri[p->id] & 0xffff & p->src.mask;
	}
	return rd(ce, WCN_CE_BASE(p->id) + WCN_CE_SR_RD) & p->src.mask;
}

int
wcn_ce_init(struct wcn_ce *ce, const struct wcn_ce_ops *ops,
	const struct wcn_ce_attr *cfg, struct wcn_mem *mem, void **ctxmem)
{
	struct wcn_ce_pipe *p;
	void **ctx = ctxmem;
	void *va;
	uint32_t base;
	unsigned i;
	int e;

	memset(ce, 0, sizeof(*ce));
	ce->ops = *ops;

	/*
	 * The read indices: a line of their own, written only by the
	 * engines.  Cleaned once, zeroed, so that no dirty line of it is
	 * ever written back over them.
	 */
	if ((e = wcn_mem_take(mem, WCN_CE_COUNT * 4, &va, &ce->rri_da)) != 0)
		return e;
	ce->rri = va;
	memset(va, 0, WCN_CE_COUNT * 4);
	ce->ops.flush(ce->ops.arg, va, WCN_CE_LINE);
	wr(ce, WCN_CE_RRI_LO, (uint32_t)ce->rri_da);
	wr(ce, WCN_CE_RRI_HI, (uint32_t)(ce->rri_da >> 32) & 0x1f);
	for (i = 0; i < WCN_CE_COUNT; i++)
		rmw(ce, WCN_CE_BASE(i) + WCN_CE_CTRL1, 0, WCN_CE_CTRL1_RRI_UPD);

	for (i = 0; i < WCN_CE_COUNT; i++) {
		p = &ce->pipe[i];
		p->id = i;
		p->attr = cfg[i];
		base = WCN_CE_BASE(i);

		if (cfg[i].src_n != 0) {
			if ((e = ring_new(&p->src, cfg[i].src_n, mem,
			    &ctx)) != 0)
				return e;
			ce->ops.clean(ce->ops.arg, p->src.desc,
			    (size_t)p->src.n * WCN_CE_DESC);
			p->has_src = 1;
			/* Take the engine's indices as found, as Linux does. */
			p->src.sw = src_hw(ce, p);
			p->src.wr = rd(ce, base + WCN_CE_SR_WR) & p->src.mask;
			wr(ce, base + WCN_CE_SR_BASE_LO, (uint32_t)p->src.da);
			wr(ce, base + WCN_CE_SR_BASE_HI,
			    (uint32_t)(p->src.da >> 32) & 0x1f);
			wr(ce, base + WCN_CE_SR_SIZE, p->src.n);
			rmw(ce, base + WCN_CE_CTRL1, WCN_CE_CTRL1_DMAX,
			    cfg[i].src_max & WCN_CE_CTRL1_DMAX);
			rmw(ce, base + WCN_CE_CTRL1, WCN_CE_CTRL1_SR_SWAP, 0);
			rmw(ce, base + WCN_CE_SR_WM, 0xffff0000, 0);
			rmw(ce, base + WCN_CE_SR_WM, 0x0000ffff, p->src.n);
		}
		if (cfg[i].dst_n != 0) {
			if ((e = ring_new(&p->dst, cfg[i].dst_n, mem,
			    &ctx)) != 0)
				return e;
			ce->ops.clean(ce->ops.arg, p->dst.desc,
			    (size_t)p->dst.n * WCN_CE_DESC);
			p->has_dst = 1;
			p->dst.sw = rd(ce, base + WCN_CE_DR_RD) & p->dst.mask;
			p->dst.wr = rd(ce, base + WCN_CE_DR_WR) & p->dst.mask;
			/*
			 * Descriptors the engine still holds from before
			 * would point at memory that is no longer ours:
			 * start empty.  The write index is given back the
			 * read index - nothing posted.
			 */
			if (p->dst.wr != p->dst.sw) {
				p->dst.wr = p->dst.sw;
				wr(ce, base + WCN_CE_DR_WR, p->dst.wr);
			}
			p->dst.pub = p->dst.wr;
			wr(ce, base + WCN_CE_DR_BASE_LO, (uint32_t)p->dst.da);
			rmw(ce, base + WCN_CE_DR_BASE_HI, 0x1f,
			    (uint32_t)(p->dst.da >> 32) & 0x1f);
			wr(ce, base + WCN_CE_DR_SIZE, p->dst.n);
			rmw(ce, base + WCN_CE_CTRL1, WCN_CE_CTRL1_DR_SWAP, 0);
			rmw(ce, base + WCN_CE_DR_WM, 0xffff0000, 0);
			rmw(ce, base + WCN_CE_DR_WM, 0x0000ffff, p->dst.n);
		}
		wcn_ce_intr_enable(ce, i,
		    !(cfg[i].flags & (WCN_CE_A_NOINTR | WCN_CE_A_QUIET)) &&
		    (p->has_src || p->has_dst));
	}
	return 0;
}

unsigned
wcn_ce_send_room(struct wcn_ce *ce, unsigned id)
{
	struct wcn_ce_ring *r = &ce->pipe[id].src;

	if (!ce->pipe[id].has_src)
		return 0;
	/*
	 * From the completions taken (sw), not from the engine's read
	 * index: a slot the engine is done with but whose context was not
	 * taken yet is not free.  (Linux counts from the engine's index.)
	 */
	return (r->sw - 1 - r->wr) & r->mask;
}

int
wcn_ce_send(struct wcn_ce *ce, unsigned id, uint64_t da, size_t len,
	unsigned xid, int gather, void *ctx)
{
	struct wcn_ce_pipe *p;
	struct wcn_ce_ring *r;
	uint8_t *d;
	uint32_t hi;

	if (id >= WCN_CE_COUNT || !ce->pipe[id].has_src)
		return EINVAL;
	p = &ce->pipe[id];
	r = &p->src;
	if (len == 0 || len > p->attr.src_max || len > 0xffff ||
	    da + len > WCN_CE_DMA_LIMIT || xid > 0xfff)
		return EINVAL;
	if (wcn_ce_send_room(ce, id) == 0) {
		ce->stat_noroom++;
		return EAGAIN;
	}

	d = r->desc + (size_t)r->wr * WCN_CE_DESC;
	hi = (uint32_t)(da >> 32) & 0x1f;
	/*
	 * Gather: bit 31 of the high word (and bit 0 of the flags).  Linux
	 * also leaves bit 16 there - its own CE_SEND_FLAG_GATHER, or'ed into
	 * the word whole - and that is what the hardware has been seen to
	 * take; kept.
	 */
	if (gather)
		hi |= WCN_CE_HI_GATHER | 0x00010000;
	put32(d + 0, (uint32_t)da);
	put32(d + 4, hi);
	d[8] = len;
	d[9] = len >> 8;
	d[10] = (gather ? WCN_CE_DF_GATHER : 0) | (xid << WCN_CE_DF_META_SHIFT);
	d[11] = (xid << WCN_CE_DF_META_SHIFT) >> 8;
	put32(d + 12, 0);
	ce->ops.clean(ce->ops.arg, d, WCN_CE_DESC);

	r->ctx[r->wr] = ctx;
	r->wr = (r->wr + 1) & r->mask;
	/* The pieces of a gather go to the engine with the last one. */
	if (!gather)
		wr(ce, WCN_CE_SHADOW_SR_WR(id), r->wr);
	return 0;
}

int
wcn_ce_send_done(struct wcn_ce *ce, unsigned id, void **ctx)
{
	struct wcn_ce_pipe *p = &ce->pipe[id];
	struct wcn_ce_ring *r = &p->src;
	unsigned hw;

	if (!p->has_src)
		return EINVAL;
	if (r->sw == r->wr)
		return EAGAIN;
	hw = src_hw(ce, p);
	/* Only what was sent can be done: an index outside is the engine's. */
	if (!in_range(r->mask, r->sw, r->sw, hw) ||
	    !in_range(r->mask, (hw - 1) & r->mask, r->sw, r->wr))
		return EAGAIN;
	*ctx = r->ctx[r->sw];
	r->ctx[r->sw] = NULL;
	r->sw = (r->sw + 1) & r->mask;
	return 0;
}

int
wcn_ce_rx_post(struct wcn_ce *ce, unsigned id, void *va, uint64_t da,
	size_t len, void *ctx)
{
	struct wcn_ce_pipe *p;
	struct wcn_ce_ring *r;
	unsigned k, first;
	uint8_t *d;

	if (id >= WCN_CE_COUNT || !ce->pipe[id].has_dst)
		return EINVAL;
	p = &ce->pipe[id];
	r = &p->dst;
	if (((uintptr_t)va | da | len) & (WCN_CE_LINE - 1) || len == 0 ||
	    da + len > WCN_CE_DMA_LIMIT)
		return EINVAL;
	if (((r->sw - 1 - r->wr) & r->mask) == 0)
		return EAGAIN;
	/* The line of this slot must hold no descriptor the engine has. */
	first = r->wr & ~(unsigned)(DPL - 1) & r->mask;
	for (k = 0; k < DPL && k < r->n; k++)
		if (in_range(r->mask, (first + k) & r->mask, r->sw, r->pub))
			return EAGAIN;

	/* No dirty line of the buffer may be written back over the DMA. */
	ce->ops.flush(ce->ops.arg, va, len);

	d = r->desc + (size_t)r->wr * WCN_CE_DESC;
	put32(d + 0, (uint32_t)da);
	put32(d + 4, (uint32_t)(da >> 32) & 0x1f);
	put32(d + 8, 0);	/* nbytes 0: not filled; flags 0 */
	put32(d + 12, 0);
	ce->ops.clean(ce->ops.arg, d, WCN_CE_DESC);
	r->ctx[r->wr] = ctx;
	r->wr = (r->wr + 1) & r->mask;
	return 0;
}

void
wcn_ce_rx_commit(struct wcn_ce *ce, unsigned id)
{
	struct wcn_ce_ring *r = &ce->pipe[id].dst;
	unsigned to;

	if (!ce->pipe[id].has_dst || r->n < DPL)
		return;
	/*
	 * Whole lines only: a line handed over with a slot still to be
	 * written would have that slot written later beside descriptors
	 * the engine owns.
	 */
	to = r->wr & ~(unsigned)(DPL - 1) & r->mask;
	if (to == r->pub)
		return;
	r->pub = to;
	wr(ce, WCN_CE_BASE(id) + WCN_CE_DR_WR, r->pub);
}

int
wcn_ce_rx_done(struct wcn_ce *ce, unsigned id, void **ctx, size_t *len)
{
	struct wcn_ce_ring *r = &ce->pipe[id].dst;
	volatile uint8_t *d;
	unsigned n;

	if (!ce->pipe[id].has_dst)
		return EINVAL;
	if (!in_range(r->mask, r->sw, r->sw, r->pub))
		return EAGAIN;
	d = r->desc + (size_t)r->sw * WCN_CE_DESC;
	/*
	 * Filled when nbytes is not 0 - the engine may move its read index
	 * before the descriptor lands, so the index says nothing.
	 */
	ce->ops.inval(ce->ops.arg, (void *)(uintptr_t)d, WCN_CE_DESC);
	n = d[8] | d[9] << 8;
	if (n == 0)
		return EAGAIN;
	*ctx = r->ctx[r->sw];
	*len = n;
	r->ctx[r->sw] = NULL;
	r->sw = (r->sw + 1) & r->mask;
	return 0;
}

void
wcn_ce_intr_ack(struct wcn_ce *ce, unsigned id)
{

	wr(ce, WCN_CE_BASE(id) + WCN_CE_HOST_IS, WCN_CE_IS_COPY | WCN_CE_IS_WM);
}

void
wcn_ce_intr_enable(struct wcn_ce *ce, unsigned id, int on)
{
	uint32_t b = WCN_CE_BASE(id);

	rmw(ce, b + WCN_CE_HOST_IE, WCN_CE_IS_COPY, on ? WCN_CE_IS_COPY : 0);
	rmw(ce, b + WCN_CE_HOST_IE, WCN_CE_IS_WM, 0);
	if (!on)
		rmw(ce, b + WCN_CE_MISC_IE, WCN_CE_MISC_ERR, 0);
}
