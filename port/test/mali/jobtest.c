/*
 * Стенд второй половины службы mali: MMU ускорителя и цепочка заданий,
 * без железа.
 *
 * Собирает настоящий mali_job.c и подставляет ему модель ускорителя,
 * которая знает о формате всё сама, независимо от службы:
 *
 *   - память некогерентна: у процессора свой вид (то, что пишет служба),
 *     у ОЗУ - свой (то, что видит ускоритель); сводит их только
 *     sys_cachectl(): CLEAN несёт вид процессора в ОЗУ, INVALIDATE - ОЗУ в
 *     вид процессора.  Забытая чистка - ускоритель видит нули; забытый
 *     сброс перед чтением - служба видит свои же нули;
 *   - проход по таблицам - по правилам io-pgtable ARM_MALI_LPAE: четыре
 *     уровня всегда, таблица - тип 3, лист - тип 1 на любом уровне (тип 3
 *     на последнем - это формат процессора, не Mali), права на чтение и
 *     запись (биты 7:6 = 3), разделяемость «внешняя» (9:8 = 2);
 *   - дескриптор - по раскладке, которой зонд port/cb2-gpu/jobprobe.py
 *     прогнал на плате цепочку из 64 заданий через panfrost
 *     (ref-jobs.txt): управляющее слово с байта 16, следующее - с 24,
 *     адрес - 32, тип значения - 40, значение - 48;
 *   - слот и адресное пространство сверяются с регистрами, которые
 *     panfrost оставил на плате: TRANSTAB ...007, MEMATTR 0x888d88,
 *     TRANSCFG 0, JS_CONFIG 0x7b00 (младшие 16 бит);
 *   - отказ - как у железа в port/cb2-gpu/ref-fault.txt: проход, не
 *     нашедший страницы, поднимает бит адресного пространства в
 *     MMU_INT_RAWSTAT, пишет FAULTSTATUS и FAULTADDRESS, и задание стоит;
 *     падает оно (JOB_BUS_FAULT), только когда адресное пространство
 *     выключено (UPDATE при TRANSTAB без таблиц), и выключение снова
 *     поднимает бит отказа - так на плате.  Линии доставляются, как это делает
 *     irq_handle() в mali.c: STAT = RAWSTAT & MASK обработчику, потом
 *     CLEAR.  Вариант fail_at_once - железо, роняющее задание сразу, с
 *     обеими линиями в одном уведомлении (задание раньше MMU).
 *
 *	sh run.sh       (собирает и этот стенд)
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>
#include <minix/cachectl.h>
#include <minix/log.h>

#include "mali.h"
#include "malireg.h"

struct mali mali;
struct log mali_log = { "mali", LEVEL_INFO, NULL };

static unsigned checks, failures;
static int verbose;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  ОТКАЗ %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/* ---- журнал ---------------------------------------------------------- */

static char logbuf[65536];
static size_t loglen;
static unsigned warnings;

void
shim_log(struct log *l, int level, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (level > l->log_level)
		return;
	if (level == LEVEL_WARN)
		warnings++;
	va_start(ap, fmt);
	n = vsnprintf(logbuf + loglen, sizeof(logbuf) - loglen, fmt, ap);
	va_end(ap);
	if (verbose)
		printf("    | %s", logbuf + loglen);
	if (n > 0 && loglen + (size_t)n < sizeof(logbuf))
		loglen += (size_t)n;
}

static int
said(const char *what)
{
	return strstr(logbuf, what) != NULL;
}

/* ---- память: вид процессора и вид ОЗУ --------------------------------- */

#define ARENA_PAGES	64
#define PAGE		4096
#define PHYS_BASE	0x6a000000ul	/* выдуманный физический адрес арены */
#define LINE		64

static unsigned char *cpu_view, *ram_view;
static size_t arena_used;
static void *freed_addr;
static size_t freed_len;

void *
alloc_contig(size_t len, int flags, phys_bytes *phys)
{
	void *v;

	(void)flags;
	len = (len + PAGE - 1) & ~(size_t)(PAGE - 1);
	if (arena_used + len > ARENA_PAGES * PAGE)
		return NULL;
	v = cpu_view + arena_used;
	*phys = PHYS_BASE + arena_used;
	arena_used += len;
	/* Свежая память - мусор и в кэше, и в ОЗУ: обнуляет служба. */
	memset(v, 0xa5, len);
	memset(ram_view + (*phys - PHYS_BASE), 0x5a, len);
	return v;
}

