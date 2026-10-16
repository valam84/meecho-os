# SDM660: eMMC (sdhci-msm v5) — что нужно сверх стандартного SDHCI

Снято 2026-10-16 из `drivers/mmc/host/sdhci-msm.c` (sdm660-mainline,
`72bc4bb`), `sdm630.dtsi:1488-1541`, `sdm660-xiaomi-clover-common.dtsi:628`.
Факты, не код. [C] — в коде/дереве, [I] — вывод.

## Узел [C]

`qcom,sdm630-sdhci` + `qcom,sdhci-msm-v5` → вариант v5 (`mci_removed`):
hc 0x0c0c4000/0x1000, cqhci 0x0c0c5000/0x1000, ice 0x0c0c8000/0x8000;
**региона "core" нет**. hc_irq SPI 110 (INTID 142), pwr_irq SPI 112 (144).
Такты iface/core/xo/ice, сброс GCC_SDCC1_BCR. 8 бит, non-removable,
HS200/HS400 1.8 В, enhanced strobe; vmmc l4b 2.95 В, vqmmc l8a 1.8 В.

## Регистры вендора — внутри hc (v5) [C]

DLL_CONFIG 0x200, DLL_STATUS 0x208, VENDOR_SPEC 0x20c, VENDOR_SPEC_FUNC2
0x210, ADMA_ERR_ADDR 0x214/0x218, VENDOR_SPEC_CAPABILITIES0 0x21c,
DDR_200_CFG 0x224, PWRCTL_STATUS/MASK/CLEAR/CTL 0x240/0x244/0x248/0x24c,
VENDOR_SPEC3 0x250, DLL_CONFIG_2 0x254, DLL_CONFIG_3 0x258, DDR_CONFIG
0x25c, VENDOR_SPECIFIC_FUNC4 0x260, MCI_VERSION 0x318 (major 31:28, minor
7:0 — **читать и печатать**), DLL_USR_CTL 0x388. HC_MODE на v5 нет.

cqhci: CFG +0x08 (bit0 CQ enable) [I], CTL +0x0C (bit0 HALT) [I],
VENDOR_CFG1 +0xA00 (bits 14:13 DIS_RST_ON_CQ_EN), NONCQ_CRYPTO_PARM +0x70
(CE bit 8), NONCQ_CRYPTO_DUN +0x74 [C].

## Рукопожатие питания (pwr_irq) [C]

Запрос вызывают: запись SDHCI_POWER_CONTROL (0x29), SOFTWARE_RESET ALL
(0x2f) при включённом питании, смена бита VDD_180 в HOST_CONTROL2 (0x3e).
STATUS 0x240: bit0 BUS_OFF, bit1 BUS_ON, bit2 IO_LOW, bit3 IO_HIGH.
Ответ: s = STATUS & 0xf → записать s в CLEAR (повторять до 10 раз по
10 мкс, пока не сойдёт) → CTL: BUS_SUCCESS (bit0) для BUS_*, IO_SUCCESS
(bit2) для IO_*. Ждёт ли контроллер ответа вечно — в коде не сказано [I:
считать, что ждёт]. **RESET_CMD/RESET_DATA запроса не вызывают** — для
восстановления после ошибки безопасны.

VENDOR_SPEC bit 16 IO_PAD_PWR_SWITCH (1 = 1.8 В) — **никогда не снимать**.
Пробник Linux пишет VENDOR_SPEC = 0xa9c — **при захвате не делать**.

## Такты [C]

Делитель SDHCI Linux не использует: такт карты — частота GCC SDCC1_APPS
(50/100/384 МГц), DDR-режимы — GCC вдвое выше. VENDOR_SPEC MCLK_SEL
(9:8): 2 обычно, 3 HS400; HS400 ещё HC_SELECT_IN_EN (18) + HC_SELECT_IN
(21:19) = 6. Квирки: CAP_CLOCK_BASE_BROKEN, PRESET_VALUE_BROKEN,
BROKEN_CARD_DETECTION, SINGLE_POWER_WRITE, MULTIBLOCK_READ_ACMD12.

## Стратегия: принять состояние загрузчика

**Не трогать**: POWER_CONTROL, RESET_ALL, VDD_180, CLOCK_CONTROL,
CLK_PWRSAVE, MCLK_SEL/HC_SELECT_IN, DLL_CONFIG*/DDR_CONFIG/DDR_200_CFG.
Пока частота GCC та же и такт включён, DLL держит калибровку [I].

Порядок захвата:
1. Живость: HOST_VERSION (0xfe), MCI_VERSION (0x318) — не нули.
2. Висящий запрос PWRCTL — очистить и ответить SUCCESS; MASK не трогать.
3. CQE: если cqhci+0x08 bit0 — остановить (CTL.HALT), VENDOR_CFG1
   14:13, снять CFG.EN, замаскировать SDHCI_INT_CQE (bit 14).
   NONCQ_CRYPTO_PARM = 0 (иначе данные пойдут через ключ ICE).
4. Снимок: HOST_CONTROL (bit5 8 бит, bit2 HS, 4:3 DMA), HOST_CONTROL2
   (2:0 UHS: 3 = HS200/HS400, 4 = DDR52; bit3 VDD_180), CLOCK_CONTROL,
   VENDOR_SPEC, DLL_CONFIG, DLL_STATUS (bit7 DLL_LOCK, bit11 DDR_DLL_LOCK),
   DDR_200_CFG (bit1 enhanced strobe). HS200/HS400 без замка DLL — отказ.
5. RCA: CMD13 с RCA 1, 2, 3… — отвечает только свой; ждать состояние 4
   (tran); 3 (stby) — CMD7.
6. CMD8 (EXT_CSD): [179] PARTITION_CONFIG биты 2:0 должны быть 0 (user
   area), [15] CMDQ_MODE_EN — 0, [185] HS_TIMING, [183] BUS_WIDTH — как у
   контроллера. Не так — **отказ, а не CMD6** (первый круг — только чтение
   и запись внутри окна).
7. Обмен: 32-битный ADMA2 (DMA select 0x10, дескрипторы 8 байт), буферы
   ниже 4 ГиБ; CMD23 + CMD18/25 или CMD17/24. Без ES при настроенном
   тюнинге Linux переключает CDR на каждой команде: чтение — CDR_EN=1,
   CDR_EXT_EN=0, прочее — CDR_EN=0, CDR_EXT_EN=1 (DLL_CONFIG 17/19); с ES
   (DDR_200_CFG bit1) CDR не нужен.

Таймаут данных — в тактах SD, 4·2^(n+13); Linux ставит 0xe/0xf.
R1b-занятость сообщается Transfer Complete (WAIT_WHILE_BUSY).
