/*
 * Модель командного процессора Adreno 5xx для стенда (стадия cp,
 * port/clover/hw-gpu.md, §6, §7 и §17; узел рендеринга, шаг 7).  Что модель
 * знает о железе — то, что там сказано, и формат PM4 (adreno_pm4.xml);
 * ничего из кода службы.
 *
 *   Старт   запись 0 в CP_PFP_ME_CNTL.  К этому моменту: адреса микрокода
 *           ME и PFP ведут (через SMMU, как пошёл бы GPU) в память, где
 *           лежит ровно микрокод — файл без первого слова, — и в ОЗУ, не в
 *           кэше; кольцо — CP_RB_CNTL 0x0800020c и читаемая база; маска
 *           прерываний — A5XX_INT_MASK.  Иначе — провал.
 *   Кольцо  исполняется по записи CP_RB_WPTR, от rptr до wptr, словами из
 *           ОЗУ через SMMU (маршрут SID 0, банк 0, TLB, таблицы) — сразу,
 *           или когда стенд скажет (cp_model_defer(), cp_model_run()):
 *           тогда CP занят, пока не дошёл.  Заголовок с неверной
 *           чётностью, незнакомый пакет, первый пакет не CP_ME_INIT —
 *           провал.  ME_INIT сверяется со значениями Linux для A512.
 *           WHERE_AM_I пишет rptr по адресу; CP_EVENT_WRITE CACHE_FLUSH_TS
 *           — seqno по адресу, бит 20 RBBM_INT_0_STATUS и, если он в маске,
 *           линию 332.  PKT4 пишет CP_SCRATCH, CP_CONTEXT_SWITCH_SAVE_ADDR
 *           (только вне защищённого режима: регистр под CP_PROTECT) и
 *           UCHE_CACHE_INVALIDATE.  CP_INDIRECT_BUFFER_PFE исполняет буфер
 *           клиента.  Доступ кольца к памяти, которой нет в таблицах, —
 *           провал: это память службы.
 *   Буфер   клиента: CP_MEM_WRITE, CP_NOP, PKT4 в CP_SCRATCH,
 *           CP_WAIT_FOR_IDLE.  Отказ SMMU при выборке буфера или записи
 *           CP_MEM_WRITE — как на железе с банком без остановки: в FSR и
 *           FAR, доступ прекращён, CP идёт дальше (выборка — к концу
 *           буфера).  CP_NOP со словом CP_MODEL_HANG — CP встаёт навсегда:
 *           занят, rptr не движется, пока GX не погаснет.
 *   Secure  CP_SET_SECURE_MODE 0 работает, только если zap загружен или
 *           возобновлён после того, как CP прошёл ME_INIT (tzmodel.c), — так
 *           делает Linux; тогда SECVID_TRUST_CNTL становится 0.  Без этого —
 *           провал: на планшете CP встал бы.
 *   Конец   маршрут потока снят, пока CP работает и порты VBIF не
 *           остановлены, или GX погашен так же — провал: на планшете это
 *           обращение в никуда посреди работы.
 */

#include "adreno.h"
#include "adreno_regs.h"
#include "model.h"

#include <minix/qcomscm.h>

#include <stdio.h>
#include <string.h>

#define LINE_GPU	332
#define MAX_CNT		256

#define CP_NOP				0x10
#define CP_WAIT_FOR_IDLE		0x26
#define CP_MEM_WRITE			0x3d
#define CP_INDIRECT_BUFFER_PFE		0x3f
#define A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR	0x083e
#define A5XX_UCHE_CACHE_INVALIDATE_MIN	0x0e91
#define A5XX_UCHE_CACHE_INVALIDATE	0x0e95

static struct {
	int started, me_inited, halted_seen;
	uint32_t rptr;			/* dwords */
	uint32_t wptr;			/* what the service wrote last */
	uint32_t int_status;
	unsigned tz_at_me_init;		/* tz_calls() then */
	unsigned packets;
	const uint8_t *pm4, *pfp;	/* the files, as the stand made them */
	size_t pm4_len, pfp_len;
	int defer, hung, prot;
	unsigned ibs, invals;
	char trail[1024];
} cp;

