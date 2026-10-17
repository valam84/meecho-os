/*
 * Модель SMMU v2 у Adreno 512 и памяти, какой её видит SMMU (model.h).
 *
 * Что модель знает о железе — то, что сказано в port/clover/hw-gpu.md, §4
 * и §13, архитектура SMMUv2 и LPAE stage 1; ничего из кода службы.
 * Обходчик таблиц написан здесь заново, по архитектуре, и с таблицами
 * службы делит только формат.
 *
 *   Память   alloc_contig() даёт две копии страниц: «процессор» (её пишет
 *            служба) и «ОЗУ» (её читает SMMU). Копии сходятся только через
 *            sys_cachectl(CACHE_CLEAN) — построчно, по 64 байта, как
 *            чистит кэш. В ОЗУ новой страницы лежит мусор 0x5b: он похож
 *            на дескриптор таблицы, и обход, который на него наступил,
 *            уходит в память, которой никто не давал, — это провал.
 *   Таблицы  обход по TCR.T0SZ и TTBR0, гранула 4К; таблица — тип 3 на
 *            уровнях 0–2, страница — тип 3 на уровне 3, блок — тип 1 на
 *            уровнях 1–2; AF обязателен; AP[1] — доступ без привилегий,
 *            AP[2] — только чтение; атрибут — байт MAIR по AttrIndx.
 *   TLB      хранит удачные обходы с ASID из TTBR0; операции трансляции
 *            (ATS1*) смотрят в него первым делом. В сценарии stale_tlb в нём
 *            с начала лежит чужая запись для ASID 0 и адреса 16 МиБ — её
 *            снимает только TLBIASID.
 *   Записи   SMR и S2CR: разряды ID и MASK — сколько говорит ID0.NUMSIDB;
 *            SMR, ставший VALID, при S2CR «bypass» — провал (поток GPU шёл
 *            бы мимо SMMU); два VALID SMR на один поток — провал; запись в
 *            запись, которая была занята с начала, — провал. sCR0, глобальные
 *            TLBI, чужие банки — не этой службы: провал.
 *            Банк 0 включается (SCTLR.M) только настроенным: VA64, S1, EPD1,
 *            гранула 4К, TTBR0 в выданной памяти, MAIR.
 *            ATS1* — только 64-битной записью.
 *   Линия    банка 0 — INTID 361 (SPI 329 дерева): поднята, пока в FSR есть
 *            отказ и в SCTLR стоит CFIE. FSRRESTORE пишет FSR, запись FSR
 *            снимает биты (W1C).
 */

#include "adreno.h"
#include "adreno_regs.h"
#include "model.h"

#include <minix/cachectl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMMU_SZ		0x10000
#define CB0		0x8000
#define GR1		0x1000
#define LINE_CB0	361
#define FSR_EF		(1u << 4)

static uint8_t smmu[SMMU_SZ] __attribute__((aligned(8)));
static const struct scenario *sc;
static unsigned writes;
static uint32_t idmask;
static unsigned nsmr;
static uint32_t busy_smr;		/* SMRs valid at the start */
static int atsr_busy, tlb_busy;

static struct {
	int valid;
	unsigned asid;
	uint64_t vpage, pa;
	uint64_t desc;
} tlb[16];

/*
 * Memory: a piece of alloc_contig(), or one page of an mmap(2) region
 * (model_pages_new(): its pages scattered over physical memory, as VM
 * gives them).  cpu is what the processor sees - its caches included -
 * ram what memory holds, sh what a line held when it was last in step
 * with memory: a line whose cpu differs from sh is dirty, and only dirty
 * lines are written back by a clean.
 */
#define NMEM	8192
static struct mreg {
	uint8_t *cpu, *ram, *sh;
	uint64_t pa;
	size_t len;
	int live;
	int seen;		/* the SMMU walked it, or translated into it */
	void *block;		/* the mmap region it is a page of, or NULL */
	size_t blocklen;
} mem[NMEM];
static uint64_t next_pa, next_page_pa;
static unsigned mem_hw;		/* slots ever used since the reset: the searches' bound */
static int serving;		/* frees judged by what the SMMU can reach */

/* Interrupt hooks: bank 0's line (361) and the GPU's (332, cpmodel.c). */
#define NHOOK		2
#define HOOK_ID0	77
static struct {
	int used, vec, bit, enabled;
} hooks[NHOOK];
static int alarm_set;
static unsigned long long alarm_tick;

static uint32_t *
r32(uint32_t off)
{
	return (uint32_t *)(void *)(smmu + off);
}

static uint64_t *
r64(uint32_t off)
{
	return (uint64_t *)(void *)(smmu + off);
}

uint8_t *
smmu_window(void)
{
	return smmu;
}

unsigned
smmu_writes(void)
{
	return writes;
}

/*===========================================================================*
 *    memory                                                                 *
 *===========================================================================*/
static int
mem_slot(void)
{
	unsigned i;

	for (i = 0; i < NMEM; i++)
		if (!mem[i].live) {
			if (i + 1 > mem_hw)
				mem_hw = i + 1;
			return (int)i;
		}
	model_fail("more than %u pieces of memory", NMEM);
	return -1;
}

