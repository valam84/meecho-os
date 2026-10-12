/*
 * Стенд слоя тактов и сбросов sdmmc: sdmmc_clk.c и sdmmc_clk_rk3568.c.
 *
 * Две половины, и обе на числах платы, а не на придуманных.
 *
 * Разбор: настоящее дерево CB2 (port/cb2-sd/live.dtb, снято с работающей
 * платы) читается той же читалкой libsys, что и в системе, и для каждого
 * контроллера MMC сверяется, какие линии нашёл слой: номер такта и сброса
 * должен совпасть с тем, что Linux на плате показывает в своём дереве.
 * Ссылка на контроллер, которого слой не знает (PMUCRU у i2c), должна дать
 * «не моё», а не чужие регистры.
 *
 * Регистры: вместо CRU - кусок памяти, и после каждой операции сверяется
 * слово, которое в него легло. Ожидаемые значения посчитаны руками по
 * раскладке Rockchip (маска записи в старшей половине) и сверены с тем, что
 * стоит в CRU работающей платы; см. linux-reference.txt.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>

#include "sdmmc.h"

int stub_log_level = LEVEL_WARN;
struct log sdmmc_log = { "sdmmc", LEVEL_INFO, NULL };

static unsigned checks, failures;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  ОТКАЗ: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/* CRU: адрес и размер - из дерева платы. */
#define CRU_BASE	0xfdd20000UL
#define CRU_SIZE	0x1000
static uint8_t cru[CRU_SIZE] __attribute__((aligned(8)));
static unsigned maps;

/* CCU H616/H313 - из дерева Tanix TX1. */
#define CCU_BASE	0x03001000UL
#define CCU_SIZE	0x1000
static uint8_t ccu[CCU_SIZE] __attribute__((aligned(8)));
static unsigned ccu_maps;

void *
vm_map_phys(int who, void *phys, size_t len)
{
	(void)who;
	if ((unsigned long)phys == CRU_BASE && len == CRU_SIZE) {
		maps++;
		return cru;
	}
	if ((unsigned long)phys == CCU_BASE && len == CCU_SIZE) {
		ccu_maps++;
		return ccu;
	}
	printf("  ОТКАЗ: отображение 0x%lx+0x%zx - ни CRU, ни CCU\n",
	    (unsigned long)phys, len);
	failures++;
	return (void *)-1;
}

static uint32_t
creg(unsigned off)
{
	return *(uint32_t *)(ccu + off);
}

int vm_unmap_phys(int who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }
void micro_delay(unsigned long us) { (void)us; }

static uint32_t
reg(unsigned off)
{
	return *(uint32_t *)(cru + off);
}

static void
clear(void)
{
	memset(cru, 0, sizeof(cru));
}

/* Найти узел по имени и отдать его колбэку проверки. */
struct find {
	const char *name;
	void (*check)(const struct fdt_node *node);
	int found;
};

static int
find_cb(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct find *f = cookie;

	(void)depth;
	if (strcmp(name, f->name) != 0)
		return 0;
	f->found = 1;
	f->check(node);
	return 1;
}

static const void *dtb;

static void
with_node(const char *name, void (*check)(const struct fdt_node *))
{
	struct find f = { name, check, 0 };

	(void)fdt_walk(dtb, find_cb, &f);
	CHECK(f.found, "узла %s в дереве нет", name);
}

static struct sdmmc_line emmc_core, sd_ciu, sd_drv, sd_smp, sd_rst;
static struct sdmmc_line emmc_rst[SDMMC_MAX_RESETS];
static unsigned emmc_nrst;

