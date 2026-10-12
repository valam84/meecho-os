/*
 * Стенд выбора контроллера: sdmmc_host.c на настоящем дереве CB2.
 *
 * Номер экземпляра обязан называть ровно один узел, и всегда один и тот же.
 * Первая версия обхода считала узел столько раз, сколько строк таблицы он
 * совпал, - у eMMC платы их две, - и instance=1 взял eMMC второй раз, под
 * работающим корнем (PORTING-LOG.md, «Второй экземпляр на том же узле»).
 * Здесь проверяется, кто достаётся каждому номеру: распаянные носители
 * первыми, потом гнёзда, слот SDIO не считается вовсе, и отказ драйвера от
 * своего узла - отказ экземпляра, а не переход к соседнему.
 *
 * Пробы драйверов подменены: стенду нужно знать, какой узел и чьей пробе
 * достался, а не что пробы делают с регистрами.
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
#include <minix/sysutil.h>

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

static const char *dtb_file;
static long instance;
static const char *probed_by;
static phys_bytes probed_base;
static unsigned probes;
static int refuse;
static struct sdmmc_devinfo probed_info;

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

int
env_parse(const char *name, const char *fmt, int field, long *param,
	long min, long max)
{
	(void)fmt; (void)field; (void)min; (void)max;
	if (strcmp(name, "instance") == 0) {
		*param = instance;
		return EP_SET;
	}
	return 0;
}

static int
record(const char *who, const struct sdmmc_devinfo *info)
{
	probes++;
	probed_by = who;
	probed_base = info->base;
	probed_info = *info;
	return refuse ? ENXIO : OK;
}

int
sdhci_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	(void)node; (void)host;
	return record("sdhci", info);
}

int
dwmshc_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	(void)node; (void)host;
	return record("dwmshc", info);
}

int
sunxi_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	(void)node; (void)host;
	return record("sunxi", info);
}

/* Нужны sdmmc_clk.c, но этот стенд регистров не трогает. */
void *vm_map_phys(int who, void *phys, size_t len)
	{ (void)who; (void)phys; (void)len; return (void *)-1; }
int vm_unmap_phys(int who, void *vir, size_t len)
	{ (void)who; (void)vir; (void)len; return 0; }
void micro_delay(unsigned long us) { (void)us; }

static int
find(long inst)
{
	struct sdmmc_host host;

	memset(&host, 0, sizeof(host));
	instance = inst;
	probed_by = NULL;
	probed_base = 0;
	probes = 0;
	return sdmmc_host_find(&host);
}

/*
 * Tanix TX1, H313 (дерево mainline, port/h313/h313-tanix-tx1.dtb). eMMC на
 * mmc2, Wi-Fi SDIO на mmc1 - распаян и без "no-sd"/"no-mmc", только с узлом
 * wifi@1 под ним; гнездо mmc0 выключено. Номер 0 обязан достаться eMMC, а
 * не слоту Wi-Fi, который в порядке дерева идёт раньше.
 */
static void
tx1(void)
{
	int r;

	printf("H313: instance=0 - eMMC на mmc2, не Wi-Fi на mmc1\n");
	r = find(0);
	CHECK(r == OK && probes == 1, "r %d, проб %u", r, probes);
	CHECK(probed_base == 0x4022000 && probed_by != NULL &&
	    strcmp(probed_by, "sunxi") == 0,
	    "достался 0x%lx пробе %s, ждали eMMC 0x4022000 у sunxi",
	    (unsigned long)probed_base, probed_by ? probed_by : "-");
	CHECK(probed_info.non_removable && probed_info.bus_width == 8 &&
	    probed_info.max_freq == 100000000 && probed_info.nresets == 1 &&
	    probed_info.reset[0].id == 16 && probed_info.irq == 32 + 37,
	    "сведения eMMC: несъёмная %d, шина %u, %u Гц, сбросов %u (%u), "
	    "линия %d", probed_info.non_removable, probed_info.bus_width,
	    probed_info.max_freq, probed_info.nresets,
	    probed_info.reset[0].id, probed_info.irq);

	printf("H313: instance=1 - больше дисков нет\n");
	r = find(1);
	CHECK(r == ENXIO && probes == 0,
	    "r %d, проб %u - mmc0 выключен, mmc1 - SDIO", r, probes);
}

int
main(int argc, char **argv)
{
	int r;

	dtb_file = (argc > 1) ? argv[1] : "live.dtb";
	if (argc > 2 && strcmp(argv[2], "tx1") == 0) {
		tx1();
		printf("\n%u проверок, %u отказов\n", checks, failures);
		return failures ? 1 : 0;
	}

	printf("instance=0: распаянная eMMC\n");
	r = find(0);
	CHECK(r == OK && probes == 1, "r %d, проб %u", r, probes);
	CHECK(probed_base == 0xfe310000 && probed_by != NULL &&
	    strcmp(probed_by, "sdhci") == 0,
	    "достался 0x%lx пробе %s, ждали eMMC 0xfe310000 у sdhci",
	    (unsigned long)probed_base, probed_by ? probed_by : "-");
	CHECK(probed_info.non_removable && probed_info.bus_width == 8 &&
	    probed_info.nresets == 5 && probed_info.irq == 32 + 0x13,
	    "сведения eMMC: несъёмная %d, шина %u, сбросов %u, линия %d",
	    probed_info.non_removable, probed_info.bus_width,
	    probed_info.nresets, probed_info.irq);

	printf("instance=1: гнездо SD, а не eMMC второй раз\n");
	r = find(1);
	CHECK(r == OK && probes == 1, "r %d, проб %u", r, probes);
	CHECK(probed_base == 0xfe2b0000 && probed_by != NULL &&
	    strcmp(probed_by, "dwmshc") == 0,
	    "достался 0x%lx пробе %s, ждали гнездо 0xfe2b0000 у dwmshc",
	    (unsigned long)probed_base, probed_by ? probed_by : "-");
	CHECK(!probed_info.non_removable && probed_info.bus_width == 4 &&
	    probed_info.cap_sd_hs && probed_info.nresets == 1 &&
	    probed_info.reset[0].id == 212 && probed_info.irq == 32 + 0x62,
	    "сведения гнезда: несъёмное %d, шина %u, hs %d, сбросов %u "
	    "(%u), линия %d", probed_info.non_removable,
	    probed_info.bus_width, probed_info.cap_sd_hs,
	    probed_info.nresets, probed_info.reset[0].id, probed_info.irq);

	printf("instance=2: слота SDIO как диска нет\n");
	r = find(2);
	CHECK(r == ENXIO && probes == 0,
	    "r %d, проб %u - слот Wi-Fi (no-sd, no-mmc) не диск", r, probes);

	printf("Отказ пробы - отказ экземпляра, не соседний узел\n");
	refuse = 1;
	r = find(0);
	CHECK(r == ENXIO && probes == 1,
	    "r %d, проб %u - после отказа eMMC искать дальше нельзя", r,
	    probes);
	refuse = 0;

	printf("\n%u проверок, %u отказов\n", checks, failures);
	return failures ? 1 : 0;
}
