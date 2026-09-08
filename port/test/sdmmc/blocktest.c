/*
 * Хостовый стенд для блочного слоя драйвера sdmmc.
 *
 * Проверяется цикл нарезки запроса на куски и различение гранта от
 * указателя — то есть ровно те два места, где ошибка не роняет систему, а
 * молча кладёт данные не туда. Первое: запрос приходит вектором, каждый
 * элемент может быть длиннее буфера драйвера, и его надо разложить на
 * команды карте, не сбившись ни в номере сектора, ни в смещении внутри
 * гранта. Второе: libblockdriver кладёт в iov_addr грант для чужого запроса
 * и указатель для собственного (чтение таблицы разделов), а на LP64 это
 * разной ширины.
 *
 * sdmmc.c включается целиком, потому что всё в нём статическое: стенду
 * нужны и sdmmc_transfer(), и таблица part[]. main() переименован ключом
 * компилятора. Слои ниже — карта и контроллер — заменены моделью, которая
 * записывает, о чём её попросили.
 *
 *	cc -Dmain=sdmmc_main -I stubs -I <дерево>/.../mmc blocktest.c
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/blockdriver.h>
#include <minix/drvlib.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include "sdmmc.h"

/* ------------------------------------------------------------------ */
/* Заглушки окружения						      */
/* ------------------------------------------------------------------ */

int stub_log_level = LEVEL_WARN;

void micro_delay(unsigned long UNUSED(u)) { }
void default_log(void) { }
void env_setargs(int UNUSED(c), char **UNUSED(v)) { }
int env_parse(const char *UNUSED(n), const char *UNUSED(f), int UNUSED(i),
	long *UNUSED(p), long UNUSED(lo), long UNUSED(hi)) { return 0; }
void sef_setcb_init_fresh(int (*cb)(int, sef_init_info_t *)) { (void)cb; }
void sef_setcb_signal_handler(void (*cb)(int)) { (void)cb; }
void sef_startup(void) { }
void blockdriver_announce(int UNUSED(t)) { }
void blockdriver_task(struct blockdriver *UNUSED(b)) { }
int fdt_node_is_compatible(const struct fdt_node *UNUSED(n),
	const char *UNUSED(w)) { return 0; }

/* Таблицу разделов стенд не разбирает: интересен не MBR, а нарезка. */
static int partition_calls;
void
partition(struct blockdriver *UNUSED(b), int UNUSED(d), int UNUSED(s),
	int UNUSED(a))
{
	partition_calls++;
}

/* ------------------------------------------------------------------ */
/* Модель гранта						      */
/* ------------------------------------------------------------------ */

#define NGRANTS	4
#define GRANT_BASE 1000

static struct { uint8_t *buf; size_t size; } grants[NGRANTS];

static cp_grant_id_t
grant_make(int slot, uint8_t *buf, size_t size)
{
	grants[slot].buf = buf;
	grants[slot].size = size;
	return GRANT_BASE + slot;
}

static int copy_faults;	/* сколько раз копирование отказало */

static int
grant_copy(cp_grant_id_t g, vir_bytes off, void *other, size_t bytes,
	int to_grant)
{
	int slot = g - GRANT_BASE;

	if (slot < 0 || slot >= NGRANTS || grants[slot].buf == NULL ||
	    off + bytes > grants[slot].size) {
		copy_faults++;
		return EFAULT;
	}

	if (to_grant)
		memcpy(grants[slot].buf + off, other, bytes);
	else
		memcpy(other, grants[slot].buf + off, bytes);
	return OK;
}

int
sys_safecopyto(int UNUSED(dst), int grant, vir_bytes off, vir_bytes addr,
	size_t bytes)
{
	return grant_copy(grant, off, (void *)addr, bytes, 1);
}

int
sys_safecopyfrom(int UNUSED(src), int grant, vir_bytes off, vir_bytes addr,
	size_t bytes)
{
	return grant_copy(grant, off, (void *)addr, bytes, 0);
}

/* ------------------------------------------------------------------ */
/* Модель карты							      */
/* ------------------------------------------------------------------ */