static void
check_emmc(const struct fdt_node *node)
{
	/* clock-names = "core", "bus", "axi", "block", "timer" */
	static const struct { const char *name; unsigned id; } want[] = {
		{ "core", 124 }, { "bus", 122 }, { "axi", 121 },
		{ "block", 123 }, { "timer", 125 },
	};
	/* resets = <cru 0x78>, <0x76>, <0x75>, <0x77>, <0x79> */
	static const unsigned rst[] = { 0x78, 0x76, 0x75, 0x77, 0x79 };
	struct sdmmc_line l;
	unsigned i;
	int r;

	for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		r = sdmmc_clk_get(node, want[i].name, &l);
		CHECK(r == OK && l.ctl != SDMMC_NO_LINE && l.id == want[i].id,
		    "eMMC такт %s: r %d ctl %d id %u, ждали %u", want[i].name,
		    r, l.ctl, l.id, want[i].id);
	}
	(void)sdmmc_clk_get(node, "core", &emmc_core);

	for (i = 0; ; i++) {
		r = sdmmc_line_get(node, "resets", NULL, NULL, i, &l);
		if (r == ENOENT)
			break;
		CHECK(r == OK && i < 5 && l.id == rst[i],
		    "eMMC сброс %u: r %d id %u", i, r, l.id);
		if (r == OK && emmc_nrst < SDMMC_MAX_RESETS)
			emmc_rst[emmc_nrst++] = l;
	}
	CHECK(i == 5, "у eMMC пять сбросов, найдено %u", i);

	/* По имени сброса - то же, что по месту. */
	r = sdmmc_line_get(node, "resets", "reset-names", "block", 0, &l);
	CHECK(r == OK && l.id == 0x77, "сброс \"block\": r %d id %u", r, l.id);

	r = sdmmc_clk_get(node, "ciu", &l);
	CHECK(r == ENOENT && l.ctl == SDMMC_NO_LINE,
	    "у eMMC нет такта \"ciu\": r %d ctl %d", r, l.ctl);
}

static void
check_sd(const struct fdt_node *node)
{
	int r;

	r = sdmmc_clk_get(node, "ciu", &sd_ciu);
	CHECK(r == OK && sd_ciu.id == 177, "SD ciu: r %d id %u", r, sd_ciu.id);
	r = sdmmc_clk_get(node, "biu", &sd_rst);
	CHECK(r == OK && sd_rst.id == 176, "SD biu: r %d id %u", r, sd_rst.id);
	r = sdmmc_clk_get(node, "ciu-drive", &sd_drv);
	CHECK(r == OK && sd_drv.id == 394, "SD drive: r %d id %u", r,
	    sd_drv.id);
	r = sdmmc_clk_get(node, "ciu-sample", &sd_smp);
	CHECK(r == OK && sd_smp.id == 395, "SD sample: r %d id %u", r,
	    sd_smp.id);
	r = sdmmc_line_get(node, "resets", "reset-names", "reset", 0, &sd_rst);
	CHECK(r == OK && sd_rst.id == 212, "SD сброс: r %d id %u", r,
	    sd_rst.id);
	CHECK(sd_ciu.ctl == emmc_core.ctl,
	    "один CRU должен быть заведён один раз: %d и %d", sd_ciu.ctl,
	    emmc_core.ctl);
}

static void
check_pmucru(const struct fdt_node *node)
{
	struct sdmmc_line l;
	int r;

	/* clocks = <pmucru 7>, <pmucru 0x2d>: держатель не наш. */
	r = sdmmc_clk_get(node, "i2c", &l);
	CHECK(r == ENOTSUP && l.ctl == SDMMC_NO_LINE,
	    "такт у PMUCRU: r %d ctl %d - ждали ENOTSUP и NO_LINE", r, l.ctl);
	CHECK(sdmmc_clk_set_rate(&l, 1000000, NULL) == ENOSYS,
	    "операция над чужой линией должна быть ENOSYS");
}

