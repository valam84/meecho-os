/*
 * Хостовый стенд для слоя карты драйвера sdmmc.
 *
 * Зачем. Драйвер пишется вслепую: платы под рукой нет, а QEMU virt никакого
 * SD-контроллера не эмулирует вовсе, так что «собралось» — это всё, что
 * даёт эмулятор. Между тем слой карты уже разделён ровно там, где нужно:
 * он ходит к контроллеру через таблицу функций struct sdmmc_host. Значит
 * его можно прогнать целиком на хосте, подставив вместо контроллера модель,
 * и ни строчки в драйвере под это менять не надо.
 *
 * Что это проверяет и чего не проверяет. Модель отвечает так, как я понял
 * спецификацию, поэтому непонимание железа она поймать не может — это
 * ограничение принципиальное и его стоит помнить. Что она ловит: порядок
 * опознания, разбор CID и CSD, вычисление ёмкости, адресацию секторами
 * против байтовой, последовательность переключений шины и скорости,
 * границы, и то, что flush — это именно CMD6 на FLUSH_CACHE.
 *
 * Регистры карты в модели — настоящие, снятые с eMMC целевой платы
 * (BIGTREETECH CB2, Kingston PJ3032) через /sys и mmc extcsd. Поэтому
 * «61079552 секторов» и «PJ3032» — это не выдуманные числа: ровно их должен
 * получить драйвер на плате.
 *
 *	cc -I stubs -I <дерево>/minix/drivers/storage/mmc \
 *	   cardtest.c <дерево>/minix/drivers/storage/mmc/sdmmc_card.c
 */

#include <errno.h>
#include <limits.h>	/* sdmmcreg.h's __bitfield() wants UINT_MAX */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>
#include <minix/log.h>

#include "sdmmc.h"
#include "sdmmcreg.h"

int stub_log_level = LEVEL_WARN;

struct log sdmmc_log = { "sdmmc", LEVEL_INFO, NULL };

void
micro_delay(unsigned long UNUSED(micros))
{
}
/* Стенд не передаёт драйверу аргументов: пределы остаются по умолчанию. */
int
env_parse(const char *UNUSED(name), const char *UNUSED(fmt), int UNUSED(field),
	long *UNUSED(param), long UNUSED(lo), long UNUSED(hi))
{
	return 0;
}

int
fdt_node_is_compatible(const struct fdt_node *UNUSED(n),
	const char *UNUSED(w))
{
	return 0;
}

/* ------------------------------------------------------------------ */
/* Модель карты						                              */
/* ------------------------------------------------------------------ */

#define ST_IDLE	0
#define ST_READY	1
#define ST_IDENT	2
#define ST_STBY	3
#define ST_TRAN	4

#define MEDIUM_SECTORS	128

/* Окно напряжений, которое карта объявляет в OCR: 2.7-3.6 В. */
#define OCR_TEST_WINDOW	0x00ff8000

struct model {
	int		is_sd;
	int		sd_v2;
	int		hc;		/* отвечает HCS: адресация секторами */
	uint32_t	cid[4];
	uint32_t	csd[4];
	uint8_t		ext_csd[512];
	uint32_t	rca;
	int		state;
	int		ocr_polls;	/* сколько раз ответить «занят» */
	int		app_next;	/* был CMD55 */
	uint8_t		medium[MEDIUM_SECTORS * SDMMC_SECTOR_SIZE];

	/* запись того, что спросили */
	struct { uint8_t index; uint32_t arg; } seen[128];
	int		nseen;
	uint32_t	last_data_arg;
	uint32_t	last_blocks;
	int		last_stop;
	phys_bytes	last_data_phys;
};

static struct model m;
static unsigned host_clock;
static unsigned host_width;
static int host_hs;

static void
record(uint8_t index, uint32_t arg)
{
	if (m.nseen < (int)(sizeof(m.seen) / sizeof(m.seen[0]))) {
		m.seen[m.nseen].index = index;
		m.seen[m.nseen].arg = arg;
		m.nseen++;
	}
}

