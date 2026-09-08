/*
 * Хостовой стенд: во что превращается список выводов GMAC1.
 *
 * Проверяется единственная чисто арифметическая часть драйвера — и та, где
 * ошибка молчит. Неверный регистр или не тот полубайт мультиплексируют
 * чужой вывод, а симптом один: линк, который не поднимается. На плате это
 * стоит круга, здесь — секунды.
 *
 * Ожидаемые значения получены **другим путём**: посчитаны вручную по
 * формуле из pinctrl-rockchip.c и записаны в port/cb2-gmac/registers.md
 * ещё до того, как был написан драйвер. Стенд сверяет два независимых
 * вывода одних и тех же чисел.
 */

#include <stdio.h>
#include <string.h>

#include "dwmac_pins.h"

static int failed;

static void
check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL  %s\n", what);
		failed++;
	}
}

/*
 * Таблица из registers.md, раздел «Выводы»: шесть записей в GRF.
 */
static const struct dwmac_pin_write expected[] = {
	{ 0x40, 0x3300, 0xff00 },	/* выводы 2, 3 */
	{ 0x44, 0x3333, 0xffff },	/* 4, 5, 6, 7 */
	{ 0x48, 0x3330, 0xfff0 },	/* 9, 10, 11 */
	{ 0x4c, 0x3330, 0xfff0 },	/* 13, 14, 15 */
	{ 0x50, 0x0003, 0x000f },	/* 16 */
	{ 0x54, 0x0033, 0x00ff },	/* 20, 21 */
};

int
main(void)
{
	struct dwmac_pin_write w[DWMAC_PIN_MAX_WRITES];
	unsigned n, i, j;

	n = dwmac_pin_writes(w, DWMAC_PIN_MAX_WRITES);

	printf("what the driver would write:\n");
	for (i = 0; i < n; i++)
		printf("    grf+0x%03x  val %04x  mask %04x\n",
		    w[i].reg, w[i].val, w[i].mask);
	printf("\n");

	check(n == sizeof(expected) / sizeof(expected[0]),
	    "wrong number of register writes");

	for (i = 0; i < n && i < sizeof(expected) / sizeof(expected[0]); i++) {
		char what[80];

		snprintf(what, sizeof(what), "write %u: register", i);
		check(w[i].reg == expected[i].reg, what);
		snprintf(what, sizeof(what), "write %u: value", i);
		check(w[i].val == expected[i].val, what);
		snprintf(what, sizeof(what), "write %u: mask", i);
		check(w[i].mask == expected[i].mask, what);
	}

	/* Ни один регистр не назван дважды, и порядок по возрастанию. */
	for (i = 1; i < n; i++)
		check(w[i].reg > w[i - 1].reg, "registers out of order");

	/*
	 * Каждый вывод покрыт ровно одним полубайтом, и во всех полубайтах
	 * стоит функция 3 — не «в среднем правильно», а поштучно.
	 */
	{
		static const unsigned pins[] = { 2, 3, 4, 5, 6, 7, 9, 10, 11,
		    13, 14, 15, 16, 20, 21 };
		unsigned covered = 0;

		for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
			unsigned reg = dwmac_pin_iomux_reg(DWMAC_PIN_BANK,
			    pins[i]);
			unsigned sh = dwmac_pin_iomux_shift(pins[i]);
			int seen = 0;

			for (j = 0; j < n; j++) {
				if (w[j].reg != reg)
					continue;
				if (((w[j].mask >> sh) & 0xf) != 0xf)
					continue;
				if (((w[j].val >> sh) & 0xf) != DWMAC_PIN_FUNC)
					continue;
				seen++;
			}
			if (seen != 1) {
				printf("FAIL  pin %u: covered %d times\n",
				    pins[i], seen);
				failed++;
			}
			covered++;
		}
		check(covered == 15, "wrong number of pins");
	}

	/*
	 * И ни одного лишнего полубайта: сумма установленных полубайтов маски
	 * ровно пятнадцать. Пятнадцать выводов - пятнадцать полей, не больше.
	 */
	{
		unsigned nibbles = 0;

		for (i = 0; i < n; i++)
			for (j = 0; j < 4; j++)
				if (((w[i].mask >> (j * 4)) & 0xf) == 0xf)
					nibbles++;
		check(nibbles == 15, "the writes touch pins that were not asked for");
	}

	printf("%s\n", failed ? "FAILED" : "all checks passed");
	return failed != 0;
}
