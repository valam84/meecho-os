/*
 * Стенд sunxi: драйвер sdmmc на эмулируемом контроллере Allwinner.
 *
 * Что это доказывает и чего нет. QEMU не умеет ни H313, ни H616, ни одной
 * 64-битной машины Allwinner; у него есть R40 ("bpim2u", Cortex-A7), и SD-
 * контроллер R40 он эмулирует моделью варианта A64 - того же поколения, что
 * у H616/H313. MEECHO на Cortex-A7 не запустить (система только AArch64),
 * поэтому сюда собраны без единой правки те файлы драйвера, что не зависят
 * от ядра: выбор хоста по дереву (sdmmc_host.c), слой тактов (sdmmc_clk.c),
 * слой карты (sdmmc_card.c) и сам контроллер (sdmmc_sunxi.c), - и гоняются
 * против эмулированного контроллера с эмулированной картой. Заглушки ниже -
 * только вызовы ядра MINIX, и ни одна из них не отвечает за SD.
 *
 * Не проверяется здесь: сдвиг адресов в дескрипторах H616 (модель A64 его
 * не знает), блок тактов H616 (его слова сверяет clktest), прерывание (линия
 * не выдаётся - проверяется переход на опрос). Заодно проверяется, что эти
 * файлы собираются и работают на ILP32.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <minix/blockdriver.h>
#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include "sdmmc.h"

struct log sdmmc_log = {
	.name = "sdmmc",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* ------------------------------------------------------------------ */
/* Машина: UART, таймер, MMU, выход				      */
/* ------------------------------------------------------------------ */

#define UART0		0x01c28000UL	/* 16550, reg-shift 2 */
#define UART_THR	0x00
#define UART_LSR	0x14
#define  LSR_THRE	(1u << 5)

static void
uart_putc(char c)
{
	volatile uint32_t *u = (volatile uint32_t *)UART0;

	while ((u[UART_LSR / 4] & LSR_THRE) == 0)
		;
	u[UART_THR / 4] = (uint32_t)(unsigned char)c;
}

void harness_semihost(int op, unsigned long arg);
void harness_exit(int code) __attribute__((noreturn));

void
harness_exit(int code)
{
	printf("harness: exit %d\n", code);
	harness_semihost(0x18, code == 0 ? 0x20026 : 0x20023);
	for (;;)
		;
}

static uint32_t cntfrq;

uint64_t
harness_now_us(void)
{
	uint32_t lo, hi;

	__asm__ volatile("isb; mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
	return (((uint64_t)hi << 32) | lo) * 1000000ULL / cntfrq;
}

/*
 * Плоское отображение секциями по мегабайту: DRAM - Normal без кэша (на ней
 * разрешён невыровненный доступ, которым пользуется newlib), остальное -
 * Device и не исполняется. Без MMU вся память Device, и первый же
 * невыровненный memcpy падает.
 */
static uint32_t l1[4096] __attribute__((aligned(16384)));

static void
mmu_on(void)
{
	uint32_t i, sctlr;

	for (i = 0; i < 4096; i++) {
		uint32_t base = i << 20;

		if (base >= 0x40000000u && base < 0x80000000u)
			l1[i] = base | (3u << 10) | (1u << 12) | 0x2;
		else
			l1[i] = base | (3u << 10) | (1u << 4) | (1u << 2) | 0x2;
	}
	__asm__ volatile(
	    "mcr p15, 0, %0, c2, c0, 0\n"	/* TTBR0 */
	    "mcr p15, 0, %1, c2, c0, 2\n"	/* TTBCR */
	    "mcr p15, 0, %2, c3, c0, 0\n"	/* DACR: client */
	    "mcr p15, 0, %1, c8, c7, 0\n"	/* TLBIALL */
	    "dsb\nisb\n"
	    :: "r"(l1), "r"(0), "r"(0x55555555));
	__asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
	sctlr |= 1u;			/* M */
	sctlr &= ~(1u << 1);		/* A: no alignment faults on Normal */
	__asm__ volatile("mcr p15, 0, %0, c1, c0, 0\nisb" :: "r"(sctlr));
	__asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(cntfrq));
	if (cntfrq == 0)
		cntfrq = 62500000;	/* QEMU's default */
}