int
free_contig(void *addr, size_t len)
{
	freed_addr = addr;
	freed_len = len;
	return 0;
}

/* Что делалось с кэшем: для проверки порядка. */
static struct { int op; size_t off, len; } cops[64];
static unsigned ncops;

int
sys_cachectl(int op, void *addr, size_t len)
{
	size_t off = (unsigned char *)addr - cpu_view, lo, hi;

	if ((unsigned char *)addr < cpu_view || off + len > ARENA_PAGES * PAGE)
		return EFAULT;
	if (ncops < sizeof(cops) / sizeof(cops[0])) {
		cops[ncops].op = op;
		cops[ncops].off = off;
		cops[ncops].len = len;
		ncops++;
	}
	/* Целыми строками, как ядро. */
	lo = off & ~(size_t)(LINE - 1);
	hi = (off + len + LINE - 1) & ~(size_t)(LINE - 1);
	if (op == CACHE_CLEAN || op == CACHE_CLEAN_INVALIDATE)
		memcpy(ram_view + lo, cpu_view + lo, hi - lo);
	if (op == CACHE_INVALIDATE || op == CACHE_CLEAN_INVALIDATE)
		memcpy(cpu_view + lo, ram_view + lo, hi - lo);
	return 0;
}

/* Была ли операция op (или CLEAN_INVALIDATE) над всем [off, off+len). */
static int
covered(int op, size_t off, size_t len, unsigned from)
{
	unsigned i;

	for (i = from; i < ncops; i++)
		if ((cops[i].op == op || cops[i].op == CACHE_CLEAN_INVALIDATE) &&
		    cops[i].off <= off && cops[i].off + cops[i].len >= off + len)
			return 1;
	return 0;
}

static unsigned char *
ram(uint64_t pa, size_t len)
{
	if (pa < PHYS_BASE || pa + len > PHYS_BASE + arena_used)
		return NULL;
	return ram_view + (pa - PHYS_BASE);
}

/* ---- часы, будильник ------------------------------------------------- */

static u64_t clock_now;
static long alarm_ticks = -1;

void read_frclock_64(u64_t *f) { *f = clock_now += 7; }
u64_t delta_frclock_64(u64_t b, u64_t c) { return c - b; }
u32_t frclock_64_to_micros(u64_t t) { return (u32_t)t; }
u32_t sys_hz(void) { return 100; }
int sys_setalarm(long t, int abs) { (void)abs; alarm_ticks = t; return 0; }
int micro_delay(u32_t usec) { (void)usec; return 0; }
void *vm_map_phys(endpoint_t w, void *p, size_t l) { (void)w; (void)p;
	(void)l; return NULL; }

/* ---- модель ускорителя ----------------------------------------------- */

static uint32_t gpu[4096];

#define G(off)	gpu[(off) / 4]

static char why[256];

/*
 * Отказ, каким его доложило железо платы (ref-fault.txt): FAULTSTATUS
 * 0x7c1003c3 - TRANSLATION_FAULT_3 (уровень прохода с нуля, пустая запись
 * последнего уровня), запись, источник 0x7c10; адрес - ровно записанный;
 * задание падает как JOB_BUS_FAULT (0x48), его заголовок не тронут.
 */
#define REF_TRANSLATION(lvl)	(0xc0u + (lvl))
#define REF_PERMISSION(lvl)	(0xc8u + (lvl))
#define REF_SOURCE		0x7c10u
#define REF_JS_FAULT		0x48u	/* JS_STATUS упавшего задания */

static uint32_t fault_exc;	/* walk(): что доложила бы MMU, 0 - не она */
static int stalled;		/* задание стоит на отказе */
static int fail_at_once;	/* вариант железа: задание падает сразу */

/*
 * Проход по таблицам по правилам Mali LPAE.  0 и физический адрес, или -1
 * и причина в why; в fault_exc - исключение MMU, если отказ её, или 0,
 * если это претензия модели к службе (не отказ железа).
 */