void *
alloc_contig(size_t len, int flags, phys_bytes *phys)
{
	int i = mem_slot();

	(void)flags;
	len = (len + 4095) & ~(size_t)4095;
	if (posix_memalign((void **)&mem[i].cpu, 4096, len) != 0)
		abort();
	mem[i].ram = malloc(len);
	mem[i].sh = malloc(len);
	if (mem[i].cpu == NULL || mem[i].ram == NULL || mem[i].sh == NULL)
		abort();
	memset(mem[i].cpu, 0xa5, len);
	memset(mem[i].ram, 0x5b, len);
	memcpy(mem[i].sh, mem[i].cpu, len);
	mem[i].pa = next_pa;
	mem[i].len = len;
	mem[i].live = 1;
	mem[i].seen = 0;
	mem[i].block = NULL;
	next_pa += len + 0x3000;	/* never one piece with the next */
	*phys = (phys_bytes)mem[i].pa;
	return mem[i].cpu;
}

/*
 * Can the SMMU still reach [lo, hi): its TLB, or - bank 0 on - the table
 * TTBR0 names, every level of it?  The service's frees while it serves are
 * judged by this, not by "the bank is on": its clients' buffers and tables
 * go while the bank stays on, and must be out of the tables and the TLB
 * first.
 */
static uint64_t
ram_peek(uint64_t pa)
{
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && pa >= mem[i].pa && pa + 8 <= mem[i].pa +
		    mem[i].len)
			return *(uint64_t *)(void *)(mem[i].ram + (pa -
			    mem[i].pa));
	return 0;
}

static int
in_range(uint64_t a, uint64_t n, uint64_t lo, uint64_t hi)
{
	return a < hi && a + n > lo;
}

static const char *
reach_table(uint64_t table, unsigned level, uint64_t lo, uint64_t hi)
{
	uint64_t d;
	unsigned i;
	const char *w;

	if (in_range(table, 4096, lo, hi))
		return "a page table bank 0 walks";
	if (model_ram_pa(table, 4096) == NULL)
		return NULL;	/* a garbage entry: nothing real behind it */
	for (i = 0; i < 512; i++) {
		d = ram_peek(table + 8 * i);
		if (!(d & 1))
			continue;
		if (level < 3 && (d & 3) == 3) {
			if ((w = reach_table(d & 0x0000fffffffff000ull, level + 1,
			    lo, hi)) != NULL)
				return w;
		} else if (in_range(d & 0x0000fffffffff000ull, 4096ull <<
		    (9 * (3 - level)), lo, hi))
			return "a page bank 0's tables map";
	}
	return NULL;
}

static const char *
reachable(uint64_t lo, uint64_t hi)
{
	unsigned i, t0sz, ias;

	for (i = 0; i < 16; i++)
		if (tlb[i].valid && in_range(tlb[i].pa, 4096, lo, hi))
			return "an entry of the SMMU's TLB";
	if (!(*r32(CB0 + SMMU_CB_SCTLR) & SCTLR_M))
		return NULL;
	t0sz = *r32(CB0 + SMMU_CB_TCR) & 0x3f;
	ias = 64 - t0sz;
	return reach_table(*r64(CB0 + SMMU_CB_TTBR0) & 0x0000fffffffff000ull,
	    4 - (ias - 12 + 8) / 9, lo, hi);
}

static void
mem_free(unsigned i)
{
	const char *w;

	if (serving) {
		if ((w = reachable(mem[i].pa, mem[i].pa + mem[i].len)) != NULL)
			model_fail("memory at %llx freed while the SMMU can "
			    "still reach it: %s", (unsigned long long)mem[i].pa,
			    w);
	} else if ((*r32(CB0 + SMMU_CB_SCTLR) & SCTLR_M) && mem[i].seen)
		model_fail("memory freed while bank 0 still translates");
	if (mem[i].block == NULL)
		free(mem[i].cpu);
	free(mem[i].ram);
	free(mem[i].sh);
	mem[i].live = 0;
}

int
free_contig(void *addr, size_t len)
{
	unsigned i;

	len = (len + 4095) & ~(size_t)4095;
	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && mem[i].block == NULL && mem[i].cpu == addr &&
		    mem[i].len == len)
			break;
	if (i == mem_hw)
		model_fail("free_contig of %p+0x%zx, never given", addr, len);
	mem_free(i);
	return 0;
}

/*
 * An mmap(2) region of the service's (MAP_PREALLOC: every page present),
 * its pages scattered: each its own piece, physical addresses going down
 * with gaps between them, so that nothing passes that takes the region for
 * contiguous.  VM zeroes them through the processor's cacheable view: the
 * zeros are dirty in the cache, memory holds whatever it held.
 */