/* Сколько раз встречалась команда. */
static int
count_cmd(uint8_t index)
{
	int i, n = 0;

	for (i = 0; i < m.nseen; i++)
		if (m.seen[i].index == index)
			n++;
	return n;
}

/* Аргумент n-й (с нуля) встречи команды, или ~0 если её не было. */
static uint32_t
arg_of(uint8_t index, int nth)
{
	int i;

	for (i = 0; i < m.nseen; i++)
		if (m.seen[i].index == index && nth-- == 0)
			return m.seen[i].arg;
	return ~0u;
}

static uint32_t
r1_status(void)
{
	return MMC_R1_READY_FOR_DATA | ((uint32_t)m.state << 9);
}

static int
do_switch(uint32_t arg)
{
	uint8_t index = (arg >> 16) & 0xff;
	uint8_t value = (arg >> 8) & 0xff;

	if (((arg >> 24) & 0x3) != MMC_SWITCH_MODE_WRITE_BYTE)
		return EIO;
	m.ext_csd[index] = value;
	return OK;
}

static int
do_data(struct sdmmc_cmd *c)
{
	uint64_t sector;
	uint32_t i;
	uint8_t *p;

	sector = m.hc ? c->arg : (c->arg / SDMMC_SECTOR_SIZE);
	m.last_data_arg = c->arg;
	m.last_blocks = c->blocks;
	m.last_stop = c->stop;
	m.last_data_phys = c->data_phys;

	if (sector + c->blocks > MEDIUM_SECTORS) {
		printf("model: transfer past the modelled medium\n");
		return EIO;
	}

	p = m.medium + sector * SDMMC_SECTOR_SIZE;
	for (i = 0; i < c->blocks; i++) {
		if (c->data_dir == SDMMC_DATA_READ)
			memcpy((uint8_t *)c->data + i * c->blocklen,
			    p + i * SDMMC_SECTOR_SIZE, c->blocklen);
		else
			memcpy(p + i * SDMMC_SECTOR_SIZE,
			    (uint8_t *)c->data + i * c->blocklen, c->blocklen);
	}
	return OK;
}

