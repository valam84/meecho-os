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
 *     TRANSCFG 0, JS_CONFIG 0x7b00 (младшие 16 бит).
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
 * Проход по таблицам по правилам Mali LPAE.  0 и физический адрес, или -1
 * и причина в why.
 */
static int
walk(uint64_t transtab, uint64_t va, int write, uint64_t *pa)
{
	uint64_t table, e;
	unsigned char *p;
	int lvl;

	if ((transtab & 3) != 3) {
		snprintf(why, sizeof(why), "TRANSTAB adrmode %u, not table",
		    (unsigned)(transtab & 3));
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
				return -1;
			}
			table = e & 0x000ffffffffff000ull;
			continue;
		}
		if ((e & 3) != 1) {
			snprintf(why, sizeof(why), "level 3 entry for 0x%llx: "
			    "type %u (a Mali LPAE leaf is 1; 3 is the CPU's)",
			    (unsigned long long)va, (unsigned)(e & 3));
			return -1;
		}
		if (((e >> 6) & 1) == 0 || (write && ((e >> 7) & 1) == 0)) {
			snprintf(why, sizeof(why), "0x%llx: %s not permitted "
			    "(entry 0x%llx)", (unsigned long long)va,
			    write ? "write" : "read", (unsigned long long)e);
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

/*
 * Исполнить то, что служба поставила в слот 1: цепочку WRITE_VALUE через
 * адресное пространство 0.  Возвращает число исполненных заданий или -1.
 */
static int
run_slot(void)
{
	uint64_t transtab, va, pa, dst;
	unsigned char *d, *t;
	int n = 0;
	unsigned seen[1024] = { 0 }, idx, k;
	static unsigned char dirty[ARENA_PAGES * PAGE / LINE];

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
		if (walk(transtab, va, 0, &pa) != 0 ||
		    (d = ram(pa, JOB_DESC_SIZE)) == NULL)
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
		if (walk(transtab, rd64(d, 32), 1, &dst) != 0 ||
		    (t = ram(dst, 4)) == NULL)
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

	c0 = ncops;
	r = mali_job_start();
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
	r = mali_job_start();
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
	r = mali_job_start();
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
	test_walker();

	printf("mali jobs: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