void *
model_pages_new(size_t len)
{
	uint8_t *block;
	size_t k;
	int i;

	len = (len + 4095) & ~(size_t)4095;
	if (len == 0 || posix_memalign((void **)&block, 4096, len) != 0)
		return NULL;
	memset(block, 0, len);
	for (k = 0; k < len / 4096; k++) {
		i = mem_slot();
		mem[i].cpu = block + k * 4096;
		mem[i].ram = malloc(4096);
		mem[i].sh = malloc(4096);
		if (mem[i].ram == NULL || mem[i].sh == NULL)
			abort();
		memset(mem[i].ram, 0x5b, 4096);
		memset(mem[i].sh, 0x5b, 4096);
		mem[i].pa = next_page_pa;
		next_page_pa -= 0x3000;
		mem[i].len = 4096;
		mem[i].live = 1;
		mem[i].seen = 0;
		mem[i].block = block;
		mem[i].blocklen = len;
	}
	return block;
}

int
model_pages_free(void *addr, size_t len)
{
	unsigned i, n = 0;

	len = (len + 4095) & ~(size_t)4095;
	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && mem[i].block == addr) {
			if (mem[i].blocklen != len)
				model_fail("munmap of %p+0x%zx: the region is "
				    "0x%zx", addr, len, mem[i].blocklen);
			mem_free(i);
			n++;
		}
	if (n == 0)
		model_fail("munmap of %p+0x%zx, never mapped", addr, len);
	free(addr);
	return 0;
}

/* The physical address of a byte the service sees, one page at most. */
int
model_umap(const void *cpu, size_t len, uint64_t *pa)
{
	const uint8_t *a = cpu;
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && a >= mem[i].cpu && a + len <= mem[i].cpu +
		    mem[i].len) {
			*pa = mem[i].pa + (uint64_t)(a - mem[i].cpu);
			return 0;
		}
	return EFAULT;
}

/* Is p, len a region of model_pages_new()'s, whole? */
int
model_pages_whole(const void *p, size_t len)
{
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && mem[i].block == p)
			return mem[i].blocklen == len;
	return 0;
}

unsigned
model_pages_live(void)
{
	unsigned i, n = 0;

	for (i = 0; i < mem_hw; i++)
		n += mem[i].live && mem[i].block != NULL;
	return n;
}

void
smmu_model_serving(int on)
{
	serving = on;
}

/*
 * The caches, line by line: a clean writes back the dirty lines, an
 * invalidate makes the processor see memory again - and loses a dirty
 * line, as it would.  The range may cross pieces (an mmap region's
 * pages), each of them the service's.
 */
int
sys_cachectl(int op, void *addr, size_t len)
{
	uint8_t *a = addr;
	size_t off, end, l;
	unsigned i;

	if (op != CACHE_CLEAN && op != CACHE_INVALIDATE &&
	    op != CACHE_CLEAN_INVALIDATE)
		return EINVAL;
	while (len > 0) {
		for (i = 0; i < mem_hw; i++)
			if (mem[i].live && a >= mem[i].cpu &&
			    a < mem[i].cpu + mem[i].len)
				break;
		if (i == mem_hw)
			return EFAULT;
		off = (size_t)(a - mem[i].cpu);
		l = mem[i].len - off < len ? mem[i].len - off : len;
		end = (off + l + 63) & ~(size_t)63;
		if (end > mem[i].len)
			end = mem[i].len;
		for (off &= ~(size_t)63; off < end; off += 64) {
			if (op != CACHE_INVALIDATE &&
			    memcmp(mem[i].cpu + off, mem[i].sh + off, 64) != 0) {
				memcpy(mem[i].ram + off, mem[i].cpu + off, 64);
				memcpy(mem[i].sh + off, mem[i].cpu + off, 64);
			}
			if (op != CACHE_CLEAN) {
				memcpy(mem[i].cpu + off, mem[i].ram + off, 64);
				memcpy(mem[i].sh + off, mem[i].ram + off, 64);
			}
		}
		a += l;
		len -= l;
	}
	return 0;
}

/* What the SMMU reads: memory, never the processor's caches. */
static uint64_t
ram64(uint64_t pa)
{
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && pa >= mem[i].pa &&
		    pa + 8 <= mem[i].pa + mem[i].len) {
			mem[i].seen = 1;
			return *(uint64_t *)(void *)(mem[i].ram + (pa -
			    mem[i].pa));
		}
	model_fail("the SMMU walked to 0x%llx, memory nobody gave it",
	    (unsigned long long)pa);
	return 0;
}

/* Memory the tables lead to; it is the SMMU's from then on (seen). */
static int
known_pa(uint64_t pa)
{
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && pa >= mem[i].pa && pa < mem[i].pa +
		    mem[i].len) {
			mem[i].seen = 1;
			return 1;
		}
	return 0;
}

/* What memory holds - not the processor's caches - of [cpu, cpu + len), if
 * that lies in one piece alloc_contig() gave; its physical start in *pa. */
const uint8_t *
model_ram_of(const void *cpu, size_t len, uint64_t *pa)
{
	const uint8_t *a = cpu;
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && a >= mem[i].cpu &&
		    a + len <= mem[i].cpu + mem[i].len) {
			*pa = mem[i].pa + (uint64_t)(a - mem[i].cpu);
			return mem[i].ram + (a - mem[i].cpu);
		}
	return NULL;
}