void
cp_expect(const void *pm4, size_t pm4_len, const void *pfp, size_t pfp_len)
{
	cp.pm4 = pm4;
	cp.pm4_len = pm4_len;
	cp.pfp = pfp;
	cp.pfp_len = pfp_len;
}

void
cp_model_reset(const struct scenario *s)
{
	(void)s;
	cp.started = cp.me_inited = cp.halted_seen = 0;
	cp.rptr = cp.wptr = cp.int_status = 0;
	cp.tz_at_me_init = 0;
	cp.packets = 0;
	cp.defer = cp.hung = 0;
	cp.prot = 1;
	cp.ibs = cp.invals = 0;
	cp.trail[0] = '\0';
	/* Bits of no meaning in the registers hw_init() changes a bit of:
	 * what it did not mean to change must still be there after. */
	model_gpu_set(A5XX_RB_DBG_ECO_CNTL, 0x00000800);
	model_gpu_set(A5XX_VPC_DBG_ECO_CNTL, 0x00000002);
	model_gpu_set(A5XX_HLSQ_DBG_ECO_CNTL, 0x00040001);
}

int cp_model_started(void) { return cp.started; }
unsigned cp_model_packets(void) { return cp.packets; }
int cp_model_hung(void) { return cp.hung; }
unsigned cp_model_ibs(void) { return cp.ibs; }
unsigned cp_model_invals(void) { return cp.invals; }
const char *cp_model_trail(void) { return cp.trail; }
void cp_model_trail_reset(void) { cp.trail[0] = '\0'; }
void cp_model_defer(int on) { cp.defer = on; }

/* The GPU's own hang detection, or an error, raised by the stand. */
void
cp_model_raise(uint32_t bits)
{
	cp.int_status |= bits;
}

static void
say(const char *what)
{
	size_t n = strlen(cp.trail);

	if (n + strlen(what) + 2 < sizeof(cp.trail))
		snprintf(cp.trail + n, sizeof(cp.trail) - n, "%s%s",
		    n > 0 ? " " : "", what);
}

/* The CP still running: stopping its memory or power under it is a fault. */
int
cp_model_busy(void)
{
	return cp.started && !cp.halted_seen;
}

int
cp_model_line_up(int vec)
{
	return vec == LINE_GPU &&
	    (cp.int_status & model_gpu_reg(A5XX_RBBM_INT_0_MASK)) != 0;
}

static uint32_t
parity(uint32_t v)
{
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	return (0x9669u >> (v & 0xf)) & 1;
}

/*
 * A GPU access: through the SMMU, then memory - never the caches.  The
 * service's own (client 0): a fault is the service's, and fails.  A
 * client's: a fault is in FSR and FAR, and the access is dropped (NULL).
 */
static uint8_t *
gpu_mem(uint64_t va, size_t n, int write, int client)
{
	uint64_t pa;
	uint8_t *p;

	if (smmu_gpu_pa(va, write, &pa) != 0) {
		if (client)
			return NULL;
		model_fail("the GPU's %s of %llx faults in the SMMU",
		    write ? "write" : "read", (unsigned long long)va);
	}
	if (((pa & 0xfff) + n) > 4096)
		model_fail("the model reads %zu bytes across a page at %llx", n,
		    (unsigned long long)va);
	if ((p = model_ram_pa(pa, n)) == NULL)
		model_fail("the GPU reached %llx, memory nobody gave it",
		    (unsigned long long)pa);
	return p;
}

static uint32_t
gpu_rd32(uint64_t va)
{
	uint32_t v;

	memcpy(&v, gpu_mem(va, 4, 0, 0), 4);
	return v;
}

static void
gpu_wr32(uint64_t va, uint32_t v)
{
	memcpy(gpu_mem(va, 4, 1, 0), &v, 4);
}

static uint64_t
reg64(uint32_t dw)
{
	return model_gpu_reg(dw) | (uint64_t)model_gpu_reg(dw + 1) << 32;
}