static int
model_command(struct sdmmc_cmd *c)
{
	int app = m.app_next;

	m.app_next = 0;
	record(c->index, c->arg);
	memset(c->resp, 0, sizeof(c->resp));

	if (app) {
		switch (c->index) {
		case SD_APP_OP_COND:
			if (!m.is_sd)
				return ETIMEDOUT;
			c->resp[0] = OCR_TEST_WINDOW;
			if (m.ocr_polls-- <= 0) {
				c->resp[0] |= MMC_OCR_MEM_READY;
				if (m.hc)
					c->resp[0] |= MMC_OCR_HCS;
				m.state = ST_READY;
			}
			return OK;
		case SD_APP_SET_BUS_WIDTH:
			c->resp[0] = r1_status();
			return OK;
		default:
			return ETIMEDOUT;
		}
	}

	switch (c->index) {
	case MMC_GO_IDLE_STATE:
		m.state = ST_IDLE;
		m.ext_csd[EXT_CSD_BUS_WIDTH] = 0;
		m.ext_csd[EXT_CSD_HS_TIMING] = 0;
		return OK;

	case MMC_SEND_OP_COND:		/* CMD1, только eMMC */
		if (m.is_sd)
			return ETIMEDOUT;
		c->resp[0] = OCR_TEST_WINDOW;
		if (c->arg == 0)
			return OK;	/* первый CMD1 только спрашивает */
		if (m.ocr_polls-- <= 0) {
			c->resp[0] |= MMC_OCR_MEM_READY;
			if (m.hc)
				c->resp[0] |= MMC_OCR_HCS;
			m.state = ST_READY;
		}
		return OK;

	case MMC_ALL_SEND_CID:
		if (m.state != ST_READY)
			return ETIMEDOUT;
		memcpy(c->resp, m.cid, sizeof(m.cid));
		m.state = ST_IDENT;
		return OK;

	case MMC_SET_RELATIVE_ADDR:	/* CMD3: у SD это SD_SEND_RELATIVE_ADDR */
		if (m.is_sd) {
			c->resp[0] = (m.rca << 16) | 0x0500;
		} else {
			m.rca = c->arg >> 16;
			c->resp[0] = r1_status();
		}
		m.state = ST_STBY;
		return OK;

	case MMC_SEND_CSD:
		memcpy(c->resp, m.csd, sizeof(m.csd));
		return OK;

	case MMC_SELECT_CARD:
		m.state = ST_TRAN;
		c->resp[0] = r1_status();
		return OK;

	case MMC_SEND_EXT_CSD:		/* CMD8; у SD в idle это SEND_IF_COND */
		if (m.state == ST_IDLE) {
			if (!m.is_sd || !m.sd_v2)
				return ETIMEDOUT;
			c->resp[0] = c->arg & 0xfff;
			return OK;
		}
		if (c->data_dir != SDMMC_DATA_READ || c->blocks != 1)
			return EIO;
		memcpy(c->data, m.ext_csd, sizeof(m.ext_csd));
		c->resp[0] = r1_status();
		return OK;

	case MMC_SWITCH:
		if (do_switch(c->arg) != OK)
			return EIO;
		c->resp[0] = r1_status();
		return OK;

	case MMC_SEND_STATUS:
		c->resp[0] = r1_status();
		return OK;

	case MMC_SET_BLOCKLEN:
		c->resp[0] = r1_status();
		return OK;

	case MMC_SET_BLOCK_COUNT:
		c->resp[0] = r1_status();
		return OK;

	case MMC_APP_CMD:
		if (!m.is_sd)
			return ETIMEDOUT;
		m.app_next = 1;
		c->resp[0] = r1_status() | MMC_R1_APP_CMD;
		return OK;

	case MMC_READ_BLOCK_SINGLE:
	case MMC_READ_BLOCK_MULTIPLE:
	case MMC_WRITE_BLOCK_SINGLE:
	case MMC_WRITE_BLOCK_MULTIPLE:
		if (do_data(c) != OK)
			return EIO;
		c->resp[0] = r1_status();
		return OK;

	default:
		return ETIMEDOUT;
	}
}

/* ------------------------------------------------------------------ */
/* Модель контроллера: она же таблица функций хоста                   */
/* ------------------------------------------------------------------ */

static int host_init(void) { return OK; }
static void host_exit(void) { }

static int
host_set_clock(uint32_t hz, uint32_t *actual)
{
	host_clock = hz;
	if (actual != NULL)
		*actual = hz;
	return OK;
}

static int
host_set_bus_width(unsigned bits)
{
	host_width = bits;
	return OK;
}

static int
host_set_timing(int hs)
{
	host_hs = hs;
	return OK;
}

static struct sdmmc_host mock = {
	.name = "model",
	.init = host_init,
	.exit = host_exit,
	.set_clock = host_set_clock,
	.set_bus_width = host_set_bus_width,
	.set_timing = host_set_timing,
	.command = model_command,
	.max_bus_width = 8,
	.max_freq = 200000000,
	/*
	 * Два потолка, как у настоящего контроллера с ADMA2: длинный, когда
	 * буфер можно отдать движку, и короткий (буфер FIFO этой части),
	 * когда нельзя. Числа разные нарочно — по ним и видно, какой из них
	 * слой карты применил.
	 */
	.max_blocks = 64,
	.max_blocks_pio = 4
};

/* ------------------------------------------------------------------ */
/* Проверки							                              */
/* ------------------------------------------------------------------ */

static int failures;
static int checks;

static void
check(int ok, const char *what, const char *detail)
{
	checks++;
	if (ok)
		return;
	failures++;
	printf("  ОТКАЗ: %s%s%s\n", what, detail ? " — " : "",
	    detail ? detail : "");
}

static void
check_u64(uint64_t got, uint64_t want, const char *what)
{
	char buf[128];

	if (got == want) {
		checks++;
		return;
	}
	snprintf(buf, sizeof(buf), "получено %llu, ожидалось %llu",
	    (unsigned long long)got, (unsigned long long)want);
	check(0, what, buf);
}

