/*
 * Хостовой стенд: станционный адрес из идентификатора чипа.
 *
 * Вторая чисто вычислительная часть драйвера и вторая, где ошибка молчит:
 * неверный хеш даёт совершенно правдоподобный адрес, и узнать, что он не
 * тот, можно только сравнив с адресом, под которым плата уже известна сети.
 *
 * Эталон получен **другим путём и раньше кода**: шестнадцать байт ячейки
 * soc-id сняты с платы под вендорским Linux
 * (`od -tx1 /sys/bus/nvmem/devices/rockchip-otp0/nvmem`, смещение 0x0a), а
 * ожидаемый адрес — тот, который то же ядро печатает при загрузке:
 *
 *   rk_gmac-dwmac fe010000.ethernet: rk_get_eth_addr_from_otp(soc-id) done
 *   rk_gmac-dwmac fe010000.ethernet: device MAC address 8a:6e:41:09:8e:2f
 *
 * То есть стенд сверяет наш вывод с числом, которое напечатала работающая
 * система, а не с числом, которое мы же и посчитали.
 */

#include <stdio.h>
#include <string.h>

#include "dwmac_socid.h"

static int failed;

static void
check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL  %s\n", what);
		failed++;
	}
}

/* Ячейка soc-id этой платы: OTP, смещение 0x0a, шестнадцать байт. */
static const uint8_t board_soc_id[16] = {
	0x4d, 0x34, 0x52, 0x30, 0x33, 0x39, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x15, 0x01
};

static const uint8_t board_hwaddr[6] = {
	0x8a, 0x6e, 0x41, 0x09, 0x8e, 0x2f
};

static void
show(const char *what, const uint8_t *a)
{
	printf("%s %02x:%02x:%02x:%02x:%02x:%02x\n", what,
	    a[0], a[1], a[2], a[3], a[4], a[5]);
}

int
main(void)
{
	uint8_t addr[6], other[6];
	uint8_t id[16];
	int r;

	/* Тот самый адрес — единственная проверка, ради которой всё это. */
	memset(addr, 0, sizeof(addr));
	r = dwmac_hwaddr_from_soc_id(board_soc_id, sizeof(board_soc_id), addr);
	check(r == 0, "адрес по soc-id платы посчитался");
	show("посчитано:", addr);
	show("на плате: ", board_hwaddr);
	check(memcmp(addr, board_hwaddr, 6) == 0,
	    "адрес совпал с тем, что печатает вендорское ядро");

	/* Он же обязан быть локально администрируемым и не групповым. */
	check((addr[0] & 0x01) == 0, "адрес не групповой");
	check((addr[0] & 0x02) != 0, "адрес локально администрируемый");

	/* Одинаковый вход — одинаковый выход: адрес переживает перезагрузку. */
	memset(other, 0, sizeof(other));
	(void)dwmac_hwaddr_from_soc_id(board_soc_id, sizeof(board_soc_id),
	    other);
	check(memcmp(addr, other, 6) == 0, "повторный счёт даёт то же самое");

	/* Другой чип — другой адрес. */
	memcpy(id, board_soc_id, sizeof(id));
	id[0] ^= 0x01;
	(void)dwmac_hwaddr_from_soc_id(id, sizeof(id), other);
	check(memcmp(addr, other, 6) != 0,
	    "изменение одного бита идентификатора меняет адрес");

	/* Пустой OTP — отказ, а не общий для всех плат адрес. */
	memset(id, 0, sizeof(id));
	check(dwmac_hwaddr_from_soc_id(id, sizeof(id), other) != 0,
	    "пустой идентификатор отвергнут");

	/* Слишком короткая ячейка — тоже отказ (вендор требует восьми байт). */
	check(dwmac_hwaddr_from_soc_id(board_soc_id, 4, other) != 0,
	    "короткий идентификатор отвергнут");
	check(dwmac_hwaddr_from_soc_id(NULL, 16, other) != 0,
	    "пустой указатель отвергнут");

	/*
	 * Длина, при которой работает основной цикл хеша (больше двенадцати
	 * байт обрабатываются блоками, остаток - хвостом). Ячейка платы в
	 * шестнадцать байт проходит оба пути, но проверить стоит и границу:
	 * ровно двенадцать - только хвост, тринадцать - цикл и хвост.
	 */
	memcpy(id, board_soc_id, sizeof(id));
	check(dwmac_hwaddr_from_soc_id(id, 12, other) == 0,
	    "двенадцать байт (только хвост) считаются");
	check(dwmac_hwaddr_from_soc_id(id, 13, other) == 0,
	    "тринадцать байт (цикл и хвост) считаются");

	if (failed == 0)
		printf("OK    станционный адрес из soc-id: все проверки\n");
	else
		printf("%d проверок не прошли\n", failed);

	return failed != 0;
}