static int
walk(uint64_t transtab, uint64_t va, int write, uint64_t *pa)
{
	uint64_t table, e;
	unsigned char *p;
	int lvl;

	fault_exc = 0;
	if ((transtab & 3) != 3) {
		snprintf(why, sizeof(why), "TRANSTAB adrmode %u, not table",
		    (unsigned)(transtab & 3));
		fault_exc = REF_TRANSLATION(0);
		return -1;
	}
	table = transtab & 0x000ffffffffff000ull;
	for (lvl = 0; lvl < 4; lvl++) {
		unsigned idx = (va >> (39 - 9 * lvl)) & 0x1ff;

		if ((p = ram(table + 8 * idx, 8)) == NULL) {
			snprintf(why, sizeof(why), "level %d table at 0x%llx is "
			    "not memory the service owns", lvl,
			    (unsigned long long)table);
			return -1;
		}
		memcpy(&e, p, 8);
		if (lvl < 3) {
			if ((e & 3) != 3) {
				snprintf(why, sizeof(why), "level %d entry %u "
				    "for 0x%llx: 0x%llx is not a table", lvl, idx,
				    (unsigned long long)va,
				    (unsigned long long)e);
				fault_exc = REF_TRANSLATION(lvl);
				return -1;
			}
			table = e & 0x000ffffffffff000ull;
			continue;
		}
		if ((e & 3) != 1) {
			snprintf(why, sizeof(why), "level 3 entry for 0x%llx: "
			    "type %u (a Mali LPAE leaf is 1; 3 is the CPU's)",
			    (unsigned long long)va, (unsigned)(e & 3));
			fault_exc = REF_TRANSLATION(3);
			return -1;
		}
		if (((e >> 6) & 1) == 0 || (write && ((e >> 7) & 1) == 0)) {
			snprintf(why, sizeof(why), "0x%llx: %s not permitted "
			    "(entry 0x%llx)", (unsigned long long)va,
			    write ? "write" : "read", (unsigned long long)e);
			fault_exc = REF_PERMISSION(3);
			return -1;
		}
		if (((e >> 8) & 3) != 2) {
			snprintf(why, sizeof(why), "0x%llx: shareability %u, "
			    "panfrost's is outer (2)", (unsigned long long)va,
			    (unsigned)((e >> 8) & 3));
			return -1;
		}
		*pa = (e & 0x000ffffffffff000ull) | (va & 0xfff);
		return 0;
	}
	return -1;
}

static uint64_t
rd64(const unsigned char *p, unsigned off)
{
	uint64_t v;

	memcpy(&v, p + off, 8);
	return v;
}

static uint32_t
rd32(const unsigned char *p, unsigned off)
{
	uint32_t v;

	memcpy(&v, p + off, 4);
	return v;
}

/* Задание падает: бит отказа слота 1 и состояние, которое дало железо. */
static void
job_fail(void)
{
	G(JOB_INT_RAWSTAT) |= JOB_INT_FAILED(1);
	G(JS_STATUS(1)) = REF_JS_FAULT;
}

/*
 * Проход не нашёл страницы: MMU докладывает об отказе адресного
 * пространства 0, и задание стоит, пока его не выключат (или, в варианте
 * fail_at_once, падает сразу).
 */
static void
mmu_fault(uint64_t va, int write)
{
	G(MMU_INT_RAWSTAT) |= MMU_INT_PAGE_FAULT(0);
	G(AS_FAULTSTATUS(0)) = fault_exc |
	    (uint32_t)(write ? AS_ACCESS_WRITE : AS_ACCESS_READ) << 8 |
	    REF_SOURCE << 16;
	G(AS_FAULTADDRESS_LO(0)) = (uint32_t)va;
	G(AS_FAULTADDRESS_HI(0)) = (uint32_t)(va >> 32);
	if (fail_at_once)
		job_fail();
	else
		stalled = 1;
}

/* Адресное пространство 0 выключено: последней командой UPDATE без таблиц. */
static int
as_off(void)
{
	return G(AS_COMMAND(0)) == AS_COMMAND_UPDATE &&
	    (G(AS_TRANSTAB_LO(0)) & 3) != 3;
}

/*
 * Что железо делает со сделанными службой записями: снимает сброшенные
 * биты прерываний и роняет стоящее на отказе задание, когда его адресное
 * пространство выключили.  Выключение при этом снова поднимает бит отказа
 * (замерено на плате: сброс до выключения берёт бит, после выключения он
 * опять 1 - у panfrost так и остаётся под маской, ref-fault.txt).  Сброс,
 * записанный до выключения, применяется до этого, поэтому зовущий settle()
 * сразу после обработчика, а сброс irq_handle() - после.
 */