unsigned
model_contig_live(void)
{
	unsigned i, n = 0;

	for (i = 0; i < mem_hw; i++)
		n += mem[i].live;
	return n;
}

/*===========================================================================*
 *    the walk                                                               *
 *===========================================================================*/
static unsigned
size_bits(unsigned code)
{
	static const unsigned bits[] = { 32, 36, 40, 42, 44, 48 };

	return code < 6 ? bits[code] : 48;
}

static unsigned
asid_now(void)
{
	uint64_t t = *r64(CB0 + SMMU_CB_TTBR0);

	return (*r32(CB0 + SMMU_CB_TCR2) & TCR2_AS) ? (unsigned)(t >> 48) :
	    (unsigned)(t >> 48) & 0xff;
}

/* The leaf for va, or a fault bit (FSR layout); *desc the leaf. */
static uint32_t
walk(uint64_t va, uint64_t *desc, uint64_t *pa)
{
	uint32_t tcr = *r32(CB0 + SMMU_CB_TCR);
	unsigned t0sz = tcr & 0x3f, ias = 64 - t0sz, levels, level, idx;
	uint64_t table, d, size;

	if (((tcr >> 14) & 3) != 0)
		model_fail("the model walks only a 4K granule (tcr %08x)", tcr);
	if (va >> ias)
		return FSR_TF;
	levels = (ias - 12 + 8) / 9;
	level = 4 - levels;
	table = *r64(CB0 + SMMU_CB_TTBR0) & 0x0000fffffffff000ull;
	for (;;) {
		idx = (unsigned)(va >> (12 + 9 * (3 - level))) & 0x1ff;
		d = ram64(table + 8 * idx);
		if (!(d & 1))
			return FSR_TF;
		if (level < 3 && (d & 3) == 3) {
			table = d & 0x0000fffffffff000ull;
			level++;
			continue;
		}
		if (level == 3 ? (d & 3) != 3 : (level == 0 || (d & 3) != 1))
			return FSR_TF;
		break;
	}
	size = 1ull << (12 + 9 * (3 - level));
	*pa = (d & 0x0000fffffffff000ull & ~(size - 1)) | (va & (size - 1));
	*desc = d;
	if (*pa >> size_bits(*r32(CB0 + SMMU_CB_TCR2) & 0xf))
		return FSR_EF;
	if (!known_pa(*pa))
		model_fail("tables lead 0x%llx to 0x%llx, memory nobody gave",
		    (unsigned long long)va, (unsigned long long)*pa);
	return 0;
}

/* One translation operation, its PAR. */
static uint64_t
translate(uint32_t op, uint64_t va)
{
	uint64_t desc = 0, pa = 0, mair;
	unsigned asid = asid_now(), i, attr;
	int unpriv = op == SMMU_CB_ATS1UR || op == SMMU_CB_ATS1UW;
	int write = op == SMMU_CB_ATS1PW || op == SMMU_CB_ATS1UW;
	uint32_t f = 0;

	if (!(*r32(CB0 + SMMU_CB_SCTLR) & SCTLR_M))
		model_fail("a translation asked of bank 0 while it is off");
	for (i = 0; i < 16; i++)
		if (tlb[i].valid && tlb[i].asid == asid &&
		    tlb[i].vpage == (va >> 12)) {
			desc = tlb[i].desc;
			pa = tlb[i].pa;
			break;
		}
	if (i == 16) {
		if ((f = walk(va, &desc, &pa)) == 0) {
			for (i = 0; i < 16 && tlb[i].valid; i++)
				;
			if (i == 16)
				i = 0;
			tlb[i].valid = 1;
			tlb[i].asid = asid;
			tlb[i].vpage = va >> 12;
			tlb[i].pa = pa & ~0xfffull;
			tlb[i].desc = desc;
		}
	}
	if (f == 0 && !(desc & (1ull << 10)))
		f = 1u << 2;			/* AFF */
	if (f == 0 && ((unpriv && !(desc & (1ull << 6))) ||
	    (write && (desc & (1ull << 7)))))
		f = FSR_PF;
	/* As the tablet: an operation leaves FSR.FORMAT (2, AArch64) behind,
	 * and PAR.ATTR is the outer half of the MAIR byte only. */
	if (sc->smmu_tablet)
		*r32(CB0 + SMMU_CB_FSR) |= 0x400;
	if (f != 0)
		return 1ull | f;
	attr = (unsigned)(desc >> 2) & 7;
	mair = *r32(CB0 + SMMU_CB_MAIR0) |
	    (uint64_t)*r32(CB0 + SMMU_CB_MAIR1) << 32;
	mair = (mair >> (8 * attr)) & (sc->smmu_tablet ? 0xf0 : 0xff);
	return (pa & ~0xfffull) | (mair << 56) | (((desc >> 8) & 3) << 7);
}

/*===========================================================================*
 *    registers                                                              *
 *===========================================================================*/