static void
same_as_file(uint32_t base_dw, const uint8_t *file, size_t len,
	const char *what)
{
	uint64_t va = reg64(base_dw);
	size_t i;

	if (file == NULL)
		model_fail("the stand gave the model no %s microcode", what);
	for (i = 4; i < len; i += 4)
		if (gpu_rd32(va + i - 4) != *(const uint32_t *)(const void *)
		    (file + i))
			model_fail("%s microcode at %llx differs at +%zx from "
			    "the file without its first word", what,
			    (unsigned long long)va, i - 4);
}

static void
start(void)
{
	if (cp.started)
		model_fail("the CP started twice");
	if (model_gpu_reg(A5XX_RBBM_INT_0_MASK) != A5XX_INT_MASK)
		model_fail("the CP started with interrupt mask %08x",
		    model_gpu_reg(A5XX_RBBM_INT_0_MASK));
	if (model_gpu_reg(A5XX_CP_RB_CNTL) != A5XX_CP_RB_CNTL_VALUE)
		model_fail("the CP started with CP_RB_CNTL %08x",
		    model_gpu_reg(A5XX_CP_RB_CNTL));
	same_as_file(A5XX_CP_ME_INSTR_BASE, cp.pm4, cp.pm4_len, "ME (pm4)");
	same_as_file(A5XX_CP_PFP_INSTR_BASE, cp.pfp, cp.pfp_len, "PFP");
	(void)gpu_rd32(reg64(A5XX_CP_RB_BASE));
	cp.started = 1;
	cp.rptr = 0;
	cp.prot = 1;
}

/* A packet's words: the ring's, or a client's buffer's. */
struct src {
	int ib;				/* a client's buffer */
	uint64_t va;
	uint32_t pos, len;		/* dwords */
	int fault;
};

static uint32_t
src_word(struct src *s, uint32_t i)
{
	uint8_t *p;
	uint32_t v;

	if (!s->ib)
		return gpu_rd32(reg64(A5XX_CP_RB_BASE) + 4ull * (i %
		    (A5XX_RING_SIZE / 4)));
	if (i >= s->len || (p = gpu_mem(s->va + 4ull * i, 4, 0, 1)) == NULL) {
		s->fault = 1;
		return 0;
	}
	memcpy(&v, p, 4);
	return v;
}

static void exec_ib(uint64_t va, uint32_t dwords);

/*
 * One packet at s->pos; 0 and s->pos past it, or -1: the CP stopped (a
 * client's buffer that faulted, or the hang marker).
 */