#define CARD_SECTORS	4096

static uint8_t medium[CARD_SECTORS * SDMMC_SECTOR_SIZE];

static struct {
	uint64_t sector;
	uint32_t count;
	int write;
} ops[64];
static int nops;

static int fail_after = -1;	/* -1: не отказывать */

static void
record_op(uint64_t sector, uint32_t count, int write)
{
	if (nops < (int)(sizeof(ops) / sizeof(ops[0]))) {
		ops[nops].sector = sector;
		ops[nops].count = count;
		ops[nops].write = write;
	}
	nops++;
}

int
sdmmc_host_find(struct sdmmc_host *UNUSED(h))
{
	return ENXIO;
}

int
sdmmc_card_init(struct sdmmc_host *UNUSED(h), struct sdmmc_card *UNUSED(c))
{
	return ENXIO;
}

int
sdmmc_card_read(uint64_t sector, uint32_t count, void *buf)
{
	record_op(sector, count, 0);
	if (fail_after >= 0 && nops > fail_after)
		return EIO;
	if (sector + count > CARD_SECTORS)
		return EINVAL;
	memcpy(buf, medium + sector * SDMMC_SECTOR_SIZE,
	    count * SDMMC_SECTOR_SIZE);
	return OK;
}

int
sdmmc_card_write(uint64_t sector, uint32_t count, const void *buf)
{
	record_op(sector, count, 1);
	if (fail_after >= 0 && nops > fail_after)
		return EIO;
	if (sector + count > CARD_SECTORS)
		return EINVAL;
	memcpy(medium + sector * SDMMC_SECTOR_SIZE, buf,
	    count * SDMMC_SECTOR_SIZE);
	return OK;
}

static int flushes;
int
sdmmc_card_flush(void)
{
	flushes++;
	return OK;
}

/* Драйвер целиком, вместе со своими статическими таблицами. */
#include "sdmmc.c"

/* main() драйвера уехал под другое имя ключом компилятора; вернуть имя
 * стенду. */
#undef main

/* ------------------------------------------------------------------ */
/* Проверки							      */
/* ------------------------------------------------------------------ */

static int checks, failures;

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
check_i64(long long got, long long want, const char *what)
{
	char buf[128];

	if (got == want) {
		checks++;
		return;
	}
	snprintf(buf, sizeof(buf), "получено %lld, ожидалось %lld", got, want);
	check(0, what, buf);
}

static void
setup(uint64_t base_sectors, uint64_t size_sectors)
{
	unsigned i;

	for (i = 0; i < sizeof(medium); i++)
		medium[i] = (uint8_t)(i * 31 + (i >> 8));

	memset(&card, 0, sizeof(card));
	card.present = 1;
	card.sectors = CARD_SECTORS;

	memset(part, 0, sizeof(part));
	memset(subpart, 0, sizeof(subpart));
	part[0].dv_base = base_sectors * SDMMC_SECTOR_SIZE;
	part[0].dv_size = size_sectors * SDMMC_SECTOR_SIZE;

	nops = 0;
	copy_faults = 0;
	fail_after = -1;
}

