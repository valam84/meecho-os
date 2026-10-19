/*
 * Host stand for minix/drivers/net/wcn/wcn_ce.c: a model of the WCN3990's
 * copy engines behind the driver's register hooks, and a model of a
 * write-back data cache between the driver and the memory the engines see.
 *
 * The cache model.  The driver works on its view C of the DMA memory; the
 * engines on the memory M.  Per 64-byte line the model keeps B, the line
 * as it was at the driver's last maintenance of it, and whether an engine
 * wrote the line since.  Then:
 *   - clean of a line the driver changed (C != B) writes it to M whole: if
 *     an engine wrote that line meanwhile, that is a CLOBBER;
 *   - invalidate of a line the driver changed and did not clean throws
 *     the change away: DISCARD;
 *   - an engine reading a line the driver changed and did not clean reads
 *     stale bytes: STALE; an engine writing one is a write the next
 *     eviction may undo: CLOBBER too;
 *   - the driver reading what an engine wrote without invalidating sees
 *     the old bytes - that shows up as wrong data or a lost completion.
 *
 * cetest [-v] [-m MUTANT] - MUTANT: noclean, noinval, noflush, nommu
 * (an engine address outside the arena); a mutant must fail (run.sh).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wcn_ce.h"

#define ARENA		(12u << 20)
#define DA_BASE		0x7f8000000ull	/* above 4 GiB, below 2^35 */
#define LINES		(ARENA / WCN_CE_LINE)

static uint8_t *C, *M, *B;
static uint8_t *devw;			/* per line: an engine wrote it */
static int verbose;
static const char *mutant = "";