static int
line_up(int vec)
{
	if (vec != LINE_CB0)
		return cp_model_line_up(vec);
	return !sc->no_line &&
	    (*r32(CB0 + SMMU_CB_SCTLR) & SCTLR_CFIE) &&
	    (*r32(CB0 + SMMU_CB_FSR) & FSR_FAULT);
}

/* Stream 0 (the GPU) goes to bank 0: a valid SMR matches it and its S2CR
 * translates there. */
static int
route_ok(void)
{
	uint32_t smr, s2;
	unsigned i;

	for (i = 0; i < nsmr; i++) {
		smr = *r32(SMMU_SMR(i));
		s2 = *r32(SMMU_S2CR(i));
		if ((smr & SMR_VALID) && (SMR_ID(smr) & ~SMR_MASK(smr)) == 0)
			return S2CR_TYPE(s2) == 0 && S2CR_CBNDX(s2) == 0;
	}
	return 0;
}

/*
 * An access of the GPU's to va, as the SMMU would make it: the route, bank
 * 0, the TLB, then the tables; a fault lands in FSR and FAR.  0 and the
 * physical address, or -1.
 */
int
smmu_gpu_pa(uint64_t va, int write, uint64_t *pa)
{
	uint64_t desc = 0, p = 0;
	unsigned asid = asid_now(), i;
	uint32_t f = 0;

	if (!route_ok())
		model_fail("the GPU reached for %llx with its stream routed "
		    "nowhere (an unmatched stream faults)",
		    (unsigned long long)va);
	if (!(*r32(CB0 + SMMU_CB_SCTLR) & SCTLR_M))
		model_fail("the GPU reached for %llx through bank 0 while it "
		    "is off", (unsigned long long)va);
	for (i = 0; i < 16; i++)
		if (tlb[i].valid && tlb[i].asid == asid &&
		    tlb[i].vpage == (va >> 12)) {
			desc = tlb[i].desc;
			p = tlb[i].pa;
			break;
		}
	if (i == 16) {
		if ((f = walk(va, &desc, &p)) == 0) {
			for (i = 0; i < 16 && tlb[i].valid; i++)
				;
			if (i == 16)
				i = 0;
			tlb[i].valid = 1;
			tlb[i].asid = asid;
			tlb[i].vpage = va >> 12;
			tlb[i].pa = p & ~0xfffull;
			tlb[i].desc = desc;
		}
	}
	if (f == 0 && !(desc & (1ull << 10)))
		f = 1u << 2;
	if (f == 0 && write && (desc & (1ull << 7)))
		f = FSR_PF;
	if (f != 0) {
		*r32(CB0 + SMMU_CB_FSR) |= f;
		*r64(CB0 + SMMU_CB_FAR) = va;
		*r32(CB0 + SMMU_CB_FSYNR0) = write ? 1u << 4 : 0;	/* WNR */
		return -1;
	}
	*pa = (p & ~0xfffull) | (va & 0xfff);
	return 0;
}

/* Memory at a physical address, as the GPU sees it (never the caches). */
uint8_t *
model_ram_pa(uint64_t pa, size_t n)
{
	unsigned i;

	for (i = 0; i < mem_hw; i++)
		if (mem[i].live && pa >= mem[i].pa &&
		    pa + n <= mem[i].pa + mem[i].len) {
			mem[i].seen = 1;
			return mem[i].ram + (pa - mem[i].pa);
		}
	return NULL;
}

static void
check_routes(unsigned i)
{
	uint32_t a = *r32(SMMU_SMR(i)), b;
	unsigned j;

	if (!(a & SMR_VALID))
		return;
	if (S2CR_TYPE(*r32(SMMU_S2CR(i))) == 1)
		model_fail("SMR %u valid while its S2CR bypasses the SMMU", i);
	for (j = 0; j < nsmr; j++) {
		b = *r32(SMMU_SMR(j));
		if (j != i && (b & SMR_VALID) && ((SMR_ID(a) ^ SMR_ID(b)) &
		    ~(SMR_MASK(a) | SMR_MASK(b)) & 0x7fff) == 0)
			model_fail("SMR %u and %u match one stream", i, j);
	}
}

static void
enable_check(void)
{
	uint32_t tcr = *r32(CB0 + SMMU_CB_TCR);
	uint64_t ttbr = *r64(CB0 + SMMU_CB_TTBR0) & 0x0000fffffffff000ull;
	const char *why = NULL;

	if (!(*r32(GR1 + SMMU_CBA2R(0)) & CBA2R_VA64))
		why = "CBA2R without VA64";
	else if (((*r32(GR1 + SMMU_CBAR(0)) >> 16) & 3) != 1)
		why = "CBAR is not stage 1 with stage 2 bypassed";
	else if (!(tcr & TCR_EPD1) || ((tcr >> 14) & 3) != 0 ||
	    (tcr & 0x3f) < 16 || (tcr & 0x3f) > 39)
		why = "TCR";
	else if (!(*r32(CB0 + SMMU_CB_TCR2) & TCR2_AS))
		why = "TCR2 without 16-bit ASIDs";
	else if (ttbr == 0 || !known_pa(ttbr))
		why = "TTBR0 not in memory this driver was given";
	else if (*r32(CB0 + SMMU_CB_MAIR0) == 0)
		why = "MAIR0";
	if (why != NULL)
		model_fail("bank 0 enabled before it was set up: %s", why);
}

