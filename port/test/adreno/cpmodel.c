/*
 * Модель командного процессора Adreno 5xx для стенда (стадия cp,
 * port/clover/hw-gpu.md, §6, §7 и §17).  Что модель знает о железе — то,
 * что там сказано, и формат PM4 (adreno_pm4.xml); ничего из кода службы.
 *
 *   Старт   запись 0 в CP_PFP_ME_CNTL.  К этому моменту: адреса микрокода
 *           ME и PFP ведут (через SMMU, как пошёл бы GPU) в память, где
 *           лежит ровно микрокод — файл без первого слова, — и в ОЗУ, не в
 *           кэше; кольцо — CP_RB_CNTL 0x0800020c и читаемая база; маска
 *           прерываний — A5XX_INT_MASK.  Иначе — провал.
 *   Кольцо  исполняется по записи CP_RB_WPTR, от rptr до wptr, словами из
 *           ОЗУ через SMMU (маршрут SID 0, банк 0, TLB, таблицы).  Заголовок
 *           с неверной чётностью, незнакомый пакет, первый пакет не
 *           CP_ME_INIT — провал.  ME_INIT сверяется со значениями Linux для
 *           A512.  WHERE_AM_I пишет rptr по адресу; CP_EVENT_WRITE
 *           CACHE_FLUSH_TS — seqno по адресу, бит 20 RBBM_INT_0_STATUS и,
 *           если он в маске, линию 332.  PKT4 пишет регистры CP_SCRATCH.
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

static struct {
	int started, me_inited, halted_seen;
	uint32_t rptr;			/* dwords */
	uint32_t int_status;
	unsigned tz_at_me_init;		/* tz_calls() then */
	unsigned packets;
	const uint8_t *pm4, *pfp;	/* the files, as the stand made them */
	size_t pm4_len, pfp_len;
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
	cp.rptr = cp.int_status = 0;
	cp.tz_at_me_init = 0;
	cp.packets = 0;
	/* Bits of no meaning in the registers hw_init() changes a bit of:
	 * what it did not mean to change must still be there after. */
	model_gpu_set(A5XX_RB_DBG_ECO_CNTL, 0x00000800);
	model_gpu_set(A5XX_VPC_DBG_ECO_CNTL, 0x00000002);
	model_gpu_set(A5XX_HLSQ_DBG_ECO_CNTL, 0x00040001);
}

int cp_model_started(void) { return cp.started; }
unsigned cp_model_packets(void) { return cp.packets; }

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

/* A GPU access: through the SMMU, then memory - never the caches. */
static uint8_t *
gpu_mem(uint64_t va, size_t n, int write)
{
	uint64_t pa;
	uint8_t *p;

	if (smmu_gpu_pa(va, write, &pa) != 0)
		model_fail("the GPU's %s of %llx faults in the SMMU",
		    write ? "write" : "read", (unsigned long long)va);
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

	memcpy(&v, gpu_mem(va, 4, 0), 4);
	return v;
}

static void
gpu_wr32(uint64_t va, uint32_t v)
{
	memcpy(gpu_mem(va, 4, 1), &v, 4);
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
}

static uint32_t
ring_dw(uint32_t i)
{
	return gpu_rd32(reg64(A5XX_CP_RB_BASE) + 4ull * (i % (A5XX_RING_SIZE /
	    4)));
}

static void
execute(uint32_t wptr)
{
	static const uint32_t me_init[8] = { 0x2f, 3, 0x20000000, 0, 0, 0, 0,
	    0 };
	uint32_t h, cnt, op, reg = 0, d[8], i;

	if (!cp.started)
		model_fail("CP_RB_WPTR written before the CP started");
	while (cp.rptr != wptr) {
		h = ring_dw(cp.rptr);
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
			model_fail("ring word %08x at %u is no PM4 header", h,
			    cp.rptr);
		if (cnt > 8)
			model_fail("a packet of %u words; the model takes 8",
			    cnt);
		for (i = 0; i < cnt; i++)
			d[i] = ring_dw(cp.rptr + 1 + i);
		cp.rptr = (cp.rptr + 1 + cnt) % (A5XX_RING_SIZE / 4);
		cp.packets++;
		if (!cp.me_inited && op != CP_ME_INIT)
			model_fail("the first packet is %02x, not CP_ME_INIT",
			    op);

		switch (op) {
		case CP_ME_INIT:
			if (cnt != 8 || memcmp(d, me_init, sizeof(me_init)))
				model_fail("CP_ME_INIT not as Linux's for an "
				    "A512");
			cp.me_inited = 1;
			cp.tz_at_me_init = tz_calls();
			break;
		case CP_WHERE_AM_I:
			if (cnt != 2)
				model_fail("WHERE_AM_I of %u words", cnt);
			gpu_wr32(d[0] | (uint64_t)d[1] << 32, cp.rptr);
			break;
		case CP_SET_SECURE_MODE:
			if (cnt != 1 || d[0] != 0)
				model_fail("CP_SET_SECURE_MODE %u", d[0]);
			if (tz_state() != QCOMSCM_PAS_AUTHED ||
			    tz_calls() <= cp.tz_at_me_init)
				model_fail("CP_SET_SECURE_MODE 0 with no zap "
				    "shader loaded or resumed after ME_INIT");
			model_gpu_set(A5XX_RBBM_SECVID_TRUST_CNTL, 0);
			break;
		case CP_EVENT_WRITE:
			if (cnt != 4 || (d[0] & 0xff) != CACHE_FLUSH_TS)
				model_fail("CP_EVENT_WRITE %08x of %u words",
				    d[0], cnt);
			gpu_wr32(d[1] | (uint64_t)d[2] << 32, d[3]);
			if (d[0] & CP_EVENT_WRITE_IRQ)
				cp.int_status |= A5XX_INT_CP_CACHE_FLUSH_TS;
			break;
		case 0x100:
			if (reg < A5XX_CP_SCRATCH_REG(0) ||
			    reg + cnt > A5XX_CP_SCRATCH_REG(8))
				model_fail("PKT4 to %x: not a scratch register",
				    reg);
			for (i = 0; i < cnt; i++)
				model_gpu_set(reg + i, d[i]);
			break;
		default:
			model_fail("packet %02x: the model does not know it",
			    op);
		}
	}
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
		*v = 0;		/* idle: the ring runs at once */
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
		execute(v);
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
	cp.rptr = 0;
}