static void
test_single_sector(void)
{
	uint8_t buf[SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Один сектор через грант\n");
	setup(0, CARD_SECTORS);

	memset(buf, 0, sizeof(buf));
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = SDMMC_SECTOR_SIZE;

	r = sdmmc_transfer(0, 0, 5 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, SDMMC_SECTOR_SIZE, "вернулось столько, сколько просили");
	check_i64(nops, 1, "одна команда карте");
	check_i64((long long)ops[0].sector, 5, "тот сектор");
	check_i64(ops[0].count, 1, "один блок");
	check(memcmp(buf, medium + 5 * SDMMC_SECTOR_SIZE,
	    SDMMC_SECTOR_SIZE) == 0, "данные дошли до гранта", NULL);
	check_i64(copy_faults, 0, "копирований мимо гранта не было");
}

static void
test_chunking(void)
{
	static uint8_t buf[100 * SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;
	int expect_first = SDMMC_CHUNK_SECTORS;

	printf("Запрос длиннее буфера драйвера\n");
	setup(0, CARD_SECTORS);

	memset(buf, 0, sizeof(buf));
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = sizeof(buf);

	r = sdmmc_transfer(0, 0, 7 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, (long long)sizeof(buf), "передано всё");
	check_i64(nops, 2, "разложено на два куска");
	check_i64((long long)ops[0].sector, 7, "первый кусок с седьмого");
	check_i64(ops[0].count, expect_first, "первый кусок полон");
	check_i64((long long)ops[1].sector, 7 + expect_first,
	    "второй кусок продолжает с места первого");
	check_i64(ops[1].count, 100 - expect_first, "второй кусок — остаток");
	check(memcmp(buf, medium + 7 * SDMMC_SECTOR_SIZE, sizeof(buf)) == 0,
	    "склеилось без дыр и без нахлёста", NULL);
}

static void
test_vector(void)
{
	static uint8_t a[2 * SDMMC_SECTOR_SIZE];
	static uint8_t b[3 * SDMMC_SECTOR_SIZE];
	iovec_t iov[2];
	ssize_t r;

	printf("Вектор из двух буферов\n");
	setup(0, CARD_SECTORS);

	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	iov[0].iov_addr = grant_make(0, a, sizeof(a));
	iov[0].iov_size = sizeof(a);
	iov[1].iov_addr = grant_make(1, b, sizeof(b));
	iov[1].iov_size = sizeof(b);

	r = sdmmc_transfer(0, 0, 100 * SDMMC_SECTOR_SIZE, 42, iov, 2, 0);
	check_i64(r, (long long)(sizeof(a) + sizeof(b)), "передано всё");
	check_i64(nops, 2, "по команде на буфер");
	check_i64((long long)ops[0].sector, 100, "первый буфер с сотого");
	check_i64((long long)ops[1].sector, 102,
	    "второй буфер продолжает, а не начинает заново");
	check(memcmp(a, medium + 100 * SDMMC_SECTOR_SIZE, sizeof(a)) == 0,
	    "первый буфер верен", NULL);
	check(memcmp(b, medium + 102 * SDMMC_SECTOR_SIZE, sizeof(b)) == 0,
	    "второй буфер верен", NULL);
}

static void
test_partition(void)
{
	static uint8_t buf[4 * SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Раздел со смещением и его край\n");
	setup(0, CARD_SECTORS);
	/* Раздел: 200 секторов начиная с 1000-го. */
	part[1].dv_base = 1000 * SDMMC_SECTOR_SIZE;
	part[1].dv_size = 200 * SDMMC_SECTOR_SIZE;

	memset(buf, 0, sizeof(buf));
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = sizeof(buf);

	r = sdmmc_transfer(1, 0, 10 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, (long long)sizeof(buf), "чтение внутри раздела");
	check_i64((long long)ops[0].sector, 1010,
	    "смещение раздела прибавлено");

	/* Последние два сектора раздела: запрос на четыре обрезается. */
	nops = 0;
	r = sdmmc_transfer(1, 0, 198 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, 2 * SDMMC_SECTOR_SIZE, "обрезано по краю раздела");
	check_i64(ops[0].count, 2, "и карте заказано только два блока");
	check_i64((long long)ops[0].sector, 1198, "с верного сектора");

	/* Целиком за краем — не ошибка, а ноль байт. */
	nops = 0;
	r = sdmmc_transfer(1, 0, 200 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, 0, "за краем раздела читается ноль");
	check_i64(nops, 0, "и карту никто не беспокоит");
}

static void
test_self(void)
{
	static uint8_t buf[SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Свой запрос драйвера: в iov_addr указатель, а не грант\n");
	setup(0, CARD_SECTORS);

	memset(buf, 0, sizeof(buf));
	iov.iov_addr = (vir_bytes)buf;
	iov.iov_size = sizeof(buf);

	r = sdmmc_transfer(0, 0, 0, SELF, &iov, 1, 0);
	check_i64(r, SDMMC_SECTOR_SIZE, "чтение таблицы разделов прошло");
	check(memcmp(buf, medium, SDMMC_SECTOR_SIZE) == 0,
	    "по указателю легли данные", NULL);
	check_i64(copy_faults, 0,
	    "указатель не был принят за грант (ловушка LP64)");
}

static void
test_write(void)
{
	static uint8_t buf[3 * SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Запись\n");
	setup(0, CARD_SECTORS);

	memset(buf, 0x5a, sizeof(buf));
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = sizeof(buf);

	r = sdmmc_transfer(0, 1, 50 * SDMMC_SECTOR_SIZE, 42, &iov, 1, 0);
	check_i64(r, (long long)sizeof(buf), "записано всё");
	check(ops[0].write, "это была запись", NULL);
	check(memcmp(medium + 50 * SDMMC_SECTOR_SIZE, buf, sizeof(buf)) == 0,
	    "на носителе то, что дали", NULL);
}

static void
test_bad_requests(void)
{
	static uint8_t buf[2 * SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Запросы, которые надо отвергнуть\n");
	setup(0, CARD_SECTORS);
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = sizeof(buf);

	r = sdmmc_transfer(0, 0, 100, 42, &iov, 1, 0);
	check_i64(r, EINVAL, "позиция не на границе сектора");

	iov.iov_size = 100;
	r = sdmmc_transfer(0, 0, 0, 42, &iov, 1, 0);
	check_i64(r, EINVAL, "длина не кратна сектору");

	iov.iov_size = 0;
	r = sdmmc_transfer(0, 0, 0, 42, &iov, 1, 0);
	check_i64(r, EINVAL, "пустой элемент вектора");

	iov.iov_size = SDMMC_SECTOR_SIZE;
	r = sdmmc_transfer(99, 0, 0, 42, &iov, 1, 0);
	check_i64(r, ENXIO, "несуществующий минор");

	check_i64(nops, 0, "ни один плохой запрос не дошёл до карты");
}

static void
test_error_midway(void)
{
	static uint8_t buf[100 * SDMMC_SECTOR_SIZE];
	iovec_t iov;
	ssize_t r;

	printf("Отказ карты посреди длинного запроса\n");
	setup(0, CARD_SECTORS);
	iov.iov_addr = grant_make(0, buf, sizeof(buf));
	iov.iov_size = sizeof(buf);
	fail_after = 1;		/* вторая команда отказывает */

	r = sdmmc_transfer(0, 0, 0, 42, &iov, 1, 0);
	check_i64(r, SDMMC_CHUNK_SECTORS * SDMMC_SECTOR_SIZE,
	    "вернулось то, что успело пройти, а не ошибка");
}

static void
test_flush_and_open(void)
{
	printf("Открытие, закрытие и flush\n");
	setup(0, CARD_SECTORS);
	open_count = 0;
	partition_calls = 0;
	flushes = 0;

	check_i64(sdmmc_open(0, 0), OK, "открытие устройства");
	check_i64(partition_calls, 1,
	    "таблица разделов прочитана при первом открытии");
	check_i64((long long)part[0].dv_size,
	    (long long)CARD_SECTORS * SDMMC_SECTOR_SIZE,
	    "размер устройства взят из ёмкости карты");

	check_i64(sdmmc_open(0, 0), OK, "второе открытие");
	check_i64(partition_calls, 1, "и таблица заново не читается");

	check_i64(sdmmc_flush(0), OK, "flush по минору");
	check_i64(flushes, 1, "дошёл до карты");

	check_i64(sdmmc_close(0), OK, "первое закрытие");
	check_i64(flushes, 1, "пока открыт — не сбрасываем");
	check_i64(sdmmc_close(0), OK, "последнее закрытие");
	check_i64(flushes, 2, "последний закрывший сбрасывает кэш");

	check_i64(sdmmc_close(0), EINVAL, "лишнее закрытие отвергнуто");
}

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		stub_log_level = LEVEL_TRACE;

	test_single_sector();
	test_chunking();
	test_vector();
	test_partition();
	test_self();
	test_write();
	test_bad_requests();
	test_error_midway();
	test_flush_and_open();

	printf("\n%d проверок, %d отказов\n", checks, failures);
	return failures != 0;
}
