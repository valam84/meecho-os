# SDM660 Mi Pad 4: сенсор FT5x06 через BLSP1 QUP3 I2C, опросом

Снято 2026-10-16 из sdm660-mainline (`72bc4bb`): `i2c-qup.c`,
`gcc-sdm660.c`, `clk-branch.c`, `clk-rcg2.c`, `pinctrl-sdm660.c`,
`pinctrl-msm.c`, `edt-ft5x06.c`; деревья sdm630/clover. Факты, не код.
[C] — в коде/дереве, [I] — вывод.

## Дерево [C]

- I2C: blsp_i2c3 @ 0x0c177000/0x600, `qcom,i2c-qup-v2.2.1`, SPI 97,
  такты core = GCC_BLSP1_QUP3_I2C_APPS_CLK, iface = GCC_BLSP1_AHB_CLK,
  400 кГц.
- GCC @ 0x00100000/0x94000. TLMM: south 0x03100000, center 0x03500000,
  north 0x03900000 (по 0x400000).
- Сенсор @ 0x38, `edt,edt-ft5406`; vcc = l11a (always-on); INT — gpio67
  (спад), RESET — gpio66 (активный ноль); 1200x1920.
- **У clover-plus на той же шине Goodix gt9110 @ 0x5d** — драйвер ищет
  именно `edt,*` и на Plus молчит.

## A. GCC (база 0x00100000) [C]

- BLSP1_AHB: голосуемый — 0x00152004 |= bit17; статус CBCR 0x00117004.
- RCG blsp1_qup3_i2c_apps: CMD_RCGR 0x0011d020, CFG_RCGR 0x0011d024
  (SRC_DIV 4:0, SRC_SEL 10:8, MODE 13:12, HW_CLK_CTRL 20).
  19.2 МГц = XO (SEL 0), поле делителя 1. Обновление: CMD bit0, ждать 0
  (500 × 1 мкс).
- QUP3_I2C_APPS CBCR 0x0011d008, bit0 enable.
- CBCR: включён, когда bit31 (CLK_OFF) = 0 или bits 30:28 = 2 (200 × 1 мкс).
- i2c-qup частоту не ставит — берёт, что есть; ставить XO/1 явно — надёжно.
- Порядок: AHB-голос → RCG → ветка. GDSC не нужен.

## B. TLMM [C]

Регистры пина: tile + 0x1000·gpio: CFG +0, IN_OUT +4, INTR_CFG +8,
INTR_STATUS +0xc. Все четыре нужных пина в **north**.

| пин | CFG | значение |
|---|---|---|
| gpio10, gpio11 (i2c3) | 0x0390a000, 0x0390b000 | func 2, 2 мА, без подтяжки → 0x008 |
| gpio66 (reset, выход) | 0x03942000 | сперва IN_OUT bit1, затем 0x2c3 |
| gpio67 (int, вход) | 0x03943000 | 0x0c3; уровень — IN_OUT bit0 |

CFG: 1:0 pull (0 нет, 1 вниз, 2 keeper, 3 вверх), 5:2 func, 8:6 сила
(мА/2 − 1), bit9 OE. Только чтение-изменение-запись этих полей.

## C. QUP v2 I2C, FIFO, опрос [C]

Регистры: CONFIG 0x000, STATE 0x004, IO_MODE 0x008, SW_RESET 0x00c,
OPERATIONAL 0x018, ERROR_FLAGS 0x01c, MX_OUTPUT_CNT 0x100, OUT_FIFO 0x110,
MX_WRITE_CNT 0x150, MX_INPUT_CNT 0x200, MX_READ_CNT 0x208, IN_FIFO 0x218,
I2C_CLK_CTL 0x400, I2C_STATUS 0x404, I2C_MASTER_GEN 0x408.

- STATE: 1:0 (RESET 0, RUN 1, PAUSE 3), bit2 VALID, bit4 MAST_GEN.
- CONFIG: 0x207 (mini-core I2C, N=7), +bit7 — без входа.
- IO_MODE: только чтение до записи; размеры FIFO из кодов; пишем 0xc000.
- OPERATIONAL: bit8 OUT_SVC, bit9 IN_SVC, bit10 MX_OUTPUT_DONE (флаги
  сбрасываются записью бита).
- Ошибки: I2C_STATUS & 0x038000fc (bit3 NACK, bit8 BUS_ACTIVE отдельно),
  ERROR_FLAGS & 0x7c; сбрасываются записью обратно.
- MX_*_CNT bit31 — «настройка во время работы», со второго подобмена.
- CLK_CTL при 19.2 МГц / 400 кГц = 0x000e031c (fs_div 28, hs_div 3).
- Теги: START 0x81, DATA_WR 0x82, DATA_WR_STOP 0x83, DATA_RD 0x85,
  DATA_RD_NACK 0x86, DATA_RD_STOP 0x87; адрес = (0x38 << 1) | R.

Запись регистра R, затем чтение N байт (как делает Linux, два подобмена):
1. SW_RESET = 1; ждать VALID и RESET.
2. CONFIG = 0x207; MASTER_GEN = 1; ждать STATE bit4.
3. MX_OUTPUT_CNT = 0, MX_INPUT_CNT = 0, IO_MODE = 0xc000.
4. MX_WRITE_CNT = 5; CONFIG = 0x287; RUN; CLK_CTL; PAUSE;
   OUT_FIFO = 0x01827081, затем 0x000000RR; RUN; ждать готовность; PAUSE.
5. MX_WRITE_CNT = 0x80000004; MX_READ_CNT = 0x80000000 | (N + 2);
   CONFIG = 0x207; OUT_FIFO = 0xNN877181; RUN; ждать IN_SVC; прочитать
   ceil((N+2)/4) слов, первые 2 байта — теги.
6. Ждать I2C_STATUS bit8 = 0; RESET.
Ошибка — записать флаги обратно, STATE = RESET; NACK → ENXIO.

## D. FT5x06 [C]

- Сброс: gpio66 низко 5–6 мс, затем высоко, ждать 300 мс.
- Опознание: 22 байта с 0xbb — "EP0" в [1..3] → M06, в [0..2] → M12,
  иначе FocalTech: 0xa6 (версия), 0xa8 (0x11/0x35/0x43/0x50/0x57/0x70/0xa1 —
  M09, 0x59 — EV_FT).
- Кадр (все, кроме M06): 33 байта с регистра 0x00; точка i — с байта
  3 + 6i: событие = b0 >> 6 (0 нажатие, 1 отпускание, 2 касание, 3 нет),
  x = (b0 & 0xf) << 8 | b1, y = (b2 & 0xf) << 8 | b3, id = b2 >> 4 (< 5).
  EV_FT меняет x и y местами. Координаты — пиксели панели [I].
- Читать, когда gpio67 низок.
- Начальных записей нет.