/* Настоящие регистры eMMC целевой платы. */
static void
board_emmc(void)
{
	memset(&m, 0, sizeof(m));
	m.is_sd = 0;
	m.hc = 1;
	m.ocr_polls = 2;

	/* CID 700100504a333033320230161381cb00, старший байт первым. */
	m.cid[3] = 0x70010050;
	m.cid[2] = 0x4a333033;
	m.cid[1] = 0x32023016;
	m.cid[0] = 0x1381cb00;

	/* CSD d02701320f5903ffffffffef8a400000. */
	m.csd[3] = 0xd0270132;
	m.csd[2] = 0x0f5903ff;
	m.csd[1] = 0xffffffef;
	m.csd[0] = 0x8a400000;

	/* SEC_COUNT 0x03a40000, CARD_TYPE 0x57, CACHE_SIZE 128 KiB. */
	m.ext_csd[EXT_CSD_SEC_COUNT + 0] = 0x00;
	m.ext_csd[EXT_CSD_SEC_COUNT + 1] = 0x00;
	m.ext_csd[EXT_CSD_SEC_COUNT + 2] = 0xa4;
	m.ext_csd[EXT_CSD_SEC_COUNT + 3] = 0x03;
	m.ext_csd[EXT_CSD_CARD_TYPE] = 0x57;
	m.ext_csd[EXT_CSD_CACHE_SIZE + 0] = 0x80;
	m.ext_csd[EXT_CSD_REV] = 8;
}

static void
fill_medium(void)
{
	unsigned i;

	for (i = 0; i < sizeof(m.medium); i++)
		m.medium[i] = (uint8_t)(i * 7 + (i >> 9));
}

