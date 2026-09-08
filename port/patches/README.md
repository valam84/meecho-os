# Правки в дереве MINIX для платы Raspberry Pi CM4

Файлы BSP (`port/bsp/broadcom/`) — новые, они просто копируются в
`minix/kernel/arch/earm/bsp/broadcom/`. Ниже — то, что надо изменить
в существующих файлах.

Список получен через `git grep -l "BOARD_IS_\|BOARD_ID_\|board_id"`, то есть
это все точки диспетчеризации по плате во всём дереве, а не догадки.

---

## 1. `minix/include/minix/board.h` — обязательно, фаза 2

Добавить вендора, плату, идентификатор и предикат.

```c
/* рядом с MINIX_BOARD_VENDOR_TI */
#define MINIX_BOARD_VENDOR_BROADCOM	MINIX_MK_BOARD_VENDOR(1<<2)

/* рядом с MINIX_BOARD_BB */
/* Raspberry Pi 4 / Compute Module 4 */
#define MINIX_BOARD_RPI4		MINIX_MK_BOARD(1<<3)

#define BOARD_ID_RPI4CM \
	( MINIX_BOARD_ARCH_ARM \
	| MINIX_BOARD_ARCH_VARIANT_ARM_ARMV7 \
	| MINIX_BOARD_VENDOR_BROADCOM \
	| MINIX_BOARD_RPI4 \
	| MINIX_BOARD_VARIANT_GENERIC \
	)

#define BOARD_IS_RPI4(v)	((v) == BOARD_ID_RPI4CM)
```

Плюс две таблицы в том же файле:

```c
static struct shortname2id shortname2id[] = {
	{.name = "BBXM",.id = BOARD_ID_BBXM},
	{.name = "A335BONE",.id = BOARD_ID_BBW},
	{.name = "A335BNLT",.id = BOARD_ID_BBB},
	{.name = "RPI4CM",.id = BOARD_ID_RPI4CM},		/* <- новое */
};

static struct board_id2name board_id2name[] = {
	...
	{.id = BOARD_ID_RPI4CM,.name = "ARM-ARMV7-BROADCOM-RPI4-GENERIC"},
};
```

Строка `RPI4CM` — это то, что должно приехать в `board_name=` из cmdline.
Её разбирает `pre_init.c:359`. **U-Boot для Raspberry Pi не выставляет
переменную `board_name` сам** (в отличие от BeagleBone, где U-Boot ставит
`A335BNLT`), поэтому значение надо прописать жёстко в генерируемом `uEnv.txt` —
см. пункт 4.

---

## 2. `minix/kernel/arch/earm/arch_clock.c` — обязательно, фаза 2

В `init_local_timer()` захардкожена частота свободно бегущего счётчика
по board_id. Без этой ветки ядро паникует на старте.

```c
	} else if (BOARD_IS_RPI4(machine.board_id)) {
		tsc_per_ms[0] = 1000;	/* System Timer: фиксированный 1 МГц */
	} else {
		panic(...);
	}
```

Значение 1000 не подбирается: System Timer BCM2711 тактируется ровно 1 МГц,
то есть 1000 тиков на миллисекунду.

---

## 3. `minix/kernel/arch/earm/Makefile.inc` — обязательно, фаза 2

Сейчас там безусловная строка:

```make
.include "bsp/ti/Makefile.inc"
```

Заменить на выбор BSP на этапе сборки:

```make
MINIX_BSP?=	ti
.include "bsp/${MINIX_BSP}/Makefile.inc"
```

**Выбор обязан быть на этапе сборки, а не в рантайме.** MINIX умеет держать
BBXM и BeagleBone в одном бинарнике за счёт диспетчеризации по board_id, но
там обе платы обслуживает один и тот же код `bsp/ti/`. Слинковать `bsp/ti/`
и `bsp/broadcom/` вместе нельзя — оба определяют одни и те же символы
`bsp_ser_init`, `bsp_irq_mask` и так далее.

Собирается потом так:

```
./build.sh -m evbearm-el -V MINIX_BSP=broadcom ...
```

---

## 4. `releasetools/gen_uEnv.txt.sh` и `releasetools/arm_sdimage.sh` — фаза 1

Нужен вариант образа для Raspberry Pi. Отличия от BeagleBone:

- раздел FAT с прошивкой VideoCore: `bootcode.bin` (на CM4 не нужен — он в
  SPI EEPROM), `start4.elf`, `fixup4.dat`
- `config.txt`:
  ```
  arm_64bit=0
  enable_gic=1
  kernel=u-boot.bin
  init_uart_clock=48000000
  enable_uart=1
  ```
  `enable_gic=1` критичен: без него прошивка оставляет легаси-контроллер
  прерываний, а весь код в `bcm2711_intr.c` рассчитан на GIC-400.
- `board_name=RPI4CM` прописывается в `uEnv.txt` жёстко (см. пункт 1)

---

## 5. `minix/drivers/tty/tty/arch/earm/rs232.c` — фаза 3

Это **userspace-драйвер** последовательного порта, отдельный от BSP-консоли
ядра. BSP-консоль умеет только `putc` для отладочной печати ядра;
интерактивный шелл идёт через этот драйвер.

Понадобится backend для PL011 рядом с существующим OMAP-овским.
Регистры уже описаны в `port/bsp/broadcom/bcm2711_registers.h`, но здесь
дополнительно нужны приём и обработка прерываний, чего в BSP-версии нет.

---

## 6. Драйверы, которые придётся трогать в фазах 4–5

Все они диспетчеризуются по board_id и сейчас умеют только OMAP:

| Файл | Фаза | Замена для BCM2711 |
|---|---|---|
| `minix/drivers/storage/mmc/emmc.c` | 4 | EMMC2 @ `0xfe340000`, SDHCI-совместимый |
| `minix/drivers/storage/mmc/mmchost_mmchs.c` | 4 | то же |
| `minix/drivers/clock/readclock/arch/earm/arch_readclock.c` | 4 | на CM4 RTC нет вообще |
| `minix/drivers/net/lan8710a/lan8710a.c` | 5 | GENET @ `0xfd580000` |
| `minix/drivers/bus/i2c/arch/earm/omap_i2c.c` | позже | BSC-контроллеры |
| `minix/drivers/system/gpio/gpio.c`, `minix/lib/libgpio/gpio_omap.c` | позже | GPIO @ `0xfe200000` |
| `minix/drivers/usb/usbd/base/earm/usbd_earm.c` | отдельный проект | VL805 за PCIe (xHCI) |
| `minix/lib/libclkconf/clkconf.c` | позже | у BCM2711 модель тактирования иная |

---

## Порядок применения

Пункты 1–3 — минимум, чтобы ядро собралось и загрузилось до печати в UART.
Пункт 4 — чтобы оно вообще стартовало на плате или в QEMU raspi4b.
Пункт 5 — чтобы получить шелл.
Пункт 6 — по мере надобности.