static void
settle(void)
{
	G(MMU_INT_RAWSTAT) &= ~G(MMU_INT_CLEAR);
	G(MMU_INT_CLEAR) = 0;
	G(JOB_INT_RAWSTAT) &= ~G(JOB_INT_CLEAR);
	G(JOB_INT_CLEAR) = 0;
	if (stalled && as_off()) {
		stalled = 0;
		job_fail();
		G(MMU_INT_RAWSTAT) |= MMU_INT_PAGE_FAULT(0);
	}
}

/*
 * Одна линия, как её обслуживает irq_handle() в mali.c: обработчику -
 * STAT, то есть RAWSTAT под маской, потом запись STAT в CLEAR.  Между
 * ними - то, что сделали записи самого обработчика.  1, если линия
 * поднята.
 */
static unsigned mmu_lines, job_lines;

static int
deliver_mmu(void)
{
	uint32_t s;

	settle();
	if ((s = G(MMU_INT_RAWSTAT) & G(MMU_INT_MASK)) == 0)
		return 0;
	mmu_lines++;
	mali_mmu_irq(s);
	settle();
	G(MMU_INT_CLEAR) |= s;
	settle();
	return 1;
}

static int
deliver_job(void)
{
	uint32_t s;

	settle();
	if ((s = G(JOB_INT_RAWSTAT) & G(JOB_INT_MASK)) == 0)
		return 0;
	job_lines++;
	mali_job_irq(s);
	settle();
	G(JOB_INT_CLEAR) |= s;
	settle();
	return 1;
}

/*
 * Исполнить то, что служба поставила в слот 1: цепочку WRITE_VALUE через
 * адресное пространство 0.  Возвращает число исполненных заданий (на
 * отказе MMU - сколько успело до него) или -1 с причиной в why.
 */
static int
run_slot(void)
{
	uint64_t transtab, va, pa, dst;
	unsigned char *d, *t;
	int n = 0;
	unsigned seen[1024] = { 0 }, idx, k;
	static unsigned char dirty[ARENA_PAGES * PAGE / LINE];

	settle();
	if (stalled) {
		snprintf(why, sizeof(why), "the slot was started while a job "
		    "waits on a fault");
		return -1;
	}
	transtab = ((uint64_t)G(AS_TRANSTAB_HI(0)) << 32) |
	    G(AS_TRANSTAB_LO(0));
	va = ((uint64_t)G(JS_HEAD_NEXT_HI(1)) << 32) | G(JS_HEAD_NEXT_LO(1));

	/*
	 * Что грязно в кэше процессора - до того, как ускоритель что-то
	 * запишет: строка, в которой вид процессора расходится с ОЗУ сейчас,
	 * однажды будет вытеснена поверх записанного.
	 */
	for (k = 0; k < arena_used / LINE; k++)
		dirty[k] = memcmp(cpu_view + k * LINE, ram_view + k * LINE,
		    LINE) != 0;

	/* Как железо: «следующий» набор слота становится текущим. */
	G(JS_HEAD_LO(1)) = G(JS_HEAD_NEXT_LO(1));
	G(JS_HEAD_HI(1)) = G(JS_HEAD_NEXT_HI(1));
	G(JS_CONFIG(1)) = G(JS_CONFIG_NEXT(1)) & 0xffff;
	G(JS_COMMAND_NEXT(1)) = 0;

	while (va != 0) {
		if (n >= 1024) {
			snprintf(why, sizeof(why), "the chain does not end");
			return -1;
		}
		if (walk(transtab, va, 0, &pa) != 0) {
			if (fault_exc == 0)
				return -1;
			mmu_fault(va, 0);
			return n;
		}
		if ((d = ram(pa, JOB_DESC_SIZE)) == NULL)
			return -1;
		/* Раскладка jobprobe.py, исполненная на плате. */
		if ((rd32(d, 16) & 1) != 1) {
			snprintf(why, sizeof(why), "job %d: not 64-bit", n);
			return -1;
		}
		if (((rd32(d, 16) >> 1) & 0x7f) != 2) {
			snprintf(why, sizeof(why), "job %d: type %u, not "
			    "WRITE_VALUE", n, (rd32(d, 16) >> 1) & 0x7f);
			return -1;
		}
		idx = rd32(d, 16) >> 16;
		if (idx == 0 || idx >= 1024 || seen[idx]) {
			snprintf(why, sizeof(why), "job %d: index %u zero or "
			    "repeated", n, idx);
			return -1;
		}
		seen[idx] = 1;
		if (rd32(d, 40) != 6) {
			snprintf(why, sizeof(why), "job %d: value type %u, not "
			    "32-bit immediate", n, rd32(d, 40));
			return -1;
		}
		if (walk(transtab, rd64(d, 32), 1, &dst) != 0) {
			if (fault_exc == 0)
				return -1;
			mmu_fault(rd64(d, 32), 1);
			return n;
		}
		if ((t = ram(dst, 4)) == NULL)
			return -1;
		/* Строка процессора поверх строки, которую пишет ускоритель. */
		if (dirty[(dst - PHYS_BASE) / LINE]) {
			snprintf(why, sizeof(why), "job %d writes 0x%llx, whose "
			    "line is dirty in the processor's cache", n,
			    (unsigned long long)dst);
			return -1;
		}
		memcpy(t, d + 48, 4);
		memset(d, 0, 4);
		d[0] = JOB_STATUS_DONE;
		va = rd64(d, 24);
		n++;
	}
	G(JOB_INT_RAWSTAT) |= JOB_INT_DONE(1);
	return n;
}

