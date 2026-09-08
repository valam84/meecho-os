/*
 * Аппаратный генератор случайных чисел, найденный в дереве устройств.
 *
 * Зачем он вообще понадобился. У этого порта единственным источником
 * энтропии были времена прихода прерываний, и на плате этого не хватает:
 * ядро отдаёт накопитель источника только заполненным целиком (64 отсчёта,
 * см. GET_RANDOMNESS_BIN в do_getinfo.c), а в пул нулевого уровня попадает
 * один отсчёт из тридцати двух, так что до порога в 256 отсчётов нужно
 * около восьми тысяч прерываний. Измерено на CB2 2026-09-08: за минуту
 * работы с поднятой сетью и корнем на eMMC /dev/random так и отвечал
 * EAGAIN. Машина не производит столько событий, и ждать нечего - нужен
 * настоящий источник.
 *
 * Что здесь поддержано: TRNG внутри криптоблока RK356x. Раскладка регистров
 * и, главное, доказательство того, что блок вообще отвечает, - в
 * port/cb2-trng/reference.txt: последовательность снята с ЖИВОЙ платы через
 * /dev/mem вендорского Linux, прежде чем был написан этот файл.
 *
 * Две вещи из той пробы, которые определяют весь код ниже:
 *
 * 1. Ни такты, ни сброс трогать не нужно - загрузчик оставляет блок
 *    затактированным и не в сбросе. Поэтому здесь нет ни CRU, ни линий
 *    сброса, хотя узел в дереве их называет.
 * 2. Узел на этой плате стоит status = "disabled", и это сознательно НЕ
 *    учитывается. Строка означает, что вендор не подключил к блоку свой
 *    драйвер, а не что блока нет: та же проба показала четыре разных
 *    256-битных значения по этим адресам. RS, раздающий права по
 *    "devicetree" в system.conf, на status тоже не смотрит.
 *
 * Чего этот файл не утверждает: качества чисел. Они идут в пул как
 * источник, наравне с временами прерываний, а не как готовый поток.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/type.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "random.h"
#include "trng.h"

#if defined(__aarch64__)

static struct log log = {
	.name = "trng",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* Регистры, считая от начала блока. */
#define TRNG_CTL		0x0400
#define TRNG_SAMPLE_CNT		0x0404
#define TRNG_DOUT		0x0410

#define TRNG_CTL_START		(1 << 0)
#define TRNG_CTL_ENABLE		(1 << 1)
#define TRNG_CTL_LEN_256	(0x3 << 4)

#define TRNG_DOUT_BYTES		32
#define TRNG_MIN_SIZE		(TRNG_DOUT + TRNG_DOUT_BYTES)

/* Период выборки; то же число, что пишет драйвер mainline. */
#define TRNG_SAMPLE_PERIOD	1000

/*
 * Сколько раз опрашивать снятие START. На плате он снимается за доли
 * миллисекунды; предел здесь только затем, чтобы неисправный блок не
 * остановил драйвер навсегда.
 */
#define TRNG_POLL_MAX		100000

static const char *const trng_compatible[] = {
	"rockchip,cryptov2-rng",	/* так называет его дерево платы */
	"rockchip,rk3568-rng",		/* так - дерево mainline */
	NULL
};

static phys_bytes trng_base;
static size_t trng_reg_size;
static vir_bytes trng_regs;

static u32_t
reg_read(unsigned off)
{
	return *(volatile u32_t *)(trng_regs + off);
}

static void
reg_write(unsigned off, u32_t val)
{
	*(volatile u32_t *)(trng_regs + off) = val;
}

/*
 * Запись с маской: у этих регистров, как и у CRU, старшие шестнадцать бит
 * говорят, какие младшие менять. Без маски запись не доходит.
 */
static void
reg_write_masked(unsigned off, u32_t val, u32_t mask)
{
	reg_write(off, (mask << 16) | (val & 0xffff));
}