static int
packet(struct src *s)
{
	static const uint32_t me_init[8] = { 0x2f, 3, 0x20000000, 0, 0, 0, 0,
	    0 };
	uint32_t h, cnt, op, reg = 0, d[MAX_CNT], i;
	uint8_t *p;
	char name[32];

	h = src_word(s, s->pos);
	if (s->fault)
		return -1;
	if ((h >> 28) == 7) {
		cnt = h & 0x7fff;
		op = (h >> 16) & 0x7f;
		if (((h >> 15) & 1) != parity(cnt) ||
		    ((h >> 23) & 1) != parity(op) || (h & 0x0f000000))
			model_fail("type-7 header %08x: bad parity", h);
	} else if ((h >> 28) == 4) {
		cnt = h & 0x7f;
		reg = (h >> 8) & 0x3ffff;
		if (((h >> 7) & 1) != parity(cnt) ||
		    ((h >> 27) & 1) != parity(reg))
			model_fail("type-4 header %08x: bad parity", h);
		op = 0x100;		/* a register write */
	} else
		model_fail("%s word %08x at %u is no PM4 header", s->ib ?
		    "a buffer's" : "ring", h, s->pos);
	if (cnt > MAX_CNT)
		model_fail("a packet of %u words; the model takes %u", cnt,
		    MAX_CNT);
	for (i = 0; i < cnt; i++)
		d[i] = src_word(s, s->pos + 1 + i);
	if (s->fault)
		return -1;
	s->pos += 1 + cnt;
	if (!s->ib)
		s->pos %= A5XX_RING_SIZE / 4;
	cp.packets++;
	if (!cp.me_inited && op != CP_ME_INIT)
		model_fail("the first packet is %02x, not CP_ME_INIT", op);

	if (s->ib) {
		switch (op) {
		case CP_MEM_WRITE:
			if (cnt < 3)
				model_fail("CP_MEM_WRITE of %u words", cnt);
			for (i = 2; i < cnt; i++)
				if ((p = gpu_mem((d[0] | (uint64_t)d[1] << 32) +
				    4ull * (i - 2), 4, 1, 1)) != NULL)
					memcpy(p, &d[i], 4);
			return 0;
		case CP_NOP:
			if (cnt >= 1 && d[0] == CP_MODEL_HANG) {
				cp.hung = 1;
				return -1;
			}
			return 0;
		case CP_WAIT_FOR_IDLE:
			return 0;
		case 0x100:
			if (reg < A5XX_CP_SCRATCH_REG(0) ||
			    reg + cnt > A5XX_CP_SCRATCH_REG(8))
				model_fail("a buffer's PKT4 to %x: the model "
				    "does not know it", reg);
			for (i = 0; i < cnt; i++)
				model_gpu_set(reg + i, d[i]);
			return 0;
		}
		model_fail("a buffer's packet %02x: the model does not know it",
		    op);
	}

	switch (op) {
	case CP_ME_INIT:
		if (cnt != 8 || memcmp(d, me_init, sizeof(me_init)))
			model_fail("CP_ME_INIT not as Linux's for an A512");
		cp.me_inited = 1;
		cp.tz_at_me_init = tz_calls();
		say("me_init");
		break;
	case CP_WHERE_AM_I:
		if (cnt != 2)
			model_fail("WHERE_AM_I of %u words", cnt);
		gpu_wr32(d[0] | (uint64_t)d[1] << 32, s->pos);
		say("where");
		break;
	case CP_SET_SECURE_MODE:
		if (cnt != 1 || d[0] != 0)
			model_fail("CP_SET_SECURE_MODE %u", d[0]);
		if (tz_state() != QCOMSCM_PAS_AUTHED ||
		    tz_calls() <= cp.tz_at_me_init)
			model_fail("CP_SET_SECURE_MODE 0 with no zap "
			    "shader loaded or resumed after ME_INIT");
		model_gpu_set(A5XX_RBBM_SECVID_TRUST_CNTL, 0);
		say("secure0");
		break;
	case CP_SET_PROTECTED_MODE:
		if (cnt != 1 || d[0] > 1)
			model_fail("CP_SET_PROTECTED_MODE of %u words: %u", cnt,
			    d[0]);
		cp.prot = (int)d[0];
		say(cp.prot ? "prot1" : "prot0");
		break;
	case CP_WAIT_FOR_IDLE:
		if (cnt != 0)
			model_fail("CP_WAIT_FOR_IDLE of %u words", cnt);
		say("wfi");
		break;
	case CP_INDIRECT_BUFFER_PFE:
		if (cnt != 3 || d[2] == 0 || d[2] >= (1u << 20))
			model_fail("CP_INDIRECT_BUFFER_PFE of %u words, size %u",
			    cnt, cnt >= 3 ? d[2] : 0);
		say("ib");
		exec_ib(d[0] | (uint64_t)d[1] << 32, d[2]);
		if (cp.hung)
			return -1;
		break;
	case CP_EVENT_WRITE:
		if (cnt != 4 || (d[0] & 0xff) != CACHE_FLUSH_TS)
			model_fail("CP_EVENT_WRITE %08x of %u words",
			    d[0], cnt);
		gpu_wr32(d[1] | (uint64_t)d[2] << 32, d[3]);
		if (d[0] & CP_EVENT_WRITE_IRQ)
			cp.int_status |= A5XX_INT_CP_CACHE_FLUSH_TS;
		snprintf(name, sizeof(name), "fence%u", d[3]);
		say(name);
		break;
	case 0x100:
		if (reg >= A5XX_CP_SCRATCH_REG(0) &&
		    reg + cnt <= A5XX_CP_SCRATCH_REG(8)) {
			for (i = 0; i < cnt; i++)
				model_gpu_set(reg + i, d[i]);
			snprintf(name, sizeof(name), "scratch%u=%u",
			    reg - A5XX_CP_SCRATCH_REG(0), d[0]);
			say(name);
		} else if (reg == A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR &&
		    cnt == 2) {
			/* Under CP_PROTECT (0x800, 64): only out of
			 * protected mode, as Linux writes it. */
			if (cp.prot)
				model_fail("CP_CONTEXT_SWITCH_SAVE_ADDR written "
				    "in protected mode: a CP_PROTECT fault");
			model_gpu_set(reg, d[0]);
			model_gpu_set(reg + 1, d[1]);
			say("save_addr");
		} else if (reg == A5XX_UCHE_CACHE_INVALIDATE_MIN && cnt == 5) {
			if (d[0] | d[1] | d[2] | d[3])
				model_fail("UCHE invalidated in a range, not "
				    "whole");
			if (d[4] != 0x12)
				model_fail("UCHE_CACHE_INVALIDATE %x, not 0x12 "
				    "(fd5's)", d[4]);
			cp.invals++;
			say("uche_inval");
		} else
			model_fail("PKT4 to %x of %u words: not a register "
			    "the ring writes", reg, cnt);
		break;
	default:
		model_fail("packet %02x: the model does not know it", op);
	}
	return 0;
}