uint32_t
smmu_rd(uint32_t off)
{
	if (off == CB0 + SMMU_CB_ATSR) {
		if (atsr_busy > 0) {
			atsr_busy--;
			return ATSR_ACTIVE;
		}
		return 0;
	}
	if (off == CB0 + SMMU_CB_TLBSTATUS) {
		if (tlb_busy > 0) {
			tlb_busy--;
			return TLBSTATUS_SACTIVE;
		}
		return 0;
	}
	return *r32(off);
}

uint64_t
smmu_rd64(uint32_t off)
{
	if (off & 7)
		model_fail("smmu +0x%x: a 64-bit read off its alignment", off);
	return *r64(off);
}

void
smmu_wr(uint32_t off, uint32_t v)
{
	unsigned i;

	writes++;
	if (off < GR1) {
		for (i = 0; i < nsmr; i++) {
			if (off == SMMU_SMR(i)) {
				if (busy_smr & (1u << i))
					model_fail("SMR %u was in use", i);
				if (sc->smr_ro)
					return;
				*r32(off) = (v & SMR_VALID) |
				    (((v >> 16) & idmask) << 16) | (v & idmask);
				check_routes(i);
				if (cp_model_busy() && !route_ok())
					model_fail("the GPU's stream taken "
					    "back under a running CP");
				return;
			}
			if (off == SMMU_S2CR(i)) {
				if (busy_smr & (1u << i))
					model_fail("S2CR %u belongs to a live "
					    "stream", i);
				if (sc->smr_ro)
					return;
				/* The tablet's hypervisor: fault and bypass
				 * types ignored (Linux's bypass quirk). */
				if (sc->s2cr_ro && S2CR_TYPE(v) != 0)
					return;
				/* An S2CR that translates through a bank that
				 * is not a stage-1 one yet: the tablet stopped
				 * here (hw-gpu.md, §15). */
				if (S2CR_TYPE(v) == 0 && (S2CR_CBNDX(v) != 0 ||
				    ((*r32(GR1 + SMMU_CBAR(0)) >> 16) & 3) != 1))
					model_fail("S2CR %u translates through "
					    "bank %u, not set up (CBAR %08x)", i,
					    S2CR_CBNDX(v),
					    *r32(GR1 + SMMU_CBAR(0)));
				*r32(off) = v;
				check_routes(i);
				if (cp_model_busy() && !route_ok())
					model_fail("the GPU's stream taken "
					    "back under a running CP");
				return;
			}
		}
		model_fail("write of smmu global +0x%x (%08x): not this "
		    "driver's", off, v);
	}
	if (off == GR1 + SMMU_CBAR(0) || off == GR1 + SMMU_CBA2R(0)) {
		for (i = 0; off == GR1 + SMMU_CBAR(0) && ((v >> 16) & 3) != 1 &&
		    i < nsmr; i++)
			if ((*r32(SMMU_S2CR(i)) & S2CR_FIELDS) == 0)
				model_fail("bank 0 made stage 2 again while "
				    "S2CR %u names it", i);
		*r32(off) = v;
		return;
	}
	if (off < CB0 || off >= CB0 + 0x1000)
		model_fail("write of smmu +0x%x: not bank 0's", off);
	switch (off - CB0) {
	case SMMU_CB_SCTLR:
		if ((v & SCTLR_M) && !(*r32(off) & SCTLR_M))
			enable_check();
		*r32(off) = v;
		return;
	case SMMU_CB_TCR2: case SMMU_CB_TCR: case SMMU_CB_MAIR0:
	case SMMU_CB_MAIR1: case SMMU_CB_TTBR0: case SMMU_CB_TTBR0 + 4:
	case SMMU_CB_TTBR1: case SMMU_CB_TTBR1 + 4:
		*r32(off) = v;
		return;
	case SMMU_CB_FSR:
		*r32(off) &= ~v;
		return;
	case SMMU_CB_FSRRESTORE:
		*r32(CB0 + SMMU_CB_FSR) = v;
		return;
	case SMMU_CB_TLBIASID:
		for (i = 0; i < 16; i++)
			if (tlb[i].asid == (v & 0xffff))
				tlb[i].valid = 0;
		return;
	case SMMU_CB_TLBSYNC:
		tlb_busy = 2;
		return;
	case SMMU_CB_ATS1PR: case SMMU_CB_ATS1PW: case SMMU_CB_ATS1UR:
	case SMMU_CB_ATS1UW: case SMMU_CB_ATS1PR + 4: case SMMU_CB_ATS1PW + 4:
	case SMMU_CB_ATS1UR + 4: case SMMU_CB_ATS1UW + 4:
		model_fail("ATS1 at +0x%x written by halves", off - CB0);
	}
	model_fail("write of bank 0 +0x%x (%08x): the model does not know it",
	    off - CB0, v);
}