static void
test_emmc(void)
{
	struct sdmmc_card card;
	static uint8_t big[10 * SDMMC_SECTOR_SIZE];
	uint8_t buf[4 * SDMMC_SECTOR_SIZE];
	int r;

	printf("eMMC целевой платы\n");
	board_emmc();
	fill_medium();
	host_clock = 0; host_width = 0; host_hs = -1;

	r = sdmmc_card_init(&mock, &card);
	check(r == OK, "опознание завершилось", NULL);
	if (r != OK)
		return;

	check(!card.is_sd, "опознан как eMMC, а не SD", NULL);
	check(card.sector_addressed, "адресация секторами", NULL);
	check_u64(card.sectors, 61079552, "ёмкость из SEC_COUNT");
	check(strcmp(card.name, "PJ3032") == 0, "имя из CID", card.name);
	check_u64(card.bus_width, 8, "шина расширена до восьми бит");
	check_u64(host_width, 8, "контроллер переведён на восемь бит");
	check(card.high_speed, "включён high speed", NULL);
	check_u64(card.clock, 52000000, "такт поднят до 52 МГц");
	check(host_hs == 1, "контроллеру сказано про high speed", NULL);
	check(card.cache_on, "кэш карты включён", NULL);

	check_u64(m.ext_csd[EXT_CSD_BUS_WIDTH], EXT_CSD_BUS_WIDTH_8,
	    "карте записана ширина шины");
	check_u64(m.ext_csd[EXT_CSD_HS_TIMING], 1,
	    "карте записан HS_TIMING");
	check_u64(m.ext_csd[EXT_CSD_CACHE_CTRL], 1,
	    "карте записан CACHE_CTRL");

	/* Порядок опознания: CMD1 после того, как ветка SD не ответила. */
	check(count_cmd(SD_SEND_IF_COND) >= 1,
	    "CMD8 задан (ветка SD пробуется первой)", NULL);
	check(count_cmd(MMC_GO_IDLE_STATE) == 4,
	    "CMD0 послан дважды до ветки SD и дважды после", NULL);
	check_u64(arg_of(MMC_GO_IDLE_STATE, 0), 0xf0f0f0f0,
	    "первым идёт GO_PRE_IDLE, вторым обычный CMD0");
	check_u64(arg_of(MMC_GO_IDLE_STATE, 1), 0, "и обычный CMD0 за ним");
	check(count_cmd(MMC_SEND_OP_COND) >= 2,
	    "CMD1 повторялся до готовности", NULL);
	check_u64(arg_of(MMC_SEND_OP_COND, 0), 0,
	    "первый CMD1 только спрашивает OCR");
	check(count_cmd(MMC_SELECT_CARD) == 1, "CMD7 послан один раз", NULL);
	check_u64(arg_of(MMC_SELECT_CARD, 0), 1u << 16,
	    "CMD7 адресован тем RCA, который назначил хост");

	/* Чтение одного сектора: CMD17, аргумент — номер сектора. */
	m.nseen = 0;
	memset(buf, 0, sizeof(buf));
	r = sdmmc_card_read(10, 1, buf, 0);
	check(r == OK, "чтение одного сектора", NULL);
	check(count_cmd(MMC_READ_BLOCK_SINGLE) == 1, "это был CMD17", NULL);
	check_u64(m.last_data_arg, 10, "аргумент — номер сектора");
	check(memcmp(buf, m.medium + 10 * SDMMC_SECTOR_SIZE,
	    SDMMC_SECTOR_SIZE) == 0, "прочитано то, что лежит", NULL);
	check(m.last_stop == 0, "одиночное чтение не просит CMD12", NULL);

	/* Четыре сектора: CMD18 и остановка. */
	m.nseen = 0;
	memset(buf, 0, sizeof(buf));
	r = sdmmc_card_read(20, 4, buf, 0);
	check(r == OK, "чтение четырёх секторов", NULL);
	check(count_cmd(MMC_READ_BLOCK_MULTIPLE) == 1, "это был CMD18", NULL);
	check_u64(m.last_blocks, 4, "запрошено четыре блока");
	check(count_cmd(MMC_READ_BLOCK_MULTIPLE) == 1,
	    "и это была одна команда: четыре блока в предел укладываются", NULL);
	check(m.last_stop == 0, "многоблочное чтение не просит CMD12", NULL);
	check(count_cmd(MMC_SET_BLOCK_COUNT) == 1,
	    "вместо него послан CMD23", NULL);
	check_u64(arg_of(MMC_SET_BLOCK_COUNT, 0), 4,
	    "и в нём число блоков");
	check(memcmp(buf, m.medium + 20 * SDMMC_SECTOR_SIZE,
	    4 * SDMMC_SECTOR_SIZE) == 0, "содержимое совпало", NULL);

	/* Длиннее предела контроллера: одна просьба, несколько команд. */
	m.nseen = 0;
	r = sdmmc_card_read(40, 10, big, 0);
	check(r == OK, "чтение десяти секторов", NULL);
	check_u64((uint64_t)(count_cmd(MMC_READ_BLOCK_MULTIPLE) + count_cmd(MMC_READ_BLOCK_SINGLE)),
	    3, "разложено на три команды: 4 + 4 + 2");
	check(memcmp(big, m.medium + 40 * SDMMC_SECTOR_SIZE,
	    10 * SDMMC_SECTOR_SIZE) == 0, "и склеилось верно", NULL);

	/*
	 * То же самое, но с физическим адресом: буфер, который контроллер
	 * может забрать сам, режется по длинному потолку, а не по короткому.
	 *
	 * Решение принимает слой карты, а не контроллер — контроллер лишь
	 * смотрит на data_phys. Ошибка в эту сторону не ломает ничего
	 * заметного: она просто оставляет потолок FIFO на месте, то есть
	 * тихо отменяет всю веху.
	 */
	m.nseen = 0;
	memset(big, 0, 10 * SDMMC_SECTOR_SIZE);
	r = sdmmc_card_read(40, 10, big, 0x40200000UL);
	check(r == OK, "то же чтение с физическим адресом", NULL);
	check_u64((uint64_t)(count_cmd(MMC_READ_BLOCK_MULTIPLE) +
	    count_cmd(MMC_READ_BLOCK_SINGLE)), 1,
	    "одна команда: потолок FIFO к этому буферу не относится");
	check_u64(m.last_blocks, 10, "все десять блоков в ней");
	check_u64(m.last_data_phys, 0x40200000UL,
	    "и физический адрес дошёл до контроллера");
	check(memcmp(big, m.medium + 40 * SDMMC_SECTOR_SIZE,
	    10 * SDMMC_SECTOR_SIZE) == 0, "содержимое то же", NULL);

	/* И он сдвигается вместе с указателем, когда команд всё-таки много. */
	m.nseen = 0;
	mock.max_blocks = 4;
	r = sdmmc_card_read(40, 10, big, 0x40200000UL);
	mock.max_blocks = 64;
	check(r == OK, "чтение с коротким потолком и физическим адресом", NULL);
	check_u64(m.last_data_phys, 0x40200000UL + 8 * SDMMC_SECTOR_SIZE,
	    "последняя команда получила адрес своего куска");

	/* Запись. */
	m.nseen = 0;
	memset(buf, 0xa5, sizeof(buf));
	r = sdmmc_card_write(30, 2, buf, 0);
	check(r == OK, "запись двух секторов", NULL);
	check(count_cmd(MMC_WRITE_BLOCK_MULTIPLE) == 1, "это был CMD25", NULL);
	check(memcmp(m.medium + 30 * SDMMC_SECTOR_SIZE, buf,
	    2 * SDMMC_SECTOR_SIZE) == 0, "записанное легло на место", NULL);

	m.nseen = 0;
	r = sdmmc_card_write(33, 1, buf, 0);
	check(r == OK, "запись одного сектора", NULL);
	check(count_cmd(MMC_WRITE_BLOCK_SINGLE) == 1, "это был CMD24", NULL);

	/* За краем карты. */
	r = sdmmc_card_read(card.sectors - 1, 4, buf, 0);
	check(r == EINVAL, "чтение за краем отвергнуто", NULL);
	r = sdmmc_card_write(card.sectors, 1, buf, 0);
	check(r == EINVAL, "запись за краем отвергнута", NULL);

	/* Flush — это CMD6 на FLUSH_CACHE. */
	m.nseen = 0;
	m.ext_csd[EXT_CSD_FLUSH_CACHE] = 0;
	r = sdmmc_card_flush();
	check(r == OK, "flush прошёл", NULL);
	check(count_cmd(MMC_SWITCH) >= 1, "flush — это CMD6", NULL);
	check_u64(m.ext_csd[EXT_CSD_FLUSH_CACHE], 1,
	    "в FLUSH_CACHE записана единица");
}