static void
reset(void)
{
	memset(gpu, 0, sizeof(gpu));
	memset(&mali, 0, sizeof(mali));
	mali.info.gpu.base = 0xfde60000;
	mali.info.gpu.size = sizeof(gpu);
	mali.info.gpu.v = (vir_bytes)gpu;
	mali.info.irq[MALI_IRQ_GPU] = 71;
	mali.info.irq[MALI_IRQ_JOB] = 72;
	mali.info.irq[MALI_IRQ_MMU] = 73;
	mali.shader_present = 1;
	memset(cpu_view, 0, ARENA_PAGES * PAGE);
	memset(ram_view, 0, ARENA_PAGES * PAGE);
	arena_used = 0;
	ncops = 0;
	freed_addr = NULL;
	alarm_ticks = -1;
	loglen = 0;
	logbuf[0] = '\0';
	warnings = 0;
	fault_exc = 0;
	stalled = 0;
	fail_at_once = 0;
	mmu_lines = job_lines = 0;
}

static void
test_chain(void)
{
	unsigned c0, c1;
	uint64_t transtab;
	int r, n;

	printf("цепочка заданий на модели\n");
	reset();

	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	CHECK(arena_used == 6 * PAGE, "%zu bytes allocated", arena_used);
	CHECK(covered(CACHE_CLEAN, 0, 6 * PAGE, 0),
	    "the tables were not cleaned to memory");
	transtab = ((uint64_t)G(AS_TRANSTAB_HI(0)) << 32) |
	    G(AS_TRANSTAB_LO(0));
	CHECK((transtab & 0xfff) == 0x007, "TRANSTAB low bits 0x%llx, the "
	    "board's are 0x007", (unsigned long long)(transtab & 0xfff));
	CHECK((transtab & ~0xfffull) == PHYS_BASE, "root 0x%llx",
	    (unsigned long long)(transtab & ~0xfffull));
	CHECK(G(AS_MEMATTR_LO(0)) == 0x888d88 && G(AS_MEMATTR_HI(0)) == 0,
	    "MEMATTR 0x%08x%08x", G(AS_MEMATTR_HI(0)), G(AS_MEMATTR_LO(0)));
	CHECK(G(AS_TRANSCFG_LO(0)) == 0, "TRANSCFG written");
	CHECK(G(AS_COMMAND(0)) == AS_COMMAND_UPDATE, "last address space "
	    "command %u, want UPDATE", G(AS_COMMAND(0)));
	CHECK(G(AS_LOCKADDR_LO(0)) == 63, "lock region %u, panfrost's is 63",
	    G(AS_LOCKADDR_LO(0)));
	CHECK(G(MMU_INT_MASK) == MMU_INT_AS(0), "mmu mask 0x%x, want the "
	    "address space's two bits", G(MMU_INT_MASK));

	c0 = ncops;
	r = mali_job_start(0);
	CHECK(r == OK, "start: %d", r);
	CHECK(G(JS_COMMAND_NEXT(1)) == JS_COMMAND_START, "slot 1 not started");
	CHECK(G(JS_HEAD_NEXT_LO(1)) == 0x02000000 && G(JS_HEAD_NEXT_HI(1)) == 0,
	    "head 0x%08x%08x, panfrost's first buffer is at 0x2000000",
	    G(JS_HEAD_NEXT_HI(1)), G(JS_HEAD_NEXT_LO(1)));
	CHECK(G(JS_AFFINITY_NEXT_LO(1)) == 1, "affinity 0x%x",
	    G(JS_AFFINITY_NEXT_LO(1)));
	CHECK((G(JS_CONFIG_NEXT(1)) & 0xffff) == 0x7b00, "config 0x%08x, the "
	    "board's first job ran with 0x7b00", G(JS_CONFIG_NEXT(1)));
	CHECK((G(JS_CONFIG_NEXT(1)) & 0xf) == 0, "address space %u",
	    G(JS_CONFIG_NEXT(1)) & 0xf);
	CHECK(G(JOB_INT_MASK) == 0x70007, "job mask 0x%x", G(JOB_INT_MASK));
	CHECK(alarm_ticks == 100, "no deadline set (%ld)", alarm_ticks);
	CHECK(covered(CACHE_CLEAN, 4 * PAGE, 2 * PAGE, c0),
	    "the job buffer was not cleaned before the start");

	n = run_slot();
	CHECK(n == 64, "the model ran %d job(s): %s", n, why);

	c1 = ncops;
	mali_job_irq(JOB_INT_DONE(1));
	CHECK(covered(CACHE_INVALIDATE, 5 * PAGE, 4 * 65, c1),
	    "the buffer was read without invalidating first");
	CHECK(said("chain of 64: done in") && said("0 wrong word(s), canary "
	    "0xdeadbeef, job status first 0x1 last 0x1, 0 not done"),
	    "result: %s", logbuf);
	CHECK(said("every word checked by the processor"), "no success line");
	CHECK(alarm_ticks == 0, "deadline not cancelled");
	CHECK(warnings == 0, "%u warning(s)", warnings);

	/* Второй запуск: флаг цепочки чередуется. */
	r = mali_job_start(0);
	CHECK(r == OK && (G(JS_CONFIG_NEXT(1)) & 0xffff) == 0x7300,
	    "second chain: %d config 0x%x", r, G(JS_CONFIG_NEXT(1)));

	/* Будильник без прерывания: слот останавливается, слово о нём. */
	loglen = 0;
	logbuf[0] = '\0';
	mali_job_alarm();
	CHECK(G(JS_COMMAND(1)) == JS_COMMAND_HARD_STOP && said("no job "
	    "interrupt within a second"), "timeout: %s", logbuf);

	/* Отказ слота - слово с его состоянием, без проверки буфера. */
	G(JS_COMMAND_NEXT(1)) = 0;		/* слот взял остановленное */
	r = mali_job_start(0);
	CHECK(r == OK, "third chain: %d", r);
	loglen = 0;
	logbuf[0] = '\0';
	G(JS_STATUS(1)) = 0x58;
	mali_job_irq(JOB_INT_FAILED(1));
	CHECK(said("the chain failed, slot status 0x58") &&
	    !said("wrong word"), "failure: %s", logbuf);

	/* Уход: адресное пространство пусто, память возвращена. */
	mali_job_teardown();
	CHECK(G(AS_TRANSTAB_LO(0)) == 0 && G(AS_TRANSTAB_HI(0)) == 0 &&
	    G(AS_MEMATTR_LO(0)) == 0, "address space left programmed");
	CHECK(freed_addr == cpu_view && freed_len == 6 * PAGE,
	    "memory not given back (%p, %zu)", freed_addr, freed_len);
}