void
smmu_wr64(uint32_t off, uint64_t v)
{
	if (off & 7)
		model_fail("smmu +0x%x: a 64-bit write off its alignment", off);
	switch (off - CB0) {
	case SMMU_CB_TTBR0:
	case SMMU_CB_TTBR1:
		writes++;
		*r64(off) = v;
		return;
	case SMMU_CB_ATS1PR: case SMMU_CB_ATS1PW: case SMMU_CB_ATS1UR:
	case SMMU_CB_ATS1UW:
		writes++;
		*r64(CB0 + SMMU_CB_PAR) = translate(off - CB0, v &
		    ~0xfffull);
		atsr_busy = 2;
		return;
	}
	model_fail("64-bit write of smmu +0x%x: the model does not know it",
	    off);
}

/*===========================================================================*
 *    the kernel: interrupts and alarms                                      *
 *===========================================================================*/
int
sys_irqctl(int request, int irq_vec, int policy, int *irq_hook_id)
{
	int k;

	(void)policy;
	switch (request) {
	case IRQ_SETPOLICY:
		for (k = 0; k < NHOOK; k++) {
			if (hooks[k].used && hooks[k].vec == irq_vec)
				model_fail("line %d hooked twice", irq_vec);
			if (hooks[k].used && hooks[k].bit == *irq_hook_id)
				model_fail("two hooks with notify bit %d",
				    *irq_hook_id);
		}
		for (k = 0; k < NHOOK && hooks[k].used; k++)
			;
		if (k == NHOOK)
			model_fail("more interrupt hooks than the model has");
		hooks[k].used = 1;
		hooks[k].vec = irq_vec;
		hooks[k].bit = *irq_hook_id;
		hooks[k].enabled = 1;
		*irq_hook_id = HOOK_ID0 + k;
		return 0;
	case IRQ_ENABLE:
	case IRQ_DISABLE:
	case IRQ_RMPOLICY:
		k = *irq_hook_id - HOOK_ID0;
		if (k < 0 || k >= NHOOK || !hooks[k].used)
			model_fail("irqctl %d on a hook never set", request);
		if (request == IRQ_RMPOLICY)
			hooks[k].used = 0;
		else
			hooks[k].enabled = request == IRQ_ENABLE;
		return 0;
	}
	return EINVAL;
}

int
sys_setalarm2(clock_t exp_time, int abs_time, clock_t *time_left,
	clock_t *uptime)
{
	unsigned long long now = model_now_us() / 10000;

	(void)abs_time;
	if (time_left != NULL)
		*time_left = 0;
	if (uptime != NULL)
		*uptime = (clock_t)now;
	alarm_set = exp_time != 0;
	alarm_tick = now + (unsigned long long)exp_time;
	return 0;
}

unsigned
sys_hz(void)
{
	return 100;
}

/*
 * The kernel's side of a serving driver's hooks (msmtest.c delivers them):
 * every enabled hook whose line is up, its notify bit in the mask, and the
 * hook disabled until the driver enables it again.
 */
uint64_t
smmu_model_lines_of(uint64_t want)
{
	uint64_t mask = 0;
	int k;

	for (k = 0; k < NHOOK; k++)
		if (hooks[k].used && hooks[k].enabled &&
		    (want & (1ull << hooks[k].bit)) && line_up(hooks[k].vec)) {
			hooks[k].enabled = 0;
			mask |= 1ull << hooks[k].bit;
		}
	return mask;
}

uint64_t
smmu_model_lines(void)
{
	return smmu_model_lines_of(~0ull);
}

int
ipc_receive(endpoint_t src, message *m, int *status)
{
	int k;

	(void)src;
	memset(m, 0, sizeof(*m));
	*status = STUB_NOTIFY_STATUS;
	m->m_type = NOTIFY_MESSAGE;
	for (k = 0; k < NHOOK; k++)
		if (hooks[k].used && hooks[k].enabled && line_up(hooks[k].vec)) {
			hooks[k].enabled = 0;
			m->m_source = HARDWARE;
			m->m_notify.interrupts = 1ull << hooks[k].bit;
			return 0;
		}
	if (alarm_set) {
		unsigned long long at = alarm_tick * 10000;

		if (at > model_now_us())
			micro_delay((unsigned long)(at - model_now_us()));
		alarm_set = 0;
		m->m_source = CLOCK;
		m->m_notify.timestamp = (clock_t)alarm_tick;
		return 0;
	}
	model_fail("a wait for a message that nothing will send");
	return 0;
}

/*===========================================================================*
 *    start and end                                                          *
 *===========================================================================*/
static struct {
	uint32_t gr0[0x1000 / 4], gr1[2], cb0[0x100 / 4];
	uint64_t ttbr0, ttbr1;
} start;

static void
snap(uint32_t *gr0, uint32_t *gr1, uint32_t *cb)
{
	memcpy(gr0, smmu, 0x1000);
	gr1[0] = *r32(GR1 + SMMU_CBAR(0));
	gr1[1] = *r32(GR1 + SMMU_CBA2R(0));
	memcpy(cb, smmu + CB0, 0x100);
}