static int
find_trng(void *cookie, int depth, const char *UNUSED(name),
	const struct fdt_node *node)
{
	u64_t base, size;
	unsigned i;
	int *found = cookie;

	if (depth == 0)
		return 0;

	for (i = 0; trng_compatible[i] != NULL; i++) {
		if (!fdt_node_is_compatible(node, trng_compatible[i]))
			continue;
		if (fdt_node_reg(node, 0, &base, &size) != 0)
			continue;
		if (size < TRNG_MIN_SIZE)
			continue;

		trng_base = (phys_bytes)base;
		trng_reg_size = (size_t)size;
		*found = 1;
		return 1;	/* обход останавливается */
	}
	return 0;
}

/*
 * Взять одну порцию. Возвращает число прочитанных байт (0, если блок не
 * ответил в срок).
 */
static size_t
trng_read(u8_t *buf, size_t len)
{
	unsigned i;
	size_t n;

	if (trng_regs == 0)
		return 0;
	if (len > TRNG_DOUT_BYTES)
		len = TRNG_DOUT_BYTES;

	reg_write(TRNG_SAMPLE_CNT, TRNG_SAMPLE_PERIOD);
	reg_write_masked(TRNG_CTL,
	    TRNG_CTL_LEN_256 | TRNG_CTL_ENABLE | TRNG_CTL_START,
	    TRNG_CTL_LEN_256 | TRNG_CTL_ENABLE | TRNG_CTL_START);

	for (i = 0; i < TRNG_POLL_MAX; i++) {
		if ((reg_read(TRNG_CTL) & TRNG_CTL_START) == 0)
			break;
	}
	if (i == TRNG_POLL_MAX) {
		log_warn(&log, "generator did not finish; giving up on it\n");
		reg_write_masked(TRNG_CTL, 0,
		    TRNG_CTL_LEN_256 | TRNG_CTL_ENABLE | TRNG_CTL_START);
		return 0;
	}

	for (n = 0; n < len; n++)
		buf[n] = *(volatile u8_t *)(trng_regs + TRNG_DOUT + n);

	/* Кольцо генератора выключается до следующего раза. */
	reg_write_masked(TRNG_CTL, 0,
	    TRNG_CTL_LEN_256 | TRNG_CTL_ENABLE | TRNG_CTL_START);

	return n;
}

int
trng_init(void)
{
	u8_t buf[TRNG_DOUT_BYTES];
	void *dtb, *v;
	int found = 0;
	size_t n;

	if ((dtb = fdt_fetch()) != NULL) {
		(void)fdt_walk(dtb, find_trng, &found);
		free(dtb);
	}

	if (!found) {
		log_info(&log, "no hardware random source on this machine\n");
		return ENODEV;
	}

	v = vm_map_phys(SELF, (void *)trng_base, trng_reg_size);
	if (v == MAP_FAILED) {
		log_warn(&log, "cannot map the registers at 0x%lx; "
		    "not granted by RS?\n", (unsigned long)trng_base);
		return EPERM;
	}
	trng_regs = (vir_bytes)v;

	/*
	 * Первая порция - до всего остального: именно она снимает вопрос
	 * "система загрузилась, а /dev/random ещё не посеян".
	 */
	n = trng_read(buf, sizeof(buf));
	if (n == 0) {
		log_warn(&log, "found at 0x%lx but it returned nothing\n",
		    (unsigned long)trng_base);
		return EIO;
	}

	random_putbytes(buf, n);
	memset(buf, 0, sizeof(buf));

	log_info(&log, "hardware random source at 0x%lx, %u bytes per read\n",
	    (unsigned long)trng_base, (unsigned)n);
	return OK;
}

size_t
trng_feed(void)
{
	u8_t buf[TRNG_DOUT_BYTES];
	size_t n;

	if (trng_regs == 0)
		return 0;

	n = trng_read(buf, sizeof(buf));
	if (n > 0)
		random_putbytes(buf, n);
	memset(buf, 0, sizeof(buf));
	return n;
}

#else /* !__aarch64__ */

/*
 * У прочих машин порта аппаратного источника нет. Это не заглушка, а
 * ответ: пул живёт на временах прихода прерываний, как и раньше.
 */
int
trng_init(void)
{
	return ENODEV;
}

size_t
trng_feed(void)
{
	return 0;
}

#endif /* !__aarch64__ */