/*
 * faulttest=1: цепочка, задание на неотображённый адрес, отказ по линии
 * MMU, адресное пространство выключено и включено снова, цепочка ещё раз.
 * at_once - вариант железа, роняющий задание сразу: обе линии в одном
 * уведомлении, и mali.c обслуживает линию заданий раньше линии MMU.
 */
static void
test_fault(int at_once)
{
	uint64_t transtab;
	int r, n;

	printf("отказ MMU%s\n", at_once ? ", задание падает сразу" : "");
	reset();
	fail_at_once = at_once;

	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	transtab = ((uint64_t)G(AS_TRANSTAB_HI(0)) << 32) |
	    G(AS_TRANSTAB_LO(0));
	r = mali_job_start(1);
	CHECK(r == OK, "start: %d", r);
	n = run_slot();
	CHECK(n == 64, "the chain before the fault: %d job(s): %s", n, why);
	CHECK(deliver_job() == 1, "no job line for the chain");
	CHECK(said("every word checked by the processor"), "chain: %s",
	    logbuf);

	/* Задание отказа: один дескриптор, пишет в пустую запись L3. */
	CHECK(said("one WRITE_VALUE job to GPU address 0x02010040"),
	    "no fault job: %s", logbuf);
	CHECK(G(JS_COMMAND_NEXT(1)) == JS_COMMAND_START &&
	    G(JS_HEAD_NEXT_LO(1)) == 0x02000000, "fault job not on the slot");
	n = run_slot();
	CHECK(n == 0, "the fault job: %d job(s) done (%s)", n, why);
	CHECK(G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0), "mmu raw 0x%x",
	    G(MMU_INT_RAWSTAT));
	CHECK(G(AS_FAULTADDRESS_LO(0)) == 0x02010040, "the model faulted at "
	    "0x%x (%s)", G(AS_FAULTADDRESS_LO(0)), why);

	if (at_once) {
		/* Одно уведомление: gpu, job, mmu - в этом порядке. */
		CHECK(deliver_job() == 1, "no job line");
		CHECK(said("the fault job failed") &&
		    !said("address space 0 back on"), "recovered before the "
		    "MMU line: %s", logbuf);
		CHECK(deliver_mmu() == 1, "no mmu line after the job line");
	} else {
		/* deliver_mmu() по шагам: что видно между ними - как на плате. */
		uint32_t s = G(MMU_INT_RAWSTAT) & G(MMU_INT_MASK);

		CHECK(s == MMU_INT_PAGE_FAULT(0), "mmu stat 0x%x", s);
		mmu_lines++;
		mali_mmu_irq(s);
		settle();
		CHECK(G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0) &&
		    (G(JOB_INT_RAWSTAT) & JOB_INT_FAILED(1)), "after the "
		    "switch-off the board has the fault bit up again and the "
		    "job failed: mmu raw 0x%x job raw 0x%x",
		    G(MMU_INT_RAWSTAT), G(JOB_INT_RAWSTAT));
		G(MMU_INT_CLEAR) |= s;
		settle();
		CHECK(G(MMU_INT_RAWSTAT) == 0, "the line's clear does not take "
		    "the bit (0x%x)", G(MMU_INT_RAWSTAT));
		/* Выключено, маска снята, задание упало по выключению. */
		CHECK(G(AS_TRANSTAB_LO(0)) == 0 && G(AS_TRANSTAB_HI(0)) == 0 &&
		    G(AS_MEMATTR_LO(0)) == 0 && G(AS_COMMAND(0)) ==
		    AS_COMMAND_UPDATE, "address space not switched off: "
		    "TRANSTAB 0x%08x command %u", G(AS_TRANSTAB_LO(0)),
		    G(AS_COMMAND(0)));
		CHECK((G(MMU_INT_MASK) & MMU_INT_AS(0)) == 0, "the faulty "
		    "address space stays unmasked (0x%x), panfrost masks it",
		    G(MMU_INT_MASK));
		CHECK(!said("address space 0 back on"), "recovered before the "
		    "job failed");
		CHECK(deliver_job() == 1, "the job did not fail once the "
		    "address space was off");
	}
	CHECK(said("line 73 (mmu): status 0x00000001; address space 0 fault "
	    "status 0x7c1003c3 (translation fault 0xc3, write access, source "
	    "0x7c10) at 0x0000000002010040"), "mmu line: %s", logbuf);
	CHECK(said("line 72 (job): the fault job failed, slot status 0x48"),
	    "job line: %s", logbuf);
	CHECK(said("fault test: the fault asked for; fault status 0x7c1003c3 "
	    "(reference 0x7c1003c3), slot status 0x48 (reference 0x48), "
	    "header status 0x0 (reference 0)"), "verdict: %s", logbuf);

	/* Включено снова, теми же таблицами; цепочка после отказа. */
	CHECK((((uint64_t)G(AS_TRANSTAB_HI(0)) << 32) | G(AS_TRANSTAB_LO(0)))
	    == transtab && G(AS_MEMATTR_LO(0)) == 0x888d88 &&
	    G(AS_COMMAND(0)) == AS_COMMAND_UPDATE, "address space not back: "
	    "TRANSTAB 0x%08x", G(AS_TRANSTAB_LO(0)));
	CHECK(G(MMU_INT_MASK) == MMU_INT_AS(0), "mmu mask 0x%x after the "
	    "fault", G(MMU_INT_MASK));
	CHECK(G(JS_COMMAND_NEXT(1)) == JS_COMMAND_START, "no chain after the "
	    "fault");
	n = run_slot();
	CHECK(n == 64, "the chain after the fault: %d job(s): %s", n, why);
	CHECK(deliver_job() == 1, "no job line for the chain after");
	CHECK(deliver_mmu() == 0, "an mmu line after the fault was over");
	CHECK(said("fault test passed: line 73 (mmu) reported the fault"),
	    "after: %s", logbuf);
	CHECK(mmu_lines == 1 && job_lines == 3, "lines: mmu %u job %u",
	    mmu_lines, job_lines);
	CHECK(warnings == 0, "%u warning(s): %s", warnings, logbuf);
	CHECK(alarm_ticks == 0, "deadline not cancelled");
	mali_job_teardown();
}