void
smmu_model_reset(const struct scenario *s)
{
	unsigned i;

	sc = s;
	writes = 0;
	atsr_busy = tlb_busy = 0;
	memset(smmu, 0, sizeof(smmu));
	memset(tlb, 0, sizeof(tlb));
	memset(hooks, 0, sizeof(hooks));
	alarm_set = 0;
	for (i = 0; i < NMEM; i++)
		if (mem[i].live && mem[i].block != NULL &&
		    mem[i].cpu == mem[i].block)
			free(mem[i].block);
	for (i = 0; i < NMEM; i++)
		if (mem[i].live) {
			if (mem[i].block == NULL)
				free(mem[i].cpu);
			free(mem[i].ram);
			free(mem[i].sh);
			mem[i].live = 0;
		}
	mem_hw = 0;
	next_pa = 0x90000000ull;
	next_page_pa = 0xc0000000ull;
	serving = 0;

	if (!s->smmu_tablet) {
		/* 5 banks, 16 SMRs, 4K pages, 36-bit IPA/PA, 48-bit VA;
		 * SID 0 routed by SMR 0 to bank 0. */
		*r32(SMMU_SCR0) = 0x1;
		*r32(SMMU_ID0) = 0x48000010;
		*r32(SMMU_ID1) = 0x20000005;
		*r32(SMMU_ID2) = 0x00000511;
		*r32(SMMU_SMR(0)) = SMR_VALID;
		*r32(SMMU_S2CR(0)) = 0;
		*r32(GR1 + SMMU_CBAR(0)) = 0x00010000;
		nsmr = 16;
		idmask = 0x7fff;
		busy_smr = 1;
	} else {
		/* As the tablet answered Q9, Q10 and Q13 (hw-gpu.md, §13,
		 * §14): 5 banks, 3 SMRs - all free, each S2CR "fault, bank
		 * 0xff" - all 15 bits of ID and MASK writable, translation
		 * operations, unmatched streams fault. */
		*r32(SMMU_SCR0) = 0x00200626;
		*r32(SMMU_ID0) = 0x48016603;
		*r32(SMMU_ID1) = 0x20008005;
		*r32(SMMU_ID2) = 0x00007511;
		*r32(SMMU_S2CR(0)) = 0x000200ff;
		*r32(SMMU_S2CR(1)) = 0x000200ff;
		*r32(SMMU_S2CR(2)) = 0x000200ff;
		nsmr = 3;
		idmask = 0x7fff;
		busy_smr = 0;
		if (s->bypass_free) {
			/* Made up, the worst a machine might hold: SMR 0
			 * routes stream 2 to a bank of TrustZone's, the free
			 * SMR 1 has an S2CR that would bypass the SMMU. */
			*r32(SMMU_SMR(0)) = SMR_VALID | 2;
			*r32(SMMU_S2CR(0)) = 2;
			*r32(SMMU_S2CR(1)) = 0x00010000;
			busy_smr = 1;
		}
		if (s->cb0_busy)
			*r32(CB0 + SMMU_CB_SCTLR) = SCTLR_M;
		if (s->stale_tlb) {
			tlb[0].valid = 1;
			tlb[0].asid = 0;
			tlb[0].vpage = 0x1000000 >> 12;
			tlb[0].pa = 0x0dead000;
			tlb[0].desc = 0x0dead000 | 0x743;
		}
	}
	snap(start.gr0, start.gr1, start.cb0);
}

int
smmu_model_same(char *why, size_t size)
{
	uint32_t gr0[0x1000 / 4], gr1[2], cb[0x100 / 4];
	unsigned i;

	for (i = 0; i < NHOOK; i++)
		if (hooks[i].used) {
			snprintf(why, size, "interrupt hook on line %d left set",
			    hooks[i].vec);
			return 0;
		}
	if ((i = model_contig_live()) != 0) {
		snprintf(why, size, "%u pieces of memory never freed", i);
		return 0;
	}
	snap(gr0, gr1, cb);
	for (i = 0; i < 0x1000 / 4; i++)
		if (gr0[i] != start.gr0[i]) {
			snprintf(why, size, "smmu +0x%x %08x, was %08x", i * 4,
			    gr0[i], start.gr0[i]);
			return 0;
		}
	for (i = 0; i < 2; i++)
		if (gr1[i] != start.gr1[i]) {
			snprintf(why, size, "smmu %s(0) %08x, was %08x",
			    i == 0 ? "CBAR" : "CBA2R", gr1[i], start.gr1[i]);
			return 0;
		}
	for (i = 0; i < 0x100 / 4; i++)
		if (i * 4 != SMMU_CB_PAR && i * 4 != SMMU_CB_PAR + 4 &&
		    cb[i] != start.cb0[i]) {
			snprintf(why, size, "bank 0 +0x%x %08x, was %08x",
			    i * 4, cb[i], start.cb0[i]);
			return 0;
		}
	return 1;
}