static void
check_registers(void)
{
	uint32_t got;
	int r;

	/*
	 * cclk_emmc: CLKSEL_CON28 (0x170), поле 14:12, маска 7 << 28.
	 * 375 кГц - вход 5; 52 МГц - cpll_50m, вход 4. Это ровно те два
	 * значения, что писал прежний код, - правка не меняет поведения на
	 * eMMC платы.
	 */
	clear();
	r = sdmmc_clk_set_rate(&emmc_core, 375000, &got);
	CHECK(r == OK && got == 375000 && reg(0x170) == 0x70005000,
	    "cclk_emmc 375k: r %d got %u reg %08x", r, got, reg(0x170));
	CHECK(reg(0x324) == 0x01000000,
	    "cclk_emmc: ворота CLKGATE_CON9 бит 8 сняты: %08x", reg(0x324));
	r = sdmmc_clk_set_rate(&emmc_core, 52000000, &got);
	CHECK(r == OK && got == 50000000 && reg(0x170) == 0x70004000,
	    "cclk_emmc 52M: r %d got %u reg %08x", r, got, reg(0x170));
	r = sdmmc_clk_set_rate(&emmc_core, 200000000, &got);
	CHECK(r == OK && got == 198000000 && reg(0x170) == 0x70001000,
	    "cclk_emmc 200M: r %d got %u reg %08x", r, got, reg(0x170));
	/* Ниже самого медленного входа - самый медленный, делит хост. */
	r = sdmmc_clk_set_rate(&emmc_core, 100000, &got);
	CHECK(r == OK && got == 375000, "cclk_emmc 100k: r %d got %u", r,
	    got);

	/*
	 * clk_sdmmc0: CLKSEL_CON30 (0x178), поле 10:8, маска 7 << 24.
	 * На плате под Linux там 0x2220 - вход 2, gpll_300m, 297 МГц, ровно
	 * то, что clk_summary показывает для clk_sdmmc0.
	 */
	clear();
	r = sdmmc_clk_set_rate(&sd_ciu, 750000, &got);
	CHECK(r == OK && got == 750000 && reg(0x178) == 0x07000500,
	    "clk_sdmmc0 750k: r %d got %u reg %08x", r, got, reg(0x178));
	CHECK(reg(0x33c) == 0x00020000,
	    "clk_sdmmc0: ворота CLKGATE_CON15 бит 1: %08x", reg(0x33c));
	r = sdmmc_clk_set_rate(&sd_ciu, 50000000, &got);
	CHECK(r == OK && got == 50000000 && reg(0x178) == 0x07000400,
	    "clk_sdmmc0 50M: r %d got %u reg %08x", r, got, reg(0x178));
	r = sdmmc_clk_set_rate(&sd_ciu, 100000000, &got);
	CHECK(r == OK && got == 100000000 && reg(0x178) == 0x07000300,
	    "clk_sdmmc0 100M: r %d got %u reg %08x", r, got, reg(0x178));
	r = sdmmc_clk_set_rate(&sd_ciu, 300000000, &got);
	CHECK(r == OK && got == 297000000 && reg(0x178) == 0x07000200,
	    "clk_sdmmc0 300M: r %d got %u reg %08x", r, got, reg(0x178));
	CHECK(((0x2220 >> 8) & 7) == 2, "снимок платы: вход 2");

	/*
	 * Фазы: SDMMC0_CON0/1 (0x580/0x584), поле 11 бит с бита 1.
	 * 90 градусов - одна четверть: (0x7ff << 17) | (1 << 1).
	 * Снимок платы: 0x004 в CON0 - это две четверти, 180, как и говорит
	 * Linux.
	 */
	clear();
	r = sdmmc_clk_set_phase(&sd_drv, 90);
	CHECK(r == OK && reg(0x580) == 0x0ffe0002,
	    "drive 90: r %d reg %08x", r, reg(0x580));
	r = sdmmc_clk_set_phase(&sd_smp, 0);
	CHECK(r == OK && reg(0x584) == 0x0ffe0000,
	    "sample 0: r %d reg %08x", r, reg(0x584));
	r = sdmmc_clk_set_phase(&sd_drv, 180);
	CHECK(r == OK && (reg(0x580) & 0x7ff) == 0x004,
	    "drive 180 должен дать 0x004, как на плате: %08x", reg(0x580));
	CHECK(sdmmc_clk_set_phase(&sd_drv, 45) == EINVAL,
	    "45 градусов - не четверть, должен быть отказ");
	CHECK(sdmmc_clk_set_phase(&emmc_core, 90) == ENOSYS,
	    "фаза у такта, который не фазовый, - ENOSYS");

	/*
	 * Сбросы: SOFTRST_CON(n / 16), бит n % 16. SRST_SDMMC0 = 212:
	 * регистр 13 (0x434), бит 4.
	 */
	clear();
	r = sdmmc_reset_set(&sd_rst, 1);
	CHECK(r == OK && reg(0x434) == 0x00100010, "SRST_SDMMC0 вкл: %08x",
	    reg(0x434));
	r = sdmmc_reset_set(&sd_rst, 0);
	CHECK(r == OK && reg(0x434) == 0x00100000, "SRST_SDMMC0 выкл: %08x",
	    reg(0x434));

	/*
	 * Импульс по пяти линиям eMMC: в конце все отпущены. 0x75..0x79 -
	 * это 117..121, регистр 7 (0x41c), биты 5..9.
	 */
	clear();
	sdmmc_reset_pulse(emmc_rst, emmc_nrst);
	CHECK((reg(0x41c) & 0xffff) == 0 &&
	    (reg(0x41c) >> 16) != 0 && ((reg(0x41c) >> 16) & 0x3e0) ==
	    (reg(0x41c) >> 16),
	    "импульс eMMC: регистр 7 после - %08x", reg(0x41c));
}