/*
 * Отказ, которого никто не просил (цепочка с испорченной таблицей):
 * адресное пространство выключается, задание падает, и служба этого не
 * скрывает - но и не включает пространство обратно и ничего не запускает.
 */
static void
test_unasked_fault(void)
{
	uint64_t *l3;
	int r, n;

	printf("отказ, которого никто не просил\n");
	reset();
	r = mali_job_setup();
	CHECK(r == OK, "setup: %d", r);
	l3 = (uint64_t *)(cpu_view + 3 * PAGE);
	l3[1] = 0;			/* страница буфера пропала */
	sys_cachectl(CACHE_CLEAN, l3, 8 * 2);
	r = mali_job_start(1);
	CHECK(r == OK, "start: %d", r);
	n = run_slot();
	CHECK(n == 0 && G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0),
	    "%d job(s), mmu raw 0x%x", n, G(MMU_INT_RAWSTAT));
	CHECK(deliver_mmu() == 1, "no mmu line");
	CHECK(warnings >= 1 && said("a fault nobody asked for"), "silent: %s",
	    logbuf);
	CHECK(as_off(), "the address space is still on");
	CHECK(deliver_job() == 1 && said("the chain failed"), "job: %s",
	    logbuf);
	CHECK(!said("fault test: one WRITE_VALUE") && G(JS_COMMAND_NEXT(1)) == 0,
	    "something started after an unasked fault");
	mali_job_teardown();
}

