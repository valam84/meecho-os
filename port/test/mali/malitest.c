/*
 * Стенд службы mali: поиск по дереву и обвязка RK3568, без железа.
 *
 * Собирает настоящие mali_find.c и mali_rk.c (не копии) с настоящей
 * читалкой дерева libsys и гоняет их на настоящих деревьях:
 *
 *   cb2   port/cb2-display/live.dtb - дерево работающей платы (вендорское,
 *         с оверлеем hdmi, который включает и gpu);
 *   base  port/cb2-sd/rk3566-bigtreetech-cb2.dtb - то же без оверлеев, где
 *         узел gpu выключен;
 *   h313  port/h313/h313-tanix-tx1.dtb - Allwinner с Mali-G31: узел тот же
 *         "arm,mali-bifrost", а обвязки Rockchip нет.
 *
 * Регистры CRU и PMU модели - числа, снятые с платы под вендорским Linux
 * (port/cb2-display/ref-720p-forced.txt, 2026-10-12): домен PD_GPU
 * выключен, источник такта gpll/2, мультиплексор ядра на PVTPLL.
 *
 * Записи в CRU и PMU маскированные (старшая половина - какие биты младшей
 * менять), а драйвер пишет прямой записью в память; поэтому модель
 * проверяет само записанное слово, а PMU оживляет в micro_delay() - так же,
 * как на плате ожидание и есть то, что даёт PMU время.
 *
 *	sh run.sh       проверки
 *	sh run.sh -v    и журнал драйвера
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>

#include "mali.h"

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

static void
log_reset(void)
{
	loglen = 0;
	logbuf[0] = '\0';
	warnings = 0;
}

static int
said(const char *what)
{
	return strstr(logbuf, what) != NULL;
}

/* ---- дерево ---------------------------------------------------------- */

static const char *dtb_file;