/* newlib */
int _write(int fd, const char *p, int n)
{
	int i;

	(void)fd;
	for (i = 0; i < n; i++) {
		if (p[i] == '\n')
			uart_putc('\r');
		uart_putc(p[i]);
	}
	return n;
}
int _read(int fd, char *p, int n) { (void)fd; (void)p; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int o, int w) { (void)fd; (void)o; (void)w; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
void _exit(int code) { harness_exit(code); }
extern char __heap_start[];
void *
_sbrk(int incr)
{
	static char *brk = __heap_start;
	char *old = brk;

	brk += incr;
	return old;
}

/* ------------------------------------------------------------------ */
/* Вызовы ядра MINIX						      */
/* ------------------------------------------------------------------ */

static long harness_instance;
static unsigned maps, cachectls, irq_refusals;

void *vm_map_phys(endpoint_t who, void *phys, size_t len)
	{ (void)who; (void)len; maps++; return phys; }
int vm_unmap_phys(endpoint_t who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }

void *
alloc_contig(size_t len, int flags, phys_bytes *phys)
{
	static uint8_t pool[64 * 1024] __attribute__((aligned(4096)));
	static size_t used;
	void *p;

	(void)flags;
	len = (len + 4095) & ~(size_t)4095;
	if (used + len > sizeof(pool))
		return NULL;
	p = pool + used;
	used += len;
	*phys = (phys_bytes)p;
	return p;
}

int sys_cachectl(int op, void *addr, size_t len)
	{ (void)op; (void)addr; (void)len; cachectls++; return OK; }
int sys_irqsetpolicy(int irq, int policy, int *hook)
	{ (void)irq; (void)policy; (void)hook; irq_refusals++; return EPERM; }
int sys_irqrmpolicy(int *hook) { (void)hook; return OK; }
int sys_irqenable(int *hook) { (void)hook; return EPERM; }
int sys_irqdisable(int *hook) { (void)hook; return OK; }
int sys_setalarm(unsigned long t, int a) { (void)t; (void)a; return OK; }
int driver_receive(endpoint_t s, message *m, int *st)
	{ (void)s; (void)m; (void)st; return EIO; }
void blockdriver_mq_queue(message *m, int s) { (void)m; (void)s; }
unsigned long micros_to_ticks(unsigned long us) { return us / 10000 + 1; }

void
micro_delay(unsigned long us)
{
	uint64_t end = harness_now_us() + us;

	while (harness_now_us() < end)
		;
}

int
env_parse(const char *name, const char *fmt, int field, long *param,
	long min, long max)
{
	(void)fmt; (void)field; (void)min; (void)max;
	if (strcmp(name, "instance") == 0) {
		*param = harness_instance;
		return EP_SET;
	}
	return 0;
}

size_t
strlcpy(char *dst, const char *src, size_t size)
{
	size_t n = strlen(src);

	if (size > 0) {
		size_t k = (n >= size) ? size - 1 : n;

		memcpy(dst, src, k);
		dst[k] = '\0';
	}
	return n;
}

extern const char harness_dtb[], harness_dtb_end[];

void *
fdt_fetch(void)
{
	size_t n = (size_t)(harness_dtb_end - harness_dtb);
	void *p = malloc(n);

	if (p != NULL)
		memcpy(p, harness_dtb, n);
	return p;
}

/* Хосты, которых в стенде нет: таблица на них ссылается. */
int sdhci_probe(const struct fdt_node *n, const struct sdmmc_devinfo *i,
	struct sdmmc_host *h) { (void)n; (void)i; (void)h; return ENXIO; }
int dwmshc_probe(const struct fdt_node *n, const struct sdmmc_devinfo *i,
	struct sdmmc_host *h) { (void)n; (void)i; (void)h; return ENXIO; }

/* ------------------------------------------------------------------ */
/* Проверки							      */
/* ------------------------------------------------------------------ */

static unsigned checks, failures;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/*
 * Узор на носителе, тот же, что пишет mkimg.py: слово w сектора s - это
 * (s * 0x9e3779b1 + w * 0x85ebca77) ^ seed. Образ размечен им в первых и
 * последних 2048 секторах; запись стенда - другим зерном.
 */
#define SEED_IMAGE	0xa5a5a5a5u
#define SEED_WRITE	0x3c3c3c3cu
#define PATTERNED	2048

static uint32_t
pat(uint32_t s, uint32_t w, uint32_t seed)
{
	return (s * 0x9e3779b1u + w * 0x85ebca77u) ^ seed;
}

static void
fill(uint32_t *buf, uint32_t first, uint32_t count, uint32_t seed)
{
	uint32_t s, w;

	for (s = 0; s < count; s++)
		for (w = 0; w < SDMMC_SECTOR_SIZE / 4; w++)
			buf[s * 128 + w] = pat(first + s, w, seed);
}

static int
same(const uint32_t *buf, uint32_t first, uint32_t count, uint32_t seed)
{
	uint32_t s, w;

	for (s = 0; s < count; s++)
		for (w = 0; w < SDMMC_SECTOR_SIZE / 4; w++)
			if (buf[s * 128 + w] != pat(first + s, w, seed)) {
				printf("  sector %lu word %lu: %08lx, want "
				    "%08lx\n", (unsigned long)(first + s),
				    (unsigned long)w,
				    (unsigned long)buf[s * 128 + w],
				    (unsigned long)pat(first + s, w, seed));
				return 0;
			}
	return 1;
}

static uint32_t big[320 * 128] __attribute__((aligned(4096)));
#define PHYS(p)	((phys_bytes)(p))

static void
rd(const char *what, uint64_t s, uint32_t n, int dma, uint32_t seed)
{
	int r;

	memset(big, 0xee, n * SDMMC_SECTOR_SIZE);
	r = sdmmc_card_read(s, n, big, dma ? PHYS(big) : 0);
	CHECK(r == OK, "%s: read %lu+%lu: %d", what, (unsigned long)s,
	    (unsigned long)n, r);
	if (r == OK)
		CHECK(same(big, (uint32_t)s, n, seed), "%s: data", what);
}

static void
wrt(const char *what, uint64_t s, uint32_t n, int dma)
{
	int r;

	fill(big, (uint32_t)s, n, SEED_WRITE);
	r = sdmmc_card_write(s, n, big, dma ? PHYS(big) : 0);
	CHECK(r == OK, "%s: write %lu+%lu: %d", what, (unsigned long)s,
	    (unsigned long)n, r);
}

int
main(void)
{
	struct sdmmc_host host;
	struct sdmmc_card card;
	uint64_t n, t0, t1;
	int r;

	mmu_on();
	printf("\nharness: sdmmc on QEMU bpim2u (Allwinner R40, A64-type "
	    "SD host), timer %lu Hz\n", (unsigned long)cntfrq);

	memset(&host, 0, sizeof(host));
	r = sdmmc_host_find(&host);
	CHECK(r == OK, "host find: %d", r);
	if (r != OK)
		harness_exit(1);
	CHECK(strcmp(host.name, "allwinner,sun50i-a64-mmc") == 0,
	    "host %s", host.name);

	r = host.init();
	CHECK(r == OK, "host init: %d", r);
	if (r != OK)
		harness_exit(1);
	CHECK(irq_refusals == 1, "the line was asked for once (%u)",
	    irq_refusals);

	t0 = harness_now_us();
	r = sdmmc_card_init(&host, &card);
	t1 = harness_now_us();
	CHECK(r == OK, "card init: %d", r);
	if (r != OK)
		harness_exit(1);
	n = card.sectors;
	printf("harness: card: sd %d, sectors=%llu, %s-addressed, %u-bit, "
	    "%lu Hz%s, %s, %lu us\n", card.is_sd, (unsigned long long)n,
	    card.sector_addressed ? "sector" : "byte", card.bus_width,
	    (unsigned long)card.clock, card.high_speed ? ", high speed" : "",
	    card.cmd23 ? "CMD23" : "CMD12", (unsigned long)(t1 - t0));
	CHECK(card.is_sd, "an SD card");
	CHECK(card.bus_width == 4, "four bits (%u)", card.bus_width);

	printf("harness: reads\n");
	rd("PIO 1", 5, 1, 0, SEED_IMAGE);
	rd("PIO 8", 100, 8, 0, SEED_IMAGE);
	rd("PIO 20 (three commands)", 130, 20, 0, SEED_IMAGE);
	rd("DMA 64", 200, 64, 1, SEED_IMAGE);
	rd("DMA 300 (75 descriptors)", 700, 300, 1, SEED_IMAGE);
	rd("DMA last 8", n - 8, 8, 1, SEED_IMAGE);
	rd("DMA tail 64", n - PATTERNED, 64, 1, SEED_IMAGE);
	r = sdmmc_card_read(n - 1, 2, big, PHYS(big));
	CHECK(r == EINVAL, "read past the end refused (%d)", r);

	printf("harness: writes\n");
	wrt("DMA 64", 1024, 64, 1);
	wrt("PIO 1", 1100, 1, 0);
	wrt("PIO 3", 1200, 3, 0);
	wrt("DMA 1 at the end", n - 1, 1, 1);
	r = sdmmc_card_flush();
	CHECK(r == OK, "flush: %d", r);

	printf("harness: read back\n");
	rd("back DMA 64", 1024, 64, 1, SEED_WRITE);
	rd("back PIO 1", 1100, 1, 0, SEED_WRITE);
	rd("back DMA 3", 1200, 3, 1, SEED_WRITE);
	rd("back last", n - 1, 1, 1, SEED_WRITE);
	rd("neighbour before", 1023, 1, 1, SEED_IMAGE);
	rd("neighbour after", 1088, 1, 0, SEED_IMAGE);
	rd("neighbour 1203", 1203, 1, 0, SEED_IMAGE);

	host.exit();
	printf("harness: %s, %u checks, %u failures, %u maps, %u cache ops\n",
	    failures ? "FAIL" : "PASS", checks, failures, maps, cachectls);
	harness_exit(failures ? 1 : 0);
	return 0;
}