/*
 * Модель без поблажек к самой себе: проход по таблицам отказывает там, где
 * должен.  Иначе стенд, принимающий любую таблицу, принял бы и неверную.
 */
static void
test_walker(void)
{
	uint64_t pa, *l3;

	printf("проход по таблицам отказывает, где должен\n");
	reset();
	(void)mali_job_setup();
	l3 = (uint64_t *)(cpu_view + 3 * PAGE);
	CHECK(walk(PHYS_BASE | 7, 0x02000000, 1, &pa) == 0 &&
	    pa == PHYS_BASE + 4 * PAGE, "the service's own mapping: %s", why);
	CHECK(walk(PHYS_BASE | 7, 0x02002000, 0, &pa) != 0,
	    "an unmapped page translated");
	/* Лист формата процессора (тип 3) - не лист Mali. */
	l3[0] = (l3[0] & ~3ull) | 3;
	sys_cachectl(CACHE_CLEAN, l3, 8);
	CHECK(walk(PHYS_BASE | 7, 0x02000000, 0, &pa) != 0 &&
	    strstr(why, "a Mali LPAE leaf is 1"), "a type-3 leaf accepted");
	l3[0] = (l3[0] & ~(3ull << 6 | 3ull)) | (1ull << 6) | 1;
	sys_cachectl(CACHE_CLEAN, l3, 8);
	CHECK(walk(PHYS_BASE | 7, 0x02000000, 1, &pa) != 0,
	    "a write through a read-only leaf accepted");
	CHECK(walk(PHYS_BASE | 6, 0x02000000, 0, &pa) != 0,
	    "a TRANSTAB without table mode accepted");
	mali_job_teardown();
}

int
main(int argc, char *argv[])
{
	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	if (posix_memalign((void **)&cpu_view, PAGE, ARENA_PAGES * PAGE) != 0 ||
	    posix_memalign((void **)&ram_view, PAGE, ARENA_PAGES * PAGE) != 0)
		return 2;

	test_chain();
	test_fault(0);
	test_fault(1);
	test_unasked_fault();
	test_walker();

	printf("mali jobs: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