/*
 * H313/H616: eMMC на mmc2 - "ahb" = CLK_BUS_MMC2 (65), "mmc" = CLK_MMC2 (62),
 * сброс RST_BUS_MMC2 (16). Регистры: модульный такт 0x838 (затвор 31,
 * вход 25:24, N 9:8, M 3:0, после них деление на 2), затворы и сбросы шины
 * 0x84c (2:0 и 18:16), без маски записи; бит сброса - «вышел из сброса».
 */
static struct sdmmc_line tx_ahb, tx_mmc, tx_rst;

static void
check_tx1_mmc2(const struct fdt_node *node)
{
	int r;

	r = sdmmc_clk_get(node, "ahb", &tx_ahb);
	CHECK(r == OK && tx_ahb.id == 65, "mmc2 ahb: r %d id %u", r, tx_ahb.id);
	r = sdmmc_clk_get(node, "mmc", &tx_mmc);
	CHECK(r == OK && tx_mmc.id == 62, "mmc2 mmc: r %d id %u", r, tx_mmc.id);
	r = sdmmc_line_get(node, "resets", "reset-names", "ahb", 0, &tx_rst);
	CHECK(r == OK && tx_rst.id == 16, "mmc2 сброс: r %d id %u", r,
	    tx_rst.id);
}

/* Что даёт слово модульного такта: вход/(M+1)/2^N/2. */
static uint32_t
mmc_rate(uint32_t v)
{
	static const uint32_t in[] = { 24000000, 1200000000, 0, 0 };

	return in[(v >> 24) & 3] / ((v & 0xf) + 1) / (1u << ((v >> 8) & 3)) /
	    2;
}