static int fails, checks;
static unsigned long clobber, discard, stale, badaddr, dmax, owned;
static int engine_owns(size_t line);

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		fails++;						\
		printf("FAIL %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/*
 * The engines run on their own: before any cache operation or register
 * read of the driver, they may take a step - so a descriptor can be
 * filled between the driver's write of a line and its clean.
 */
static void dev_step(void);
static int async_on = 1, in_dev;

static void
maybe_step(void)
{

	if (async_on && !in_dev && rand() % 2 == 0)
		dev_step();
}

/* ---- the cache ---- */

static size_t
line_of_va(const void *va)
{
	size_t off = (const uint8_t *)va - C;

	if ((const uint8_t *)va < C || off >= ARENA) {
		printf("FAIL: maintenance outside the arena\n");
		exit(1);
	}
	return off / WCN_CE_LINE;
}

static int
dirty(size_t l)
{

	return memcmp(C + l * WCN_CE_LINE, B + l * WCN_CE_LINE,
	    WCN_CE_LINE) != 0;
}

static void
c_clean(void *arg, void *va, size_t len)
{
	size_t l, e;

	(void)arg;
	maybe_step();
	if (strcmp(mutant, "noclean") == 0 || len == 0)
		return;
	e = line_of_va((uint8_t *)va + len - 1);
	for (l = line_of_va(va); l <= e; l++) {
		if (!dirty(l))
			continue;
		if (engine_owns(l)) {
			owned++;
			if (verbose)
				printf("OWNED line %zu (clean)\n", l);
		}
		if (devw[l]) {
			clobber++;
			if (verbose)
				printf("CLOBBER line %zu (clean)\n", l);
		}
		memcpy(M + l * WCN_CE_LINE, C + l * WCN_CE_LINE, WCN_CE_LINE);
		memcpy(B + l * WCN_CE_LINE, C + l * WCN_CE_LINE, WCN_CE_LINE);
		devw[l] = 0;
	}
}

static void
c_inval(void *arg, void *va, size_t len)
{
	size_t l, e;

	(void)arg;
	maybe_step();
	if (strcmp(mutant, "noinval") == 0 || len == 0)
		return;
	e = line_of_va((uint8_t *)va + len - 1);
	for (l = line_of_va(va); l <= e; l++) {
		if (dirty(l)) {
			discard++;
			if (verbose)
				printf("DISCARD line %zu\n", l);
		}
		memcpy(C + l * WCN_CE_LINE, M + l * WCN_CE_LINE, WCN_CE_LINE);
		memcpy(B + l * WCN_CE_LINE, M + l * WCN_CE_LINE, WCN_CE_LINE);
		devw[l] = 0;
	}
}

static void
c_flush(void *arg, void *va, size_t len)
{

	if (strcmp(mutant, "noflush") == 0)
		return;
	c_clean(arg, va, len);
	c_inval(arg, va, len);
}

/* An engine's access to [da, da + len). */
static uint8_t *
dev_mem(uint64_t da, size_t len, int write)
{
	size_t off, l;

	if (da < DA_BASE || da + len > DA_BASE + ARENA || len > ARENA) {
		badaddr++;
		return NULL;
	}
	off = da - DA_BASE;
	for (l = off / WCN_CE_LINE; len && l <= (off + len - 1) / WCN_CE_LINE;
	    l++) {
		if (dirty(l)) {
			if (write)
				clobber++;
			else
				stale++;
			if (verbose)
				printf("%s line %zu\n", write ? "CLOBBER(dev)" :
				    "STALE", l);
		}
		if (write)
			devw[l] = 1;
	}
	return M + off;
}

/* ---- the engines ---- */

#define NREG	(0x60 / 4)
static uint32_t reg[WCN_CE_COUNT][NREG];
static uint32_t rri_lo, rri_hi;
static unsigned long shadow_bad, direct_swi;

/* Messages the target has for the host, per pipe. */
#define QMAX	4096
struct msg {
	uint8_t *data;
	size_t len;
};
static struct msg outq[WCN_CE_COUNT][QMAX];
static unsigned qh[WCN_CE_COUNT], qt[WCN_CE_COUNT];

/* What the target received, per send pipe, in order. */
static struct msg inq[WCN_CE_COUNT][QMAX];
static unsigned ih[WCN_CE_COUNT], it[WCN_CE_COUNT];

static uint8_t gbuf[65536];
static size_t glen;

static int small;
static int late_nbytes;			/* write nbytes a step after DRRI */
static struct { uint8_t *d; unsigned n; } pending;

#define R(id, off)	reg[id][(off) / 4]

static void
rri_write(unsigned id)
{
	uint8_t *p;
	uint64_t a = (uint64_t)rri_hi << 32 | rri_lo;
	uint32_t v;

	if (!(R(id, WCN_CE_CTRL1) & WCN_CE_CTRL1_RRI_UPD))
		return;
	if ((p = dev_mem(a + 4 * id, 4, 1)) == NULL)
		return;
	v = (R(id, WCN_CE_SR_RD) & 0xffff) | (R(id, WCN_CE_DR_RD) << 16);
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static uint64_t
desc_addr(const uint8_t *d)
{

	return (uint64_t)(d[0] | d[1] << 8 | d[2] << 16 |
	    (uint32_t)d[3] << 24) | (uint64_t)(d[4] & 0x1f) << 32;
}

/* Consume what the host put in the send ring of "id". */
static void
dev_send(unsigned id)
{
	uint32_t n = R(id, WCN_CE_SR_SIZE);
	uint64_t ring = (uint64_t)R(id, WCN_CE_SR_BASE_HI) << 32 |
	    R(id, WCN_CE_SR_BASE_LO);
	uint8_t *d, *data;
	unsigned len, flags;

	if (n == 0)
		return;
	while (R(id, WCN_CE_SR_RD) != R(id, WCN_CE_SR_WR)) {
		if ((d = dev_mem(ring + 16 * R(id, WCN_CE_SR_RD), 16,
		    0)) == NULL)
			return;
		len = d[8] | d[9] << 8;
		flags = d[10] | d[11] << 8;
		if (len > (R(id, WCN_CE_CTRL1) & WCN_CE_CTRL1_DMAX))
			dmax++;
		if (((flags & WCN_CE_DF_GATHER) != 0) !=
		    ((d[7] & 0x80) != 0))
			fails++, printf("FAIL: gather bits disagree\n");
		if ((data = dev_mem(desc_addr(d), len, 0)) == NULL)
			return;
		if (glen + len <= sizeof(gbuf)) {
			memcpy(gbuf + glen, data, len);
			glen += len;
		}
		R(id, WCN_CE_SR_RD) = (R(id, WCN_CE_SR_RD) + 1) % n;
		if (!(flags & WCN_CE_DF_GATHER)) {
			struct msg *m = &inq[id][it[id]++ % QMAX];
			m->data = malloc(glen);
			memcpy(m->data, gbuf, glen);
			m->len = glen;
			glen = 0;
		}
		R(id, WCN_CE_HOST_IS) |= WCN_CE_IS_COPY;
		rri_write(id);
	}
}

/* Fill the receive ring of "id" from its queue. */
static void
dev_recv(unsigned id)
{
	uint32_t n = R(id, WCN_CE_DR_SIZE);
	uint64_t ring = (uint64_t)(R(id, WCN_CE_DR_BASE_HI) & 0x1f) << 32 |
	    R(id, WCN_CE_DR_BASE_LO);
	uint8_t *d, *data;
	struct msg *m;

	if (pending.d != NULL) {
		pending.d[8] = pending.n;
		pending.d[9] = pending.n >> 8;
		pending.d = NULL;
	}
	if (n == 0)
		return;
	while (qh[id] != qt[id] && R(id, WCN_CE_DR_RD) != R(id, WCN_CE_DR_WR)) {
		m = &outq[id][qh[id] % QMAX];
		if ((d = dev_mem(ring + 16 * R(id, WCN_CE_DR_RD), 16,
		    1)) == NULL)
			return;
		if ((data = dev_mem(desc_addr(d), m->len, 1)) == NULL)
			return;
		memcpy(data, m->data, m->len);
		free(m->data);
		qh[id]++;
		R(id, WCN_CE_DR_RD) = (R(id, WCN_CE_DR_RD) + 1) % n;
		if (late_nbytes && pending.d == NULL) {
			pending.d = d;
			pending.n = m->len;
		} else {
			d[8] = m->len;
			d[9] = m->len >> 8;
		}
		R(id, WCN_CE_HOST_IS) |= WCN_CE_IS_COPY;
		rri_write(id);
		/* -s: a slow engine, a descriptor a step at most. */
		if (pending.d != NULL || small)
			break;
	}
}

/* The target's side: echo HTC control and WMI (0, 3) on 2, HTT (4) on 1. */
static void
dev_step(void)
{
	static const unsigned echo[WCN_CE_COUNT] = {
		[0] = 2, [3] = 2, [4] = 1,
	};
	unsigned id;
	struct msg *m;

	in_dev = 1;
	for (id = 0; id < WCN_CE_COUNT; id++)
		dev_send(id);
	for (id = 0; id < WCN_CE_COUNT; id++)
		while (ih[id] != it[id]) {
			m = &inq[id][ih[id]++ % QMAX];
			if (echo[id] && qt[echo[id]] - qh[echo[id]] < QMAX) {
				outq[echo[id]][qt[echo[id]]++ % QMAX] = *m;
			} else
				free(m->data);
		}
	for (id = 0; id < WCN_CE_COUNT; id++)
		dev_recv(id);
	in_dev = 0;
}

static int auto_step = 1;

static uint32_t
m_rd(void *arg, uint32_t off)
{
	unsigned id;

	(void)arg;
	maybe_step();
	if (off == WCN_CE_RRI_LO)
		return rri_lo;
	if (off == WCN_CE_RRI_HI)
		return rri_hi;
	id = (off - 0x240000) / 0x1000;
	if (off < 0x240000 || id >= WCN_CE_COUNT || (off & 0xfff) >= 0x60) {
		printf("FAIL: read of 0x%x\n", off);
		fails++;
		return 0;
	}
	return reg[id][(off & 0xfff) / 4];
}

static void
m_wr(void *arg, uint32_t off, uint32_t v)
{
	unsigned id, r;

	(void)arg;
	if (off == WCN_CE_RRI_LO) {
		rri_lo = v;
		return;
	}
	if (off == WCN_CE_RRI_HI) {
		rri_hi = v;
		return;
	}
	if (off >= 0x32000 && off < 0x32000 + 4 * WCN_CE_COUNT) {
		id = (off - 0x32000) / 4;
		if (R(id, WCN_CE_SR_SIZE) == 0) {
			shadow_bad++;	/* a pipe with no send ring */
			return;
		}
		R(id, WCN_CE_SR_WR) = v;
		if (auto_step)
			dev_step();
		return;
	}
	id = (off - 0x240000) / 0x1000;
	r = off & 0xfff;
	if (off < 0x240000 || id >= WCN_CE_COUNT || r >= 0x60) {
		printf("FAIL: write of 0x%x\n", off);
		fails++;
		return;
	}
	if (r == WCN_CE_HOST_IS) {
		reg[id][r / 4] &= ~v;
		return;
	}
	if (r == WCN_CE_SR_WR) {
		direct_swi++;	/* the shadow register is the way in */
		return;
	}
	reg[id][r / 4] = v;
	if (r == WCN_CE_DR_WR && auto_step)
		dev_step();
}

/*
 * Does line "l" hold a receive descriptor the engine owns - posted to it
 * ([DRRI, DWI)) and so liable to be written by it at any moment?
 */
static int
engine_owns(size_t l)
{
	uint64_t ring, da = DA_BASE + (uint64_t)l * WCN_CE_LINE;
	uint32_t n, rd, wr, k, slot;
	unsigned id;

	for (id = 0; id < WCN_CE_COUNT; id++) {
		if ((n = R(id, WCN_CE_DR_SIZE)) == 0)
			continue;
		ring = (uint64_t)(R(id, WCN_CE_DR_BASE_HI) & 0x1f) << 32 |
		    R(id, WCN_CE_DR_BASE_LO);
		if (da + WCN_CE_LINE <= ring || da >= ring + 16 * n)
			continue;
		rd = R(id, WCN_CE_DR_RD);
		wr = R(id, WCN_CE_DR_WR);
		for (k = 0; k < WCN_CE_LINE / 16; k++) {
			if (da + 16 * k < ring || da + 16 * k >= ring + 16 * n)
				continue;
			slot = (da + 16 * k - ring) / 16;
			if ((slot - rd) % n < (wr - rd) % n)
				return 1;
		}
	}
	return 0;
}

/* ---- the test ---- */

static struct wcn_ce ce;
static struct wcn_mem mem;

/*
 * -s: small rings, completions taken a few at a time - the host's write
 * index catches up with its read index, and a line holds descriptors of
 * both sides; with the full-size rings that never happens.
 */
static struct wcn_ce_attr cfg[WCN_CE_COUNT];

static void
pick_cfg(void)
{
	unsigned i;

	memcpy(cfg, wcn_ce_host_cfg, sizeof(cfg));
	if (!small)
		return;
	for (i = 0; i < WCN_CE_COUNT; i++) {
		if (cfg[i].dst_n > 8)
			cfg[i].dst_n = 8;
		if (cfg[i].src_n > 8 && i != 4)
			cfg[i].src_n = 8;
	}
	cfg[4].src_n = 16;
}

struct rxb {
	uint8_t *va;
	uint64_t da;
	size_t len;
};

static int
line_up(unsigned id)
{

	return (R(id, WCN_CE_HOST_IS) & R(id, WCN_CE_HOST_IE) &
	    WCN_CE_IS_COPY) != 0;
}

static unsigned
post_all(unsigned id)
{
	struct rxb *b;
	void *va;
	unsigned n = 0;

	for (;;) {
		b = malloc(sizeof(*b));
		b->len = ce.pipe[id].attr.rx_size;
		if (wcn_mem_take(&mem, b->len, &va, &b->da) != 0) {
			printf("FAIL: arena full\n");
			exit(1);
		}
		b->va = va;
		if (wcn_ce_rx_post(&ce, id, b->va, b->da, b->len, b) != 0) {
			/* Give the memory back: it was the last taken. */
			mem.used -= (b->len + WCN_CE_LINE - 1) &
			    ~(size_t)(WCN_CE_LINE - 1);
			free(b);
			break;
		}
		n++;
	}
	wcn_ce_rx_commit(&ce, id);
	return n;
}

/* Re-post a taken buffer; queue it if its slot's line is not free yet. */
static struct rxb *spare[WCN_CE_COUNT][1024];
static unsigned nspare[WCN_CE_COUNT];

static void
repost(unsigned id, struct rxb *b)
{
	unsigned i, k;

	if (b != NULL)
		spare[id][nspare[id]++] = b;
	for (i = 0; i < nspare[id]; i++)
		if (wcn_ce_rx_post(&ce, id, spare[id][i]->va, spare[id][i]->da,
		    spare[id][i]->len, spare[id][i]) != 0)
			break;
	for (k = 0; i + k < nspare[id]; k++)
		spare[id][k] = spare[id][i + k];
	nspare[id] -= i;
	wcn_ce_rx_commit(&ce, id);
}

/* A message: [pipe, seq lo, seq hi, len lo, len hi] then a pattern. */
static void
fill(uint8_t *p, size_t len, unsigned pipe, unsigned seq)
{
	size_t i;

	p[0] = pipe;
	p[1] = seq;
	p[2] = seq >> 8;
	p[3] = len;
	p[4] = len >> 8;
	for (i = 5; i < len; i++)
		p[i] = (uint8_t)(seq * 31 + i * 7 + pipe);
}

static int
verify(const uint8_t *p, size_t len, unsigned pipe, unsigned seq)
{
	size_t i;

	if (len < 5 || p[0] != pipe || (unsigned)(p[1] | p[2] << 8) != (seq & 0xffff) ||
	    (size_t)(p[3] | p[4] << 8) != len)
		return 0;
	for (i = 5; i < len; i++)
		if (p[i] != (uint8_t)(seq * 31 + i * 7 + pipe))
			return 0;
	return 1;
}

#define TXN	256
static struct txb {
	uint8_t *va;
	uint64_t da;
	int busy;
} txb[TXN];

static struct txb *
tx_get(void)
{
	static unsigned at;
	unsigned i;

	for (i = 0; i < TXN; i++) {
		struct txb *t = &txb[(at + i) % TXN];
		if (!t->busy) {
			at = (at + i + 1) % TXN;
			t->busy = 1;
			return t;
		}
	}
	return NULL;
}

static unsigned sent[WCN_CE_COUNT], got[WCN_CE_COUNT];
static unsigned long bad_data, delivered;

static void
take_completions(void)
{
	void *ctx;
	size_t len;
	unsigned id, lim;
	struct rxb *b;
	static const unsigned from[WCN_CE_COUNT] = { [2] = 1, [1] = 1 };

	for (id = 0; id < WCN_CE_COUNT; id++) {
		if (!ce.pipe[id].has_src)
			continue;
		while (wcn_ce_send_done(&ce, id, &ctx) == 0)
			if (ctx != NULL)
				((struct txb *)ctx)->busy = 0;
	}
	for (id = 0; id < WCN_CE_COUNT; id++) {
		if (!from[id])
			continue;
		wcn_ce_intr_ack(&ce, id);
		lim = small ? (unsigned)(1 + rand() % 3) : ~0u;
		while (lim-- > 0 && wcn_ce_rx_done(&ce, id, &ctx, &len) == 0) {
			b = ctx;
			if (strcmp(mutant, "noinval") != 0)
				ce.ops.inval(ce.ops.arg, b->va, b->len);
			/*
			 * Echoes of pipes 0 and 3 come on 2 in send order
			 * of each; tell them apart by the pipe byte.
			 */
			if (len < 5 || b->va[0] >= WCN_CE_COUNT ||
			    !verify(b->va, len, b->va[0], got[b->va[0]]))
				bad_data++;
			else
				got[b->va[0]]++;
			delivered++;
			/*
			 * The layers above work in the buffer in place now
			 * and then (a header stripped, a frame converted):
			 * its lines are dirty when it goes back.
			 */
			if (rand() % 3 == 0)
				memset(b->va + (rand() % (b->len / 64)) * 64,
				    0xa5, 64);
			repost(id, b);
		}
	}
}

static void
traffic(unsigned rounds, unsigned seed)
{
	static const unsigned pipes[] = { 0, 3, 4 };
	unsigned r, id, pieces, k;
	size_t len, piece, max;
	struct txb *t[3];

	srand(seed);
	for (r = 0; r < rounds; r++) {
		id = pipes[rand() % 3];
		max = id == 4 ? 256 : 2048;
		/* Up to three pieces gathered; the whole fits a buffer. */
		/* In flight at most 32 per pipe: the protocol's flow control. */
		if (sent[id] - got[id] >= 32) {
			dev_step();
			take_completions();
			continue;
		}
		pieces = 1 + rand() % 3;
		max = pieces * max - pieces;
		if (max > 2048)
			max = 2048;
		len = 5 + rand() % (max - 4);
		if (wcn_ce_send_room(&ce, id) < pieces) {
			take_completions();
			continue;
		}
		for (k = 0; k < pieces; k++)
			if ((t[k] = tx_get()) == NULL)
				break;
		if (k < pieces) {
			while (k--)
				t[k]->busy = 0;
			take_completions();
			continue;
		}
		/* The message, split evenly; the last piece takes the rest. */
		{
			uint8_t whole[2048];
			size_t off = 0;

			fill(whole, len, id, sent[id]);
			for (k = 0; k < pieces; k++) {
				piece = k + 1 < pieces ? len / pieces :
				    len - off;
				memcpy(t[k]->va, whole + off, piece);
				ce.ops.clean(ce.ops.arg, t[k]->va, piece);
				CHECK(wcn_ce_send(&ce, id, t[k]->da, piece, id,
				    k + 1 < pieces, t[k]) == 0,
				    "send on %u, piece %u of %u, %zu bytes", id,
				    k, pieces, piece);
				off += piece;
			}
		}
		sent[id]++;
		if (rand() % 4 == 0)
			take_completions();
	}
	for (k = 0; k < 400; k++) {
		dev_step();
		take_completions();
	}
}

static void
setup(void)
{
	void **ctx;
	unsigned i;
	void *va;

	if (posix_memalign((void **)&C, WCN_CE_LINE, ARENA) != 0)
		exit(2);
	M = calloc(1, ARENA);
	B = calloc(1, ARENA);
	devw = calloc(1, LINES);
	memset(C, 0, ARENA);
	memset(reg, 0, sizeof(reg));
	mem.va = C;
	mem.da = DA_BASE;
	mem.len = ARENA;
	mem.used = 0;
	if (strcmp(mutant, "nommu") == 0)
		mem.da = 0x10000;	/* the engine sees other memory */

	pick_cfg();
	ctx = calloc(wcn_ce_ctx_size(cfg), sizeof(void *));
	{
		struct wcn_ce_ops ops = { m_rd, m_wr, c_clean, c_inval,
		    c_flush, NULL };
		CHECK(wcn_ce_init(&ce, &ops, cfg, &mem, ctx) == 0,
		    "init");
	}
	for (i = 0; i < TXN; i++) {
		if (wcn_mem_take(&mem, 2048, &va, &txb[i].da) != 0)
			exit(2);
		txb[i].va = va;
	}
}

int
main(int argc, char **argv)
{
	unsigned i, n, posted[WCN_CE_COUNT];
	void *ctx;
	int c;

	while ((c = getopt(argc, argv, "svm:")) != -1)
		switch (c) {
		case 's':
			small = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'm':
			mutant = optarg;
			break;
		default:
			return 2;
		}

	setup();

	/* Registers after init: Linux's values. */
	for (i = 0; i < WCN_CE_COUNT; i++) {
		const struct wcn_ce_attr *a = &cfg[i];
		int on = !(a->flags & (WCN_CE_A_NOINTR | WCN_CE_A_QUIET));

		CHECK(R(i, WCN_CE_SR_SIZE) == a->src_n, "ce%u src size", i);
		CHECK(R(i, WCN_CE_DR_SIZE) == a->dst_n, "ce%u dst size", i);
		CHECK(R(i, WCN_CE_CTRL1) & WCN_CE_CTRL1_RRI_UPD, "ce%u rri", i);
		if (a->src_n)
			CHECK((R(i, WCN_CE_CTRL1) & WCN_CE_CTRL1_DMAX) ==
			    a->src_max, "ce%u dmax", i);
		CHECK(((R(i, WCN_CE_HOST_IE) & 1) != 0) == on,
		    "ce%u interrupt %s", i, on ? "off" : "on");
		CHECK((R(i, WCN_CE_HOST_IE) & WCN_CE_IS_WM) == 0,
		    "ce%u watermarks", i);
	}
	CHECK(((uint64_t)rri_hi << 32 | rri_lo) == ce.rri_da, "rri address");
	CHECK(strcmp(mutant, "nommu") == 0 || ce.rri_da >= DA_BASE,
	    "rri da");

	/* Receive buffers: all but a line's worth, in whole lines. */
	for (i = 0; i < WCN_CE_COUNT; i++) {
		posted[i] = 0;
		if (cfg[i].rx_size == 0)
			continue;
		posted[i] = post_all(i);
		n = cfg[i].dst_n;
		CHECK(posted[i] == n - 1, "ce%u posted %u of %u", i,
		    posted[i], n);
		CHECK(R(i, WCN_CE_DR_WR) == n - 4, "ce%u write index %u", i,
		    R(i, WCN_CE_DR_WR));
		CHECK(R(i, WCN_CE_DR_WR) % 4 == 0, "ce%u partial line", i);
	}

	/* Traffic: wraps every ring many times; reverse-order nbytes too. */
	traffic(20000, 1);
	late_nbytes = 1;
	traffic(20000, 2);
	late_nbytes = 0;

	for (i = 0; i < WCN_CE_COUNT; i++)
		if (i == 0 || i == 3 || i == 4)
			CHECK(got[i] == sent[i], "ce%u: sent %u, echoed %u",
			    i, sent[i], got[i]);
	printf("traffic: sent %u/%u/%u on 0/3/4, delivered %lu\n",
	    sent[0], sent[3], sent[4], delivered);
	CHECK(bad_data == 0, "%lu messages with wrong bytes", bad_data);

	/* CE4 never raises its line, and its completions still come. */
	CHECK(!line_up(4), "ce4 line up");
	CHECK(wcn_ce_send_room(&ce, 4) == cfg[4].src_n - 1, "ce4 not drained: %u",
	    wcn_ce_send_room(&ce, 4));

	/* A full ring: n - 1 sends, then EAGAIN, while nothing is taken. */
	auto_step = 0;
	async_on = 0;
	for (n = 0; n < 64; n++) {
		struct txb *t = tx_get();
		if (t == NULL) {
			CHECK(0, "send buffers never came back");
			break;
		}
		fill(t->va, 16, 3, 0);
		ce.ops.clean(ce.ops.arg, t->va, 16);
		if (wcn_ce_send(&ce, 3, t->da, 16, 3, 0, t) != 0) {
			t->busy = 0;
			break;
		}
	}
	CHECK(n == cfg[3].src_n - 1, "ring of %u took %u", cfg[3].src_n, n);
	/* The engine consumes them: still no room until taken. */
	dev_send(3);
	CHECK(wcn_ce_send_room(&ce, 3) == 0, "room before completions");
	n = 0;
	while (wcn_ce_send_done(&ce, 3, &ctx) == 0) {
		((struct txb *)ctx)->busy = 0;
		n++;
	}
	CHECK(n == cfg[3].src_n - 1 &&
	    wcn_ce_send_room(&ce, 3) == cfg[3].src_n - 1, "took %u", n);
	for (i = 0; i < WCN_CE_COUNT; i++)
		while (ih[i] != it[i])
			free(inq[i][ih[i]++ % QMAX].data);
	auto_step = 1;
	async_on = 1;

	/* Bad arguments. */
	CHECK(wcn_ce_send(&ce, 4, txb[0].da, 257, 0, 0, NULL) == EINVAL,
	    "over DMAX taken");
	CHECK(wcn_ce_send(&ce, 1, txb[0].da, 16, 0, 0, NULL) == EINVAL,
	    "send on a receive pipe");
	CHECK(wcn_ce_rx_post(&ce, 2, txb[0].va + 8, txb[0].da + 8, 2048,
	    NULL) == EINVAL, "unaligned buffer posted");
	CHECK(wcn_ce_send(&ce, 3, WCN_CE_DMA_LIMIT, 16, 0, 0, NULL) == EINVAL,
	    "address past 35 bits");

	CHECK(clobber == 0, "%lu clobbered lines", clobber);
	CHECK(discard == 0, "%lu discarded writes", discard);
	CHECK(stale == 0, "%lu stale reads", stale);
	CHECK(badaddr == 0, "%lu engine accesses outside", badaddr);
	CHECK(dmax == 0, "%lu sends over DMAX", dmax);
	CHECK(owned == 0, "%lu lines cleaned over descriptors the engine owns",
	    owned);
	CHECK(shadow_bad == 0 && direct_swi == 0,
	    "write index: %lu to a shadow of no ring, %lu direct",
	    shadow_bad, direct_swi);

	printf("%s%d checks, %d failed\n", *mutant ? "mutant: " : "", checks,
	    fails);
	return fails != 0;
}