void *
fdt_fetch(void)
{
	FILE *f = fopen(dtb_file, "rb");
	long n;
	void *buf;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc((size_t)n);
	if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

/* ---- модель ---------------------------------------------------------- */

#define CRU_BASE	0xfdd20000
#define PMU_BASE	0xfdd90000
#define GPU_BASE	0xfde60000

static uint32_t cru[1024], pmu[1024], gpu[4096], other[16384];
static unsigned mapped;

/* Кто не отвечает: PMU не включает питание или не выпускает шину. */
static int pmu_stuck_power, pmu_stuck_idle;

#define C(off)	cru[(off) / 4]
#define P(off)	pmu[(off) / 4]

static void
model_reset(void)
{
	memset(cru, 0, sizeof(cru));
	memset(pmu, 0, sizeof(pmu));
	memset(gpu, 0, sizeof(gpu));
	mapped = 0;
	pmu_stuck_power = pmu_stuck_idle = 0;

	/* CRU платы под Linux: PLL, MODE_CON0, CLKSEL_CON6, вентили, сбросы. */
	C(0x00) = 0x0000104b; C(0x04) = 0x00001441; C(0x0c) = 0x00000007;
	C(0x20) = 0x0000406c; C(0x24) = 0x00001501; C(0x2c) = 0x00000007;
	C(0x40) = 0x00002063; C(0x44) = 0x00001441; C(0x4c) = 0x00000007;
	C(0x60) = 0x0000107d; C(0x64) = 0x00001443; C(0x6c) = 0x00000007;
	C(0x80) = 0x00001032; C(0x84) = 0x00001441;
	C(0xa0) = 0x0000307d; C(0xa4) = 0x00001442;
	C(0xc0) = 0x00004455;
	C(0x118) = 0x00004951;
	C(0x308) = 0x000001c0;
	C(0x408) = 0x00000000;

	/* PMU платы под Linux: GPU выключен и его шина в простое. */
	P(0x50) = 0x000001e6;
	P(0x60) = 0x000001ee;
	P(0x68) = 0x000001ee;
	P(0x98) = 0x0000007f;
	P(0xa0) = 0x0000007f;
}

void *
vm_map_phys(endpoint_t who, void *phaddr, size_t len)
{
	unsigned long pa = (unsigned long)phaddr;

	(void)len;
	if (who != SELF)
		return MAP_FAILED;
	mapped++;
	switch (pa) {
	case CRU_BASE:	return cru;
	case PMU_BASE:	return pmu;
	case GPU_BASE:	return gpu;
	default:	return other;	/* чужой SoC: окно есть, смысла нет */
	}
}

/*
 * PMU оживает во время ожидания: снятый запрос питания домена 0 гасит его
 * бит состояния, снятый запрос простоя шины - бит подтверждения и бит
 * простоя.  Слова в PWR_CON и IDLE_REQ - то, что драйвер записал последним.
 */
int
micro_delay(u32_t usec)
{
	(void)usec;
	if ((P(0xa0) & 0x10000) && !(P(0xa0) & 0x1) && !pmu_stuck_power)
		P(0x98) &= ~0x1u;
	if ((P(0x50) & 0x20000) && !(P(0x50) & 0x2) && !pmu_stuck_idle) {
		P(0x60) &= ~0x2u;
		P(0x68) &= ~0x2u;
	}
	return 0;
}

/* ---- проверки -------------------------------------------------------- */

static int
find_on(const char *file)
{
	dtb_file = file;
	memset(&mali, 0, sizeof(mali));
	log_reset();
	return mali_find(&mali.info);
}

static void
test_find_cb2(const char *file)
{
	struct mali_info *i = &mali.info;
	int r;

	printf("дерево платы (%s)\n", file);
	r = find_on(file);
	CHECK(r == OK, "mali_find: %d", r);
	CHECK(i->gpu.base == GPU_BASE && i->gpu.size == 0x4000,
	    "gpu 0x%lx/%zu", i->gpu.base, i->gpu.size);
	/* SPI 39/40/41 названы "gpu", "mmu", "job" - в таком порядке. */
	CHECK(i->irq[MALI_IRQ_GPU] == 71, "gpu line %d", i->irq[MALI_IRQ_GPU]);
	CHECK(i->irq[MALI_IRQ_JOB] == 72, "job line %d", i->irq[MALI_IRQ_JOB]);
	CHECK(i->irq[MALI_IRQ_MMU] == 73, "mmu line %d", i->irq[MALI_IRQ_MMU]);
	CHECK(i->bus_clk_id == 27, "bus clock %u", i->bus_clk_id);
	CHECK(i->bus_clk_provider == CRU_BASE, "bus provider 0x%lx",
	    i->bus_clk_provider);
	CHECK(i->gpu_clk_id == 1, "gpu clock %u", i->gpu_clk_id);
	CHECK(i->power_domain == 7, "domain %d", i->power_domain);
	CHECK(i->cru.base == CRU_BASE, "cru 0x%lx", i->cru.base);
	CHECK(i->pmu.base == PMU_BASE, "pmu 0x%lx", i->pmu.base);
	CHECK(strcmp(i->supply_name, "vdd_gpu") == 0, "supply \"%s\"",
	    i->supply_name);
	CHECK(i->supply_always_on && i->supply_boot_on,
	    "supply always-on %d boot-on %d", i->supply_always_on,
	    i->supply_boot_on);
	CHECK(i->supply_init_uv == 900000, "supply %u uV", i->supply_init_uv);
	CHECK(warnings == 0, "%u warning(s) on the board's own tree",
	    warnings);
}

static void
test_find_absent(const char *file)
{
	int r;

	printf("дерево без включённого узла (%s)\n", file);
	r = find_on(file);
	CHECK(r == ENXIO, "mali_find: %d, want ENXIO", r);
	CHECK(said("disabled in the tree"), "no word about the disabled node");
	CHECK(said("no Mali GPU"), "no word about the absent GPU");
}

static void
test_foreign_soc(const char *file)
{
	int r;

	printf("чужой SoC (%s)\n", file);
	model_reset();
	r = find_on(file);
	CHECK(r == OK, "mali_find: %d (the node is there)", r);
	CHECK(mali.info.irq[MALI_IRQ_GPU] == 32 + 0x61 &&
	    mali.info.irq[MALI_IRQ_JOB] == 32 + 0x5f &&
	    mali.info.irq[MALI_IRQ_MMU] == 32 + 0x60,
	    "lines gpu %d job %d mmu %d", mali.info.irq[MALI_IRQ_GPU],
	    mali.info.irq[MALI_IRQ_JOB], mali.info.irq[MALI_IRQ_MMU]);
	CHECK(mali.info.cru.base == 0 && mali.info.pmu.base == 0,
	    "Rockchip blocks found on an Allwinner tree");
	log_reset();
	r = mali_rk_map();
	CHECK(r != OK, "mali_rk_map accepted a foreign SoC");
	CHECK(said("names no clock unit"), "no word about the clock unit");
	CHECK(other[0] == 0, "something was written to the foreign window");
}

static void
board_ready(const char *file)
{
	model_reset();
	(void)find_on(file);
	log_reset();
}

static void
test_rk(const char *file)
{
	int r;

	printf("обвязка RK3568 на числах платы\n");

	board_ready(file);
	r = mali_rk_map();
	CHECK(r == OK && mapped == 3, "mali_rk_map: %d, %u maps", r, mapped);

	/* Отчёт: PVTPLL под ядром, gpll/2 за ним, домен выключен. */
	log_reset();
	mali_rk_report("found");
	CHECK(said("gpll/2 (unused: the core runs off its PVTPLL)"),
	    "report: %s", logbuf);
	CHECK(said("aclk /2, pclk /5"), "report dividers");
	CHECK(said("source open, clk_gpu open"), "report gates");
	CHECK(said("resets 0x00000000 (released)"), "report resets");
	CHECK(said("PD_GPU off, bus idle"), "report domain");
	CHECK(warnings == 0, "report warned");

	/* Тот же источник без PVTPLL: частота считается. */
	C(0x118) = 0x00004151;
	log_reset();
	mali_rk_report("pll");
	CHECK(said("gpll/2 = 594000 kHz"), "rate: %s", logbuf);

	/*
	 * Не с платы: у всех её PLL второй постделитель равен единице, и
	 * поле шириной в два бита вместо трёх на её числах не видно.  GPLL
	 * 24 МГц * 99 / 1 / 4 = 594 МГц, источник /2 = 297 МГц.
	 */
	C(0x40) = 0x00001063;
	C(0x44) = 0x00001101;
	log_reset();
	mali_rk_report("pd2");
	CHECK(said("gpll/2 = 297000 kHz"), "postdiv2 4: %s", logbuf);

	/* Такты: PVTPLL -> путь PLL, gpll 1188 МГц / 3 = 396 МГц. */
	board_ready(file);
	(void)mali_rk_map();
	r = mali_rk_clocks_on();
	CHECK(r == OK, "clocks_on: %d", r);
	CHECK(C(0x118) == 0x080f0002, "CLKSEL_CON6 written 0x%08x, want "
	    "0x080f0002", C(0x118));
	CHECK(said("moving it to gpll/3 = 396000 kHz"), "clock: %s", logbuf);
	CHECK(C(0x308) == 0x000001c0, "gates written though open: 0x%08x",
	    C(0x308));

	/* cpll (1000 МГц) и npll (1200 МГц) за PVTPLL: тоже делятся на 3. */
	board_ready(file);
	(void)mali_rk_map();
	C(0x118) = 0x00004991;		/* mux 2: cpll */
	r = mali_rk_clocks_on();
	CHECK(r == OK && said("cpll/3 = 333333 kHz"), "cpll: %d %s", r,
	    logbuf);
	board_ready(file);
	(void)mali_rk_map();
	C(0x118) = 0x000049d1;		/* mux 3: npll */
	r = mali_rk_clocks_on();
	CHECK(r == OK && said("npll/3 = 400000 kHz"), "npll: %d %s", r,
	    logbuf);

	/* mpll за PVTPLL посчитать нечем: отказ, и ничего не записано. */
	board_ready(file);
	(void)mali_rk_map();
	C(0x118) = 0x00004911;		/* mux 0: mpll */
	r = mali_rk_clocks_on();
	CHECK(r == EIO, "mpll: %d, want EIO", r);
	CHECK(C(0x118) == 0x00004911, "mpll: CLKSEL written 0x%08x",
	    C(0x118));

	/* Закрытые вентили открываются маскированной записью. */
	board_ready(file);
	(void)mali_rk_map();
	C(0x118) = 0x00004151;
	C(0x308) = 0x000001c9;
	r = mali_rk_clocks_on();
	CHECK(r == OK, "gates: %d", r);
	CHECK(C(0x308) == 0x00090000, "CLKGATE_CON2 written 0x%08x, want "
	    "0x00090000", C(0x308));
	CHECK(C(0x118) == 0x00004151, "CLKSEL touched without PVTPLL");

	/* Домен: питание, потом шина - и слова ровно такие. */
	board_ready(file);
	(void)mali_rk_map();
	r = mali_rk_power_domain();
	CHECK(r == OK, "power_domain: %d", r);
	CHECK(P(0xa0) == 0x00010000, "PWR_CON written 0x%08x", P(0xa0));
	CHECK(P(0x50) == 0x00020000, "IDLE_REQ written 0x%08x", P(0x50));
	CHECK(!(P(0x98) & 1) && !(P(0x60) & 2) && !(P(0x68) & 2),
	    "domain not on: status 0x%x ack 0x%x idle 0x%x", P(0x98),
	    P(0x60), P(0x68));
	CHECK((P(0x98) & ~1u) == 0x7e, "other domains touched: 0x%x",
	    P(0x98));

	/* Уже включённый домен не трогается. */
	log_reset();
	P(0xa0) = 0x7e;
	P(0x50) = 0x1e4;
	r = mali_rk_power_domain();
	CHECK(r == OK && P(0xa0) == 0x7e && P(0x50) == 0x1e4,
	    "an on domain was written: %d 0x%x 0x%x", r, P(0xa0), P(0x50));
	CHECK(said("PD_GPU is on"), "no word about the on domain");

	/* PMU не включает питание: ошибка, и шину не трогали. */
	board_ready(file);
	(void)mali_rk_map();
	pmu_stuck_power = 1;
	r = mali_rk_power_domain();
	CHECK(r == EIO && said("did not come up"), "stuck power: %d", r);
	CHECK(P(0x50) == 0x000001e6, "bus released under a dead domain");

	/* PMU не выпускает шину: ошибка. */
	board_ready(file);
	(void)mali_rk_map();
	pmu_stuck_idle = 1;
	r = mali_rk_power_domain();
	CHECK(r == EIO && said("stayed idle"), "stuck idle: %d", r);

	/* Сбросы: отпускаются только удержанные, тремя битами. */
	board_ready(file);
	(void)mali_rk_map();
	mali_rk_resets();
	CHECK(C(0x408) == 0, "released resets written 0x%08x", C(0x408));
	C(0x408) = 0x5;
	mali_rk_resets();
	CHECK(C(0x408) == 0x00070000, "SOFTRST_CON2 written 0x%08x",
	    C(0x408));
}

int
main(int argc, char *argv[])
{
	const char *cb2, *base, *h313;

	if (argc < 4) {
		fprintf(stderr, "usage: malitest cb2.dtb base.dtb h313.dtb "
		    "[-v]\n");
		return 2;
	}
	cb2 = argv[1];
	base = argv[2];
	h313 = argv[3];
	verbose = argc > 4 && strcmp(argv[4], "-v") == 0;

	test_find_cb2(cb2);
	test_find_absent(base);
	test_foreign_soc(h313);
	test_rk(cb2);

	printf("mali: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