static void
check_h616_registers(void)
{
	uint32_t got;
	int r;

	memset(ccu, 0, sizeof(ccu));
	r = sdmmc_clk_set_rate(&tx_mmc, 375000, &got);
	CHECK(r == OK && got == 375000 && (creg(0x838) >> 31) &&
	    ((creg(0x838) >> 24) & 3) == 0 && mmc_rate(creg(0x838)) == 375000,
	    "mmc2 375k: r %d got %u reg %08x (%u Hz)", r, got, creg(0x838),
	    mmc_rate(creg(0x838)));
	r = sdmmc_clk_set_rate(&tx_mmc, 52000000, &got);
	CHECK(r == OK && got == 50000000 && creg(0x838) == 0x8100000b,
	    "mmc2 52M: r %d got %u reg %08x - ждали 0x8100000b "
	    "(periph0-2x, M 12, N 0)", r, got, creg(0x838));
	r = sdmmc_clk_set_rate(&tx_mmc, 150000000, &got);
	CHECK(r == OK && got == 150000000 && mmc_rate(creg(0x838)) == got,
	    "mmc2 150M: r %d got %u reg %08x", r, got, creg(0x838));
	r = sdmmc_clk_set_rate(&tx_mmc, 1000, &got);
	CHECK(r == OK && got == mmc_rate(creg(0x838)) && got > 1000,
	    "mmc2 ниже всего: r %d got %u - самый медленный, делит хост", r,
	    got);
	/* Чужие поля регистра не трогаются. */
	*(uint32_t *)(ccu + 0x838) = 0x00c00000;
	(void)sdmmc_clk_set_rate(&tx_mmc, 50000000, &got);
	CHECK((creg(0x838) & 0x00c00000) == 0x00c00000,
	    "посторонние биты 0x838 сохранены: %08x", creg(0x838));

	memset(ccu, 0, sizeof(ccu));
	*(uint32_t *)(ccu + 0x84c) = 0x00010001;	/* mmc0 жив */
	r = sdmmc_clk_enable(&tx_ahb);
	CHECK(r == OK && creg(0x84c) == 0x00010005,
	    "затвор шины mmc2: %08x, ждали 0x00010005", creg(0x84c));
	r = sdmmc_reset_set(&tx_rst, 0);
	CHECK(r == OK && creg(0x84c) == 0x00050005,
	    "mmc2 из сброса: %08x, ждали 0x00050005", creg(0x84c));
	r = sdmmc_reset_set(&tx_rst, 1);
	CHECK(r == OK && creg(0x84c) == 0x00010005,
	    "mmc2 в сброс: %08x, ждали 0x00010005 (mmc0 не задет)",
	    creg(0x84c));
	CHECK(sdmmc_clk_set_phase(&tx_mmc, 90) == ENOSYS,
	    "фаз у этого блока нет - ENOSYS");
}

static int
load(const char *path)
{
	FILE *f;
	long n;
	char *buf;

	if ((f = fopen(path, "rb")) == NULL) {
		perror(path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc((size_t)n);
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "не прочитать %s\n", path);
		return -1;
	}
	fclose(f);
	if (!fdt_valid(buf)) {
		fprintf(stderr, "%s - не дерево\n", path);
		return -1;
	}
	dtb = buf;
	return 0;
}

int
main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "live.dtb";
	FILE *f;
	long n;
	char *buf;

	if (argc > 2 && strcmp(argv[2], "tx1") == 0) {
		if (load(path) != 0)
			return 2;
		printf("Линии eMMC H313 по дереву Tanix TX1\n");
		with_node("mmc@4022000", check_tx1_mmc2);
		printf("Слова в регистрах CCU H616\n");
		check_h616_registers();
		CHECK(ccu_maps == 1, "CCU отображён %u раз, ждали один",
		    ccu_maps);
		printf("\n%u проверок, %u отказов\n", checks, failures);
		return failures ? 1 : 0;
	}

	if ((f = fopen(path, "rb")) == NULL) {
		perror(path);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc((size_t)n);
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "не прочитать %s\n", path);
		return 2;
	}
	fclose(f);
	dtb = buf;
	if (!fdt_valid(dtb)) {
		fprintf(stderr, "%s - не дерево\n", path);
		return 2;
	}

	printf("Линии eMMC по дереву платы\n");
	with_node("mmc@fe310000", check_emmc);
	printf("Линии SD по дереву платы\n");
	with_node("mmc@fe2b0000", check_sd);
	printf("Чужой держатель (PMUCRU)\n");
	with_node("i2c@fdd40000", check_pmucru);
	printf("Слова в регистрах CRU\n");
	check_registers();
	CHECK(maps == 1, "CRU отображён %u раз, ждали один", maps);

	printf("\n%u проверок, %u отказов\n", checks, failures);
	return failures ? 1 : 0;
}