/* Малая карта: ёмкость из CSD, адресация байтами. */
static void
test_small_emmc(void)
{
	struct sdmmc_card card;
	uint8_t buf[SDMMC_SECTOR_SIZE];
	int r;

	printf("eMMC на 2 ГиБ и меньше: байтовая адресация\n");
	memset(&m, 0, sizeof(m));
	m.is_sd = 0;
	m.hc = 0;			/* карта не подтверждает HCS */
	m.ocr_polls = 0;
	m.cid[3] = 0x15010045;
	m.cid[2] = 0x4d4d4331;
	m.cid[1] = 0x30000000;
	m.cid[0] = 0x00000000;

	/*
	 * CSD с C_SIZE = 3751, C_SIZE_MULT = 7, READ_BL_LEN = 10.
	 *
	 * Ёмкость по формуле спецификации: (C_SIZE+1) << (C_SIZE_MULT+2) =
	 * 3752 << 9 = 1 921 024 блока по 2^READ_BL_LEN = 1024 байта, то есть
	 * 1 967 128 576 байт и 3 842 048 секторов по 512. Меньше двух
	 * гигабайт, а значит адресация байтовая — ради этого случай и есть.
	 *
	 * Раскладка: csd[0] — биты 31:0, csd[3] — биты 127:96.
	 */
	m.csd[3] = 0x00000000;
	m.csd[2] = 0x000a0000;		/* READ_BL_LEN=10, биты 83:80 */
	m.csd[1] = 0x00000000;
	m.csd[0] = 0x00000000;
	m.csd[1] |= (3751u & 0x3) << 30;	/* C_SIZE, биты 63:62 */
	m.csd[2] |= (3751u >> 2) & 0x3ff;	/* C_SIZE, биты 73:64 */
	m.csd[1] |= 7u << 15;			/* C_SIZE_MULT, биты 49:47 */
	/* SEC_COUNT остаётся нулём: ёмкость должна прийти из CSD */

	fill_medium();
	r = sdmmc_card_init(&mock, &card);
	check(r == OK, "опознание малой карты", NULL);
	if (r != OK)
		return;
	check(!card.sector_addressed, "адресация байтами", NULL);
	check_u64(card.sectors, 3842048, "ёмкость из CSD");

	m.nseen = 0;
	r = sdmmc_card_read(7, 1, buf, 0);
	check(r == OK, "чтение с малой карты", NULL);
	check_u64(m.last_data_arg, 7 * SDMMC_SECTOR_SIZE,
	    "аргумент — байтовое смещение");

	/* Кэша нет: flush честно ничего не делает и отвечает OK. */
	check(!card.cache_on, "у карты без кэша он и не включён", NULL);
	m.nseen = 0;
	r = sdmmc_card_flush();
	check(r == OK, "flush без кэша отвечает OK", NULL);
	check(count_cmd(MMC_SWITCH) == 0, "и не шлёт CMD6", NULL);
	check(count_cmd(MMC_SEND_STATUS) >= 1,
	    "но дожидается готовности карты", NULL);
}