/* A client's buffer: its packets, each from memory through the SMMU. */
static void
exec_ib(uint64_t va, uint32_t dwords)
{
	struct src s = { 1, va, 0, dwords, 0 };

	cp.ibs++;
	while (s.pos < s.len && !cp.hung)
		if (packet(&s) != 0)
			break;
}

/* The ring from rptr to what the service wrote, unless the CP is stuck. */
static void
execute(void)
{
	struct src s = { 0, 0, 0, 0, 0 };

	if (!cp.started)
		model_fail("CP_RB_WPTR written before the CP started");
	s.pos = cp.rptr;
	while (s.pos != cp.wptr && !cp.hung) {
		if (packet(&s) != 0)
			break;
		cp.rptr = s.pos;
	}
	if (cp.hung)
		cp.rptr = s.pos;
}

void
cp_model_run(void)
{
	if (cp.started && !cp.hung)
		execute();
}

/* A read of the GPU's window, dword dw: 1 if the CP's own. */
int
cp_model_rd(uint32_t dw, uint32_t *v)
{
	switch (dw) {
	case A5XX_CP_RB_RPTR:
		*v = cp.rptr;
		return 1;
	case A5XX_RBBM_INT_0_STATUS:
		*v = cp.int_status;
		return 1;
	case A5XX_RBBM_STATUS:
		/* Busy (CP_BUSY, bit 31) while words wait or the CP is stuck;
		 * idle otherwise: the ring runs at once. */
		*v = cp.started && (cp.hung || cp.rptr != cp.wptr) ?
		    0x80000000u : 0;
		return 1;
	case A5XX_VBIF_XIN_HALT_CTRL1:
		*v = model_gpu_reg(A5XX_VBIF_XIN_HALT_CTRL0) & 0xf;
		return 1;
	}
	return 0;
}

void
cp_model_wr(uint32_t dw, uint32_t v)
{
	switch (dw) {
	case A5XX_CP_PFP_ME_CNTL:
		if (v == 0)
			start();
		break;
	case A5XX_CP_RB_WPTR:
		cp.wptr = v;
		if (!cp.defer)
			execute();
		break;
	case A5XX_RBBM_INT_CLEAR_CMD:
		cp.int_status &= ~v;
		break;
	case A5XX_VBIF_XIN_HALT_CTRL0:
		if ((v & 0xf) == 0xf && cp.started)
			cp.halted_seen = 1;
		break;
	}
}

/* GX collapses: whatever ran is gone - checked first that it was let go. */
void
cp_model_gx_off(void)
{
	if (cp_model_busy())
		model_fail("GX collapsed under a running CP (no VBIF halt)");
	if (cp.started && model_gpu_reg(A5XX_RBBM_INT_0_MASK) != 0)
		model_fail("GX collapsed with the GPU's interrupts unmasked");
	cp.started = cp.me_inited = cp.halted_seen = 0;
	cp.int_status = 0;
	cp.rptr = cp.wptr = 0;
	cp.hung = 0;
	cp.prot = 1;
}
