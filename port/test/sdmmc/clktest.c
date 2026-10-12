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

void *
vm_map_phys(int who, void *phys, size_t len)
{
	(void)who;
	maps++;
	if ((unsigned long)phys == CRU_BASE && len == CRU_SIZE)
		return cru;
	printf("  ОТКАЗ: отображение 0x%lx+0x%zx - не CRU\n",
	    (unsigned long)phys, len);
	failures++;
	return (void *)-1;
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

int
main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "live.dtb";
	FILE *f;
	long n;
	char *buf;

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