static void
test_sd(void)
{
	struct sdmmc_card card;
	int r;

	printf("SD-карта версии 2\n");
	memset(&m, 0, sizeof(m));
	m.is_sd = 1;
	m.sd_v2 = 1;
	m.hc = 1;
	m.ocr_polls = 3;
	m.rca = 0x1234;

	m.cid[3] = 0x03534453;		/* MID 3, OID "SD" */
	m.cid[2] = 0x55303847;		/* PNM "U08G.." */
	m.cid[1] = 0x80112233;
	m.cid[0] = 0x00445500;

	/* CSD версии 2, C_SIZE = 15159 -> (15159+1)*1024 секторов. */
	m.csd[3] = 0x40000000;
	m.csd[2] = 0x00000000;
	m.csd[1] = 0x3b370000;
	m.csd[0] = 0x00000000;

	host_width = 0;
	r = sdmmc_card_init(&mock, &card);
	check(r == OK, "опознание SD", NULL);
	if (r != OK)
		return;

	check(card.is_sd, "опознана как SD", NULL);
	check(card.sector_addressed, "SDHC адресуется секторами", NULL);
	check_u64(card.sectors, 15523840, "ёмкость из CSD версии 2");
	check_u64(card.rca, 0x1234, "RCA взят из ответа карты");
	check_u64(card.bus_width, 4, "шина расширена до четырёх бит");
	check_u64(host_width, 4, "контроллер переведён на четыре бита");
	check_u64(card.clock, 25000000, "такт SD — 25 МГц");
	check(count_cmd(MMC_SEND_OP_COND) == 0,
	    "CMD1 карте SD не задавался", NULL);
	check(count_cmd(MMC_APP_CMD) >= 2, "ACMD41 повторялся", NULL);
}

/* Карта, которая не отвечает вовсе. */
static void
test_absent(void)
{
	struct sdmmc_card card;
	int r;

	printf("Пустой слот\n");
	memset(&m, 0, sizeof(m));
	m.is_sd = 0;
	m.hc = 0;
	m.ocr_polls = 1 << 30;		/* никогда не готова */

	r = sdmmc_card_init(&mock, &card);
	check(r != OK, "пустой слот — это отказ, а не карта", NULL);
	check(!card.present, "карта не помечена как найденная", NULL);
}

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		stub_log_level = LEVEL_TRACE;

	test_emmc();
	test_small_emmc();
	test_sd();
	test_absent();

	printf("\n%d проверок, %d отказов\n", checks, failures);
	return failures != 0;
}
