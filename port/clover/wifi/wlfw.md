# WLFW поверх QMI поверх QRTR: спецификация провода для WCN3990 (SDM660)

Справка для своего BSD-драйвера. Источник фактов — Linux (GPL): отсюда
берутся **только** номера, раскладки и порядок, текст кода не переносится.
Все ссылки вида `файл:строка`:

- `qmi_encdec.c`, `qmi_interface.c`, `qmi.h`, `qrtr.h`, `af_qrtr.c`, `ns.c` —
  `~/wifi-ref/soc/`;
- `qmi.c`, `qmi.h(ath10k)`, `qmi_wlfw_v01.c`, `qmi_wlfw_v01.h`, `snoc.c`,
  `ce.h`, `htc.h`, `hw.h`, `core.c` — `~/wifi-ref/ath10k/`;
- `git:путь:строка` — `~/sdm660-linux`, коммит `72bc4bb1` (2026-10-14), через
  `git show HEAD:путь` (дерево частичное, часть файлов без блобов).

Порядок байтов везде **little-endian**. «Обязательный» TLV — тип 0x01…0x0F,
«необязательный» — от 0x10 (`qmi_encdec.c:100`).

---

## 1. Кадрирование QMI

### 1.1 Заголовок (7 байт, упакован)

| Смещение | Поле | Размер | Значение |
|---|---|---|---|
| 0 | `type` | u8 | 0 — запрос, 2 — ответ, 4 — индикация (`qmi.h:32-34`) |
| 1 | `txn_id` | le16 | номер транзакции |
| 3 | `msg_id` | le16 | номер сообщения (у запроса и ответа один и тот же) |
| 5 | `msg_len` | le16 | длина TLV-части после заголовка |

Раскладка — `qmi.h:25-30` (`__packed`), заполнение — `qmi_encdec.c:864-868`.
Пустое сообщение (`CAP_REQ`, `MSA_READY_REQ`) — один заголовок с
`msg_len = 0`: описание без элементов даёт 0 байт (`qmi_encdec.c:855-862`,
`qmi_wlfw_v01.c:823-824`, `:1502-1503`).

**При приёме Linux `msg_len` не читает:** разбор идёт по длине датаграммы
минус 7 (`qmi_encdec.c:895-896`). Своему разборщику лучше сверять оба.

### 1.2 Транзакции и сопоставление ответа

- Номер берётся `idr_alloc_cyclic(…, 0, U16_MAX)` (`qmi_interface.c:323`):
  циклически, диапазон 0…65534 — **по коду первый номер может быть 0**.
- Ответ (`type == 2`) ищется по `txn_id` среди открытых транзакций
  (`qmi_interface.c:490-498`); ответ без открытой транзакции молча
  выбрасывается. `msg_id` ответа при этом **не сверяется**.
- Запросы и индикации, пришедшие клиенту, ищут обработчик по паре
  (`type`, `msg_id`) (`qmi_interface.c:402-405`, `:515-521`). У ath10k два
  обработчика — `FW_READY_IND` и `MSA_READY_IND` (`qmi.c:964-980`).
- Срок ожидания ответа у ath10k — 30 с (`qmi.c:26`).
- Пакет от `(свой узел, QRTR_PORT_CTRL)` — это управляющий пакет QRTR, а не
  QMI (`qmi_interface.c:559-561`).

### 1.3 TLV

| Поле | Размер |
|---|---|
| тип | u8 |
| длина | le16 (число байт значения, без этих 3 байт) |
| значение | `длина` байт |

Кодирование — `qmi_encdec.c:14-18`, разбор — `:20-24`. Порядок TLV в
сообщении — порядок элементов в описании (`qmi_encdec.c:380-494`).
При разборе: неизвестный тип ≥ 0x10 пропускается по длине, неизвестный
< 0x10 — ошибка всего сообщения (`qmi_encdec.c:726-734`).

Числа: u8/u16/u32/u64 и перечисления (`SIGNED_2/4_BYTE_ENUM`) — ровно своей
ширины, LE (`qmi_encdec.c:33-52`, `:209-237`). Перечисления WLFW — 4 байта.

### 1.4 Необязательные поля (`*_valid`)

Флаг `*_valid` **на провод не идёт**. Если он ноль, все элементы с тем же
типом TLV пропускаются целиком, TLV отсутствует (`qmi_encdec.c:395-404`,
`:121-137`). Если не ноль — TLV есть. При разборе наличие TLV ставит
флаг в 1 (`qmi_encdec.c:744-748`).

### 1.5 Массивы переменной длины

Перед элементами — счётчик **элементов** (не байт):

- **1 байт**, если `elem_size` счётчика в описании равен 1;
- **2 байта (le16) в любом другом случае** — в том числе когда в C-структуре
  счётчик `u32` (`qmi_encdec.c:406-408`, `:750-752`).

Счётчик 0: TLV всё равно выходит, длина значения = ширина счётчика, сами
элементы пропущены (`qmi_encdec.c:426-430`). Это случай `CAL_REPORT` ниже.
Статический массив (`STATIC_ARRAY`) счётчика не имеет: ровно `elem_len`
элементов (`qmi_encdec.c:386-387`).

### 1.6 Вложенные структуры

Структура внутри TLV — **плотная конкатенация полей без заголовков TLV и без
выравнивания** (TLV ставится только на уровне 1: `qmi_encdec.c:377-378`,
`:486-493`). Массив структур — элементы подряд после счётчика
(`qmi_encdec.c:257-278`). При разборе вложенный уровень 2 обязан съесть
TLV ровно до конца (`qmi_encdec.c:582-588`).

### 1.7 Строки

- **На уровне 1** (строка — сама значение TLV): префикса длины нет, длина
  строки = длина TLV, NUL не передаётся (`qmi_encdec.c:315-321`, `:622-623`).
- **Внутри структуры**: префикс длины — 1 байт, если `elem_len ≤ 255`, иначе
  2 байта; затем символы без NUL (`qmi_encdec.c:307-308`, `:322-333`,
  `:624-641`).
- При разборе строка должна быть **строго короче** `elem_len` (место под NUL):
  `qmi_encdec.c:643-646`.

### 1.8 Общий ответ, TLV 0x02

Во всех ответах WLFW первым идёт обязательный TLV **0x02**, длина 4:
`result` le16, затем `error` le16 (`qmi_encdec.c:901-929`; тип 0x02 задаёт
каждое сообщение, например `qmi_wlfw_v01.c:533-541`).

| `result` | | `error` (выборка) | |
|---|---|---|---|
| 0 | SUCCESS | 0 | NONE |
| 1 | FAILURE | 1 | MALFORMED_MSG |
| | | 2 / 3 | NO_MEMORY / INTERNAL |
| | | 41 | INVALID_ID |
| | | 58 | ENCODING |
| | | 90 | INCOMPATIBLE_STATE |
| | | 94 | NOT_SUPPORTED |

`qmi.h:81-93`.

---

## 2. Служба WLFW

| Что | Значение | Где |
|---|---|---|
| Служба | **0x45** (69) | `qmi.h:103` |
| Версия | **1** | `qmi_wlfw_v01.h:10` |
| Экземпляр в поиске ath10k | **0** | `qmi.c:1115-1116` |
| Поле `instance` в QRTR | `версия | экземпляр << 8` = **0x00000001** | `qmi_interface.c:177`, `git:include/linux/soc/qcom/qrtr.h:18` |
| ID клиента в `IND_REGISTER` | **0x4b4e454c** (байты `4c 45 4e 4b`, «LENK») | `qmi.c:25` |
| Буфер приёма клиента | 6182 + 7 | `qmi.c:1098-1100`, `qmi_interface.c:644` |

Обратно из `NEW_SERVER`: версия = `instance & 0xff`, экземпляр =
`instance >> 8` (`qmi_interface.c:53-54`).

### 2.1 Номера сообщений (`qmi_wlfw_v01.h:12-61`)

Запрос и ответ делят номер; ниже — только нужные bring-up'у.

| ID | Запрос/ответ | ID | Индикация |
|---|---|---|---|
| 0x0020 | IND_REGISTER | 0x0021 | FW_READY_IND |
| 0x0022 | WLAN_MODE | 0x002B | MSA_READY_IND |
| 0x0023 | WLAN_CFG | 0x0028 | INITIATE_CAL_DOWNLOAD_IND |
| 0x0024 | CAP | 0x002A | INITIATE_CAL_UPDATE_IND |
| 0x0025 | BDF_DOWNLOAD | 0x002C | PIN_CONNECT_RESULT_IND |
| 0x0026 | CAL_REPORT | 0x0035 | REQUEST_MEM_IND |
| 0x002D | MSA_INFO | 0x0037 | MEM_READY_IND |
| 0x002E | MSA_READY | 0x0038 | FW_INIT_DONE_IND |
| 0x002F | INI | 0x0039 | REJUVENATE_IND |
| 0x0034 | HOST_CAP | 0x003D | XO_CAL_IND |

Прочие запросы: 0x0027 CAL_DOWNLOAD, 0x0029 CAL_UPDATE, 0x0030/0x0031
ATHDIAG_READ/WRITE, 0x0032 VBATT, 0x0033 MAC_ADDR, 0x0036 RESPOND_MEM,
0x003A REJUVENATE_ACK, 0x003B DYNAMIC_FEATURE_MASK, 0x003C M3_INFO.

Биты `fw_status` (`IND_REGISTER` ответ, TLV 0x10): 0x01 ALREADY_REGISTERED,
**0x02 FW_READY**, 0x04 MSA_READY, 0x08 MEM_READY, 0x10 FW_INIT_DONE
(`qmi_wlfw_v01.h:118-122`).

### 2.2 Перечисления

- `wlan_mode`: 0 MISSION, 1 FTM, 2 EPPING, 3 WALTEST, **4 OFF**, 5 CCPM,
  6 QVIT, 7 CALIBRATION (`qmi_wlfw_v01.h:80-89`). ath10k шлёт MISSION
  (`snoc.c:1006-1007`) и OFF при выключении (`qmi.c:540-543`).
- `pipe_dir`: 0 NONE, 1 IN (цель→хост), 2 OUT (хост→цель), 3 INOUT
  (`qmi_wlfw_v01.h:99-104`, `ce.h:415-418`).
- `cal_temp_id`/`file_id`: 0…4 (`qmi_wlfw_v01.h:91-97`).
- Флаги CE: 0x00 нет, 0x01 NO_SNOOP, 0x02 BYTE_SWAP_DATA, 0x04
  SWIZZLE_DESCRIPTORS, **0x08 DISABLE_INTR**, 0x10 ENABLE_POLL
  (`qmi_wlfw_v01.h:111-116`; у ath10k `CE_ATTR_FLAGS = 0` `ce.h:386`,
  `CE_ATTR_DIS_INTR = BIT(3)` `ce.h:279`).

### 2.3 Сообщения по одному

Обозначения: **О** — обязательный, **Н** — необязательный; «ширина» — байты
значения TLV. Колонка «стр.» — строка элемента в `qmi_wlfw_v01.c`.

#### IND_REGISTER_REQ (0x20), MAX 50 (`qmi_wlfw_v01.h:217`)

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x10 | fw_ready_enable | u8 | Н | 331-348 |
| 0x11 | initiate_cal_download_enable | u8 | Н | 349-366 |
| 0x12 | initiate_cal_update_enable | u8 | Н | 367-384 |
| 0x13 | msa_ready_enable | u8 | Н | 385-402 |
| 0x14 | pin_connect_result_enable | u8 | Н | 403-420 |
| 0x15 | client_id | u32 | Н | 421-438 |
| 0x16 | request_mem_enable | u8 | Н | 439-456 |
| 0x17 | mem_ready_enable | u8 | Н | 457-474 |
| 0x18 | fw_init_done_enable | u8 | Н | 475-492 |
| 0x19 | rejuvenate_enable | **u32** | Н | 493-510 |
| 0x1A | xo_cal_enable | u8 | Н | 511-528 |

ath10k ставит 0x10=1, 0x13=1, 0x15=0x4b4e454c; 0x1A=1 только при
`qcom,xo-cal-data` в дереве (`qmi.c:760-770`, `snoc.c:1343-1350`; у clover
этого свойства нет). Итог на проводе (msg_len = 15):

```
00 TT TT 20 00 0f 00 | 10 01 00 01 | 13 01 00 01 | 15 04 00 4c 45 4e 4b
```

#### IND_REGISTER_RESP (0x20), MAX 18 (`:226`)

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x02 | result, error | 2+2 | О | 533-542 |
| 0x10 | fw_status | u64 | Н | 543-560 |

Если `fw_status & 0x02` — прошивка уже готова, ath10k сразу идёт в
«FW_READY» и пропускает всё до MSA включительно (`qmi.c:797-800`, `:818-821`).

#### HOST_CAP_REQ (0x34): обычный и «8bit»

Полное описание (`qmi_wlfw_v01.c:1745-1989`), MAX 189 (`qmi_wlfw_v01.h:576`):

| TLV | Поле | Ширина | О/Н |
|---|---|---|---|
| 0x10 | daemon_support | u32 | Н |
| 0x11 | wake_msi | u32 | Н |
| 0x12 | gpios | **le16 счётчик** + n×u32, n ≤ 32 | Н |
| 0x13 | nm_modem | u8 | Н |
| 0x14 | bdf_support | u8 | Н |
| 0x15 | bdf_cache_support | u8 | Н |
| 0x16 | m3_support | u8 | Н |
| 0x17 | m3_cache_support | u8 | Н |
| 0x18 | cal_filesys_support | u8 | Н |
| 0x19 | cal_cache_support | u8 | Н |
| 0x1A | cal_done | u8 | Н |
| 0x1B | mem_bucket | u32 | Н |
| 0x1C | mem_cfg_mode | u8 | Н |

Вариант «8bit» (`qmi_wlfw_v01.c:1992-2011`) — описание из **одного** элемента:
TLV 0x10 `daemon_support` как **u8**. ath10k заполняет только
`daemon_support_valid = 1, daemon_support = 0` (`qmi.c:663-664`), так что
разница на проводе ровно в ширине значения TLV 0x10:

```
обычный: 00 TT TT 34 00 07 00 | 10 04 00 00 00 00 00
8bit:    00 TT TT 34 00 04 00 | 10 01 00 00
```

Выбор — по свойству `qcom,snoc-host-cap-8bit-quirk` (`snoc.c:1363-1364`,
`qmi.c:671-674`); `qcom,snoc-host-cap-skip-quirk` — не слать вовсе
(`snoc.c:1366-1367`, `qmi.c:827-831`). **У SDM630/660 стоит 8bit**
(`git:arch/arm64/boot/dts/qcom/sdm630.dtsi:2671`; `sdm660.dtsi` включает
`sdm630.dtsi`, clover — `sdm660.dtsi`). Ответ `result≠0` с
`error = 94 (NOT_SUPPORTED)` ath10k считает нормой (`qmi.c:690-696`).

Ответ HOST_CAP_RESP: только 0x02 (`qmi_wlfw_v01.c:2014-2025`), MAX 7.

*Расхождение в Linux:* у `gpios_len` в описании `elem_size = u32`, поэтому
счётчик на проводе 2 байта, а MAX 189 посчитан с 1 байтом (190 при 2).
Безвредно, пока `gpios` не шлют.

#### MSA_INFO_REQ (0x2D), MAX 18

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x01 | msa_addr | u64 | О | 1448-1456 |
| 0x02 | size | u32 | О | 1457-1465 |

Значения — физический адрес и размер области MSA хоста (`qmi.c:131-132`).
Область берётся из `memory-region` узла (`snoc.c:1584-1590`); у SDM630/660
это `wlan_msa_mem` **0x85700000, 0x100000** (`git:…/sdm630.dtsi:451-454`,
`:2653`); при её отсутствии — `dma_alloc_coherent` 0x100000
(`snoc.c:69`, `:1597-1604`). Провод (msg_len 18):

```
00 TT TT 2d 00 12 00 | 01 08 00 00 00 70 85 00 00 00 00 | 02 04 00 00 00 10 00
```

#### MSA_INFO_RESP (0x2D), MAX 37

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x02 | result, error | 4 | О | 1470-1479 |
| 0x03 | mem_region_info | **u8 счётчик** + n×13, n ≤ 2 | О | 1480-1498 |

Элемент `mem_region_info` — 13 байт подряд (`qmi_wlfw_v01.c:126-155`):

| Смещение | Поле | Ширина |
|---|---|---|
| 0 | region_addr | u64 |
| 8 | size | u32 |
| 12 | secure_flag | u8 |

ath10k проверяет: n ≤ 2, каждая область внутри [msa_addr, msa_addr+size]
(`qmi.c:159-179`). Каждая область потом отдаётся SCM (раздел 3.2).

#### MSA_READY_REQ (0x2E) / RESP

Запрос пустой (`00 TT TT 2e 00 00 00`), ответ — 0x02 (`qmi_wlfw_v01.c:1502-1517`),
MAX 0 и 7.
*Дефект Linux:* при `result ≠ 0` ath10k печатает ошибку, но возвращает 0 и
идёт дальше (`qmi.c:226-232`).

#### CAP_REQ (0x24) / CAP_RESP, MAX 0 / 207

Запрос пустой. Ответ (`qmi_wlfw_v01.c:827-950`):

| TLV | Поле | Значение на проводе | О/Н |
|---|---|---|---|
| 0x02 | result, error | 4 | О |
| 0x10 | chip_info | chip_id u32, chip_family u32 (8) | Н |
| 0x11 | board_info | board_id u32 (4) | Н |
| 0x12 | soc_info | soc_id u32 (4) | Н |
| 0x13 | fw_version_info | fw_version u32, затем **строка с префиксом u8** длины ≤ 32 (5…37) | Н |
| 0x14 | fw_build_id | строка уровня 1: **без префикса**, длина TLV = длина строки ≤ 128 | Н |
| 0x15 | num_macs | u8 | Н |

Вложенные описания — `qmi_wlfw_v01.c:260-328`. Длины строк: 32+1 и 128+1
(`qmi_wlfw_v01.h:69`, `:72`, `:189`, `:316`). Без 0x10/0x11 ath10k ставит
`chip_id`/`board_id` = 0xFF (`qmi.c:609-619`); `board_id`/`chip_id` дают имя
секции в `board-2.bin`: `bus=snoc,qmi-board-id=%x,qmi-chip-id=%x…`
(`core.c:1584`, `qmi.c:868-877`). ath10k ещё пишет `fw_build_id` в SMEM,
элемент 469, слот 13×128, до 75 байт (`qmi.c:27-30`, `:545-570`) —
к проводу не относится.

#### BDF_DOWNLOAD_REQ (0x25), MAX 6182 (`qmi_wlfw_v01.h:341`)

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x01 | valid | u8 | О | 954-962 |
| 0x10 | file_id | s32 enum | Н | 963-980 |
| 0x11 | total_size | u32 | Н | 981-998 |
| 0x12 | seg_id | u32 | Н | 999-1016 |
| 0x13 | data | **le16 счётчик** + n байт, n ≤ 6144 | Н | 1017-1043 |
| 0x14 | end | u8 | Н | 1044-1061 |
| 0x15 | bdf_type | u8 | Н | 1062-1079 |

Сегмент — не больше **6144** байт (`QMI_WLFW_MAX_DATA_SIZE_V01`,
`qmi_wlfw_v01.h:66`). Что шлёт ath10k на каждом сегменте (`qmi.c:255-312`):
`valid=1`, `file_id=0`, `total_size` = длина всего файла, `seg_id` = 0, 1, 2…,
`data`, `end` = 0 на всех сегментах, кроме последнего (там 1); **TLV 0x14
присутствует всегда**, 0x15 не шлётся. Сегменты — строго по одному, каждый
ждёт ответа. msg_len = 34 + n:

```
00 TT TT 25 00 LL LL | 01 01 00 01 | 10 04 00 00 00 00 00 | 11 04 00 <total:4>
| 12 04 00 <seg:4> | 13 NN NN <n:2> <n байт> | 14 01 00 <end>     (NN NN = n + 2)
```

**Причуда конца:** `end = 1` запускает в прошивке проверку CRC платы; при
провале приходит ошибка, но прошивка BDF всё равно принимает. ath10k
прощает отказ на последнем сегменте, сравнивая **`result`** (не `error`!) с
`QMI_ERR_MALFORMED_MSG_V01` = 1 (`qmi.c:294-307`). Так как `result` бывает
только 0 или 1, на деле прощается **любой** отказ последнего сегмента.
Ответ BDF_DOWNLOAD_RESP — 0x02 (`qmi_wlfw_v01.c:1083-1094`), MAX 7.

#### CAL_REPORT_REQ (0x26), MAX 28

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x01 | meta_data | **u8 счётчик** + n×s32, n ≤ 5 | О | 1098-1115 |
| 0x10 | xo_cal_data | u8 | Н | 1116-1133 |

ath10k кладёт в `meta_data` номера `cal_data[]` с данными
(`qmi.c:344-351`), но `cal_data` нигде не заполняется — **n всегда 0**;
0x10 — только с `qcom,xo-cal-data`. Провод (msg_len 4):
`00 TT TT 26 00 04 00 | 01 01 00 00`. Ответ — 0x02, MAX 7.

#### WLAN_CFG_REQ (0x23), MAX 803 (`qmi_wlfw_v01.h:288`)

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x10 | host_version | строка уровня 1, ≤ 16 | Н | 676-693 |
| 0x11 | tgt_cfg | **u8 счётчик** + n×20, n ≤ 12 | Н | 694-721 |
| 0x12 | svc_cfg | **u8 счётчик** + n×12, n ≤ 24 | Н | 722-749 |
| 0x13 | shadow_reg | **u8 счётчик** + n×4, n ≤ 24 | Н | 750-777 |
| 0x14 | shadow_reg_v2 | **u8 счётчик** + n×4, n ≤ 36 | Н | 778-805 |

Элементы:

| `tgt_cfg` (20 байт, `:11-58`) | `svc_cfg` (12 байт, `:60-89`) | `shadow_reg` (4, `:91-111`) | `shadow_reg_v2` (4, `:113-124`) |
|---|---|---|---|
| pipe_num u32 | service_id u32 | id u16 | addr u32 |
| pipe_dir s32 | pipe_dir s32 | offset u16 | |
| nentries u32 | pipe_num u32 | | |
| nbytes_max u32 | | | |
| flags u32 | | | |

Что шлёт ath10k (`qmi.c:450-484`, `snoc.c:971-1003`): 0x10 нет, 0x14 нет;
0x11 — 12 элементов, 0x12 — **21** (20 записей + нулевой терминатор, он тоже
уходит: число считается `sizeof`, `snoc.c:997-998`), 0x13 — 12.
msg_len = (3+1+240) + (3+1+252) + (3+1+48) = **552**. Таблицы:

`tgt_cfg` (`snoc.c:244-364`):

| CE | dir | nentries | nbytes_max | flags |
|---|---|---|---|---|
| 0 | 2 OUT | 32 | 2048 | 0 |
| 1 | 1 IN | 32 | 2048 | 0 |
| 2 | 1 IN | 64 | 2048 | 0 |
| 3 | 2 OUT | 32 | 2048 | 0 |
| 4 | 2 OUT | 256 | 256 | 8 |
| 5 | 2 OUT | 1024 | 64 | 8 |
| 6 | 3 INOUT | 32 | 16384 | 0 |
| 7 | **4** (не константа PIPEDIR) | 0 | 0 | 8 |
| 8 | 1 IN | 32 | 2048 | 0 |
| 9 | 1 IN | 32 | 2048 | 0 |
| 10 | 1 IN | 32 | 2048 | 0 |
| 11 | 1 IN | 32 | 2048 | 0 |

`svc_cfg` (`snoc.c:366-473`; номера служб HTC — `htc.h:259-294`,
`SVC(g,i) = g<<8 | i`):

| service_id | dir → pipe | | service_id | dir → pipe |
|---|---|---|---|---|
| 0x104 WMI_DATA_VO | OUT→3, IN→2 | | 0x0001 RSVD_CTRL | OUT→0, IN→2 |
| 0x102 WMI_DATA_BK | OUT→3, IN→2 | | 0xFE00 TEST_RAW | OUT→0, IN→2 |
| 0x101 WMI_DATA_BE | OUT→3, IN→2 | | 0x300 HTT_DATA | OUT→4, IN→1 |
| 0x103 WMI_DATA_VI | OUT→3, IN→2 | | 0xFE00 TEST_RAW | OUT→5 |
| 0x100 WMI_CONTROL | OUT→3, IN→2 | | 0x301 HTT_DATA2 | IN→9 |
| | | | 0x302 HTT_DATA3 | IN→10 |
| | | | 0x600 HTT_LOG | IN→11 |
| | | | 0, 0, 0 | терминатор |

Порядок записей на проводе — как в таблице слева направо по строкам
исходника: VO-out, VO-in, BK-out, BK-in, BE-out, BE-in, VI-out, VI-in,
CTRL-out, CTRL-in, RSVD-out, RSVD-in, TEST-out(0), TEST-in(2), HTT-out(4),
HTT-in(1), TEST-out(5), DATA2-in(9), DATA3-in(10), LOG-in(11), нули.

`shadow_reg` (`snoc.c:72-135`): (0,0x3C) (3,0x3C) (4,0x3C) (5,0x3C) (7,0x3C)
(1,0x40) (2,0x40) (7,0x40) (8,0x40) (9,0x40) (10,0x40) (11,0x40) —
0x3C = SRC_WR_IDX, 0x40 = DST_WR_IDX.

Ответ WLAN_CFG_RESP — 0x02, MAX 7.

#### WLAN_MODE_REQ (0x22), MAX 11

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x01 | mode | s32 enum | О | 631-639 |
| 0x10 | hw_debug | u8 | Н | 640-657 |

ath10k всегда шлёт `hw_debug_valid=1, hw_debug=0` (`qmi.c:396-398`):
`00 TT TT 22 00 0b 00 | 01 04 00 00 00 00 00 | 10 01 00 00` (MISSION).
Ответ — 0x02, MAX 7.

#### INI_REQ (0x2F), MAX 4

| TLV | Поле | Ширина | О/Н | стр. |
|---|---|---|---|---|
| 0x10 | enablefwlog | u8 | Н | 1521-1538 |

Шлётся всегда при старте ядра драйвера (`core.c:3281`, `snoc.c:1134-1145`),
значение — `ATH10K_ENABLE_FW_LOG_DIAG` или `_CE` (модульный параметр
`fw_diag_log`, по умолчанию выкл. → DIAG). **Числовые значения этих двух
констант в доступных исходниках не найдены** (ни в `~/wifi-ref`, ни в
локальных блобах `~/sdm660-linux`) — снять на эталоне. Ответ — 0x02, MAX 7.

#### Индикации FW_READY_IND (0x21) и MSA_READY_IND (0x2B)

Обе пустые: описание без элементов (`qmi_wlfw_v01.c:564-570`), MAX 0
(`qmi_wlfw_v01.h:233`, `:240`). На проводе: `04 TT TT 21 00 00 00` и
`04 TT TT 2b 00 00 00`; `txn_id` у индикации значения не имеет.

### 2.4 Все *_MAX_MSG_LEN (`qmi_wlfw_v01.h`)

| Сообщение | MAX | стр. | | Сообщение | MAX | стр. |
|---|---|---|---|---|---|---|
| IND_REGISTER req/resp | 50 / 18 | 217, 226 | | CAL_UPDATE req/resp | 14 / 6181 | 413, 431 |
| FW_READY_IND | 0 | 233 | | MSA_INFO req/resp | 18 / 37 | 439, 448 |
| MSA_READY_IND | 0 | 240 | | MSA_READY req/resp | 0 / 7 | 455, 462 |
| PIN_CONNECT_RESULT_IND | 21 | 252 | | INI req/resp | 4 / 7 | 470, 477 |
| WLAN_MODE req/resp | 11 / 7 | 261, 268 | | ATHDIAG_READ req/resp | 21 / 6156 | 486, 496 |
| WLAN_CFG req/resp | 803 / 7 | 288, 295 | | ATHDIAG_WRITE req/resp | 6163 / 7 | 506, 513 |
| CAP req/resp | 0 / 207 | 302, 321 | | VBATT req/resp | 11 / 7 | 520, 527 |
| BDF_DOWNLOAD req/resp | 6182 / 7 | 341, 348 | | MAC_ADDR req/resp | 9 / 7 | 535, 542 |
| CAL_REPORT req/resp | 28 / 7 | 358, 365 | | HOST_CAP req/resp | 189 / 7 | 576, 584 |
| INITIATE_CAL_DOWNLOAD_IND | 7 | 372 | | REQUEST_MEM_IND | 564 | 592 |
| CAL_DOWNLOAD req/resp | 6178 / 7 | 390, 397 | | RESPOND_MEM req/resp | 260 / 7 | 600, 607 |
| INITIATE_CAL_UPDATE_IND | 14 | 405 | | MEM_READY / FW_INIT_DONE IND | 0 / 0 | 614, 621 |
| REJUVENATE_IND | 144 | 635 | | REJUVENATE_ACK req/resp | 0 / 7 | 642, 649 |
| DYNAMIC_FEATURE_MASK req/resp | 11 / 29 | 657, 668 | | M3_INFO req/resp | 18 / 7 | 676, 683 |
| XO_CAL_IND | 4 | 690 | | | | |

Числа — длина TLV-части без 7 байт заголовка; пересчёт по раскладке выше
сходится (кроме HOST_CAP, см. выше).

---

## 3. Последовательность ath10k

### 3.1 Порядок

Подготовка (`snoc.c:1727-1866`): свойства-причуды (`snoc.c:1765`,
`:1355-1368`), область MSA (`:1846`, `:1578-1612`), `wifi-firmware` нет —
`use_tz = true` (`:1624-1628`), затем QMI-клиент (`:1858`, `qmi.c:1078-1121`):
флаги `qcom,msa-fixed-perm` и `qcom,no-msa-ready-indicator`
(`qmi.c:1092-1096`), `NEW_LOOKUP(0x45, instance 1)` (`qmi.c:1115-1116`).

Дальше всё событиями (`qmi.c:1033-1076`):

1. **`NEW_SERVER` для 0x45** → `connect` на (node, port) службы
   (`qmi.c:982-1007`) → событие SERVER_ARRIVE.
2. **SERVER_ARRIVE** (`qmi.c:808-860`):
   1. `IND_REGISTER` (`:814`); если в ответе `fw_status & FW_READY` —
      сразу шаг 4 (`:818-821`).
   2. `HOST_CAP` (8bit на SDM660), если нет skip-причуды (`:827-831`).
   3. `MSA_INFO` (`:833`).
   4. пауза 20 мс (`:842`; заплатка против нарушения XPU на SDM845).
   5. **SCM assign: HLOS → MSS_MSA + WLAN (+ WLAN_CE)** для каждой области
      из ответа, если нет `msa-fixed-perm` (`:844`, `:88-108`).
   6. `MSA_READY` (`:848`).
   7. `CAP` (`:852`).
   8. При отказе 6–7 — обратный assign (`:858-859`, `:110-119`).
3. **BDF** (`qmi.c:918-934`): поиск `board-2.bin` по `qmi-board-id`/
   `qmi-chip-id` из CAP, `BDF_DOWNLOAD` по сегментам, `CAL_REPORT`.
   Запуск: **с `no-msa-ready-indicator` — сразу после SERVER_ARRIVE**
   (`qmi.c:1050-1053`), иначе по `MSA_READY_IND`; при этой причуде пришедший
   `MSA_READY_IND` игнорируется с предупреждением (`:1061-1065`).
   **У SDM630/660 причуда стоит** (`git:…/sdm630.dtsi:2672`), `msa-fixed-perm`
   — нет: assign делается.
4. **`FW_READY_IND`** → регистрация ядра драйвера (`qmi.c:936-944`,
   `snoc.c:1380-1394`) → `hif_power_up` (`core.c:3358`) → питание и такты
   (`snoc.c:1100`) → **`WLAN_CFG`, затем `WLAN_MODE(MISSION)`**
   (`snoc.c:1106`, `qmi.c:515-538`) → кольца CE (`snoc.c:1112-1120`).
5. Старт ядра: HTC/WMI по CE, затем **`INI`** (`core.c:3281`).
6. Выключение: `WLAN_MODE(OFF)` (`snoc.c:1068-1081`, `qmi.c:540-543`).
   Уход службы (`DEL_SERVER`/`BYE`) → обратный assign и «FW_DOWN»
   (`qmi.c:903-916`, `:1009-1026`).

### 3.2 Права на память MSA: `qcom_scm_assign_mem`

Права (`qmi.c:32-86`; VMID — `git:include/dt-bindings/firmware/qcom,scm.h`;
права — `git:include/linux/firmware/qcom/qcom_scm.h:56-60`):

| VMID | Номер | стр. | | Право | Бит |
|---|---|---|---|---|---|
| HLOS | **0x3** | 12 | | READ | 0x4 |
| MSS_MSA | **0xF** | 21 | | WRITE | 0x2 |
| WLAN | **0x18** | 26 | | EXEC | 0x1 |
| WLAN_CE | **0x19** | 27 | | RW | **0x6** |

| Вызов | Источник | Назначение |
|---|---|---|
| map, `secure_flag ≠ 0` | {HLOS} | {MSS_MSA, RW}, {WLAN, RW} |
| map, `secure_flag = 0` | {HLOS} | {MSS_MSA, RW}, {WLAN, RW}, {WLAN_CE, RW} |
| unmap | {MSS_MSA, WLAN} (+ WLAN_CE, если не secure) | {HLOS, RW} |

Адрес и размер — **каждой области из `MSA_INFO_RESP`**, не всей MSA
(`qmi.c:96-99`, `:56-57`). Ошибкой ath10k считает любой ненулевой итог
(`qmi.c:97-99`).

Сам вызов (`git:drivers/firmware/qcom/qcom_scm.c:1316-1423`):

**Буфер описателей** — один физически непрерывный кусок, три секции, каждая
с начала, выровненного на **64** байта (`:1374-1378`, `:1396-1404`):

| Секция | Смещение | Содержимое | Размер (аргумент) |
|---|---|---|---|
| src | 0 | массив le32 — номера VMID источника (биты маски по возрастанию) | 4 × n_src (`:1374`, `:1388-1393`) |
| mem | ALIGN(src, 64) | le64 `mem_addr`, le64 `mem_size` | 16 (`:73-76`, `:1398-1399`) |
| dest | ALIGN(src,64) + ALIGN(16,64) | n_dst × 24 байта: le32 vmid, le32 perm, le64 ctx = 0, le32 ctx_size = 0, le32 unused | 24 × n_dst (`:65-71`, `:1406-1409`) |

Для `map` non-secure: src = 4 байта (`03 00 00 00`), mem с 64, dest с 128,
3 × 24 = 72 байта. Linux берёт буфер из пула tzmem: в режиме GENERIC это
`dma_alloc_coherent` (`git:drivers/firmware/qcom/qcom_tzmem.c:52-67`, `:206`),
то есть некэшируемая память — **своей стороне перед SMC надо сбросить кэш до
PoC** (или держать буфер некэшируемым). Режим SHM-bridge на SDM660 вероятно
не включается (проверить на эталоне, см. раздел 5).

**Аргументы** (`qcom_scm.c:1321-1335`): 7 штук,
`arginfo = QCOM_SCM_ARGS(7, RO, VAL, RO, VAL, RO, VAL, VAL)`, где VAL=0,
RO=1 (`git:drivers/firmware/qcom/qcom_scm.h:22-27`), формула —
`num | t0<<4 | t1<<6 | …` (`qcom_scm.h:29-42`) → **0x1117**.

| № | Значение |
|---|---|
| 0 | физ. адрес секции mem |
| 1 | 16 |
| 2 | физ. адрес секции src |
| 3 | 4 × n_src |
| 4 | физ. адрес секции dest |
| 5 | 24 × n_dst |
| 6 | 0 |

**SMC** (`git:drivers/firmware/qcom/qcom_scm-smc.c:151-213`):

- `x0` = идентификатор по SMCCC: yielding (STD) call, SMC64, владелец SiP
  (2), функция `SCM_SMC_FNID(svc, cmd) = (svc & 0xFF) << 8 | (cmd & 0xFF)`
  (`qcom_scm.h:72`), svc = **0x0C** (MP, `qcom_scm.h:117`), cmd = **0x16**
  (MP_ASSIGN, `qcom_scm.h:123`). По спецификации SMCCC (ARM DEN0028:
  бит 31 — fast, бит 30 — SMC64, биты 29:24 — владелец) это
  **0x42000C16**. Вывод числа — по SMCCC, а не по тексту Linux
  (`arm-smccc.h` в локальном дереве недоступен).
- `x1` = arginfo (0x1117) (`qcom_scm-smc.c:170`).
- `x2`, `x3`, `x4` = аргументы 0, 1, 2 (`:171-172`).
- Аргументов больше четырёх, поэтому **`x5` = физ. адрес второго буфера**,
  куда кладутся аргументы 3…9 как **le64** (7 слотов по 8 байт; 3…6 —
  значения, остальное нули) (`:32-36`, `:174-200`). Этот буфер — тоже из
  tzmem, тоже некэшируемый.
- `x6` = 0 (сессия Qualcomm-причуды), `x7` = 0 (`:41-49`).
- Возврат: `x0` = статус, `x1…x3` = результаты (`:207-213`). Итог Linux —
  статус, если он не 0, иначе `x1` (`qcom_scm.c:1338-1340`).
- `x0 = 1` (INTERRUPTED) — повторить SMC с `x0 = 1` и `x6` из ответа
  (`qcom_scm-smc.c:46-54`); `x0 = 2` (WAITQ_SLEEP) — ждать и возобновить
  (`:93-118`); `x0 = −12` (EBUSY) — повтор до 20 раз через 30 мс (`:29-30`,
  `:140-145`). Прочие ошибки: −1 ERROR, −2 EINVAL_ARG, −3 EINVAL_ADDR,
  −4 EOPNOTSUPP, −5 ENOMEM (`qcom_scm.h:169-175`).
- Конвенция выбирается пробой `INFO/IS_CALL_AVAIL` (svc 0x06, cmd 0x01):
  сначала SMC64, затем SMC32, иначе legacy (`qcom_scm.c:238-300`). Для
  SMC32 расширенные аргументы — le32 (`qcom_scm-smc.c:186-191`).

---

## 4. QRTR: то, что нужно клиенту

### 4.1 Транспорт

На SDM660 служба WLFW живёт на модеме; QRTR к нему идёт каналом GLINK
**`IPCRTR`** (`git:net/qrtr/smd.c:289`) по ребру `modem`: `qcom,remote-pid =
<1>`, прерывание SPI 452, почтовый ящик `apcs_glb` бит 15
(`git:arch/arm64/boot/dts/qcom/sdm630.dtsi:1094-1098`). Каждое сообщение
канала — один пакет QRTR с заголовком.

### 4.2 Заголовок версии 1 (32 байта)

`af_qrtr.c:39-48`, заполнение — `af_qrtr.c:350-364`:

| Смещение | Поле | Тип |
|---|---|---|
| 0 | version = **1** | le32 |
| 4 | type (`QRTR_TYPE_*`) | le32 |
| 8 | src_node_id | le32 |
| 12 | src_port_id | le32 |
| 16 | confirm_rx (0/1) | le32 |
| 20 | size — длина полезной нагрузки **без** выравнивания | le32 |
| 24 | dst_node_id | le32 |
| 28 | dst_port_id | le32 |

Полезная нагрузка дополняется нулями до кратного 4 (`af_qrtr.c:366`).
Приём требует: длина пакета кратна 4, `size ≠ 0`,
`длина = ALIGN(size, 4) + 32` (`af_qrtr.c:445`, `:501`). Тип не DATA и не
RESUME_TX допустим только с `dst_port = CTRL` (`af_qrtr.c:509-511`).
`NEW_SERVER` и `RESUME_TX` короче 20 байт — отброс (`:504-507`).

Версия 2 (значение **3** в первом байте) тоже принимается: 16 байт —
u8 version, u8 type, u8 flags (бит 0 — confirm_rx), u8 optlen, le32 size,
le16 src_node, le16 src_port, le16 dst_node, le16 dst_port, затем `optlen`
байт (`af_qrtr.c:17-18`, `:62-74`, `:473-491`); порт 0xFFFF в v2 — это CTRL.
Linux **сам шлёт только v1** (`af_qrtr.c:351`).

### 4.3 Адреса

| Что | Значение | Где |
|---|---|---|
| Порт управления | 0xFFFFFFFE (`QRTR_PORT_CTRL`) | `qrtr.h:9` |
| Широковещательный узел | 0xFFFFFFFF | `qrtr.h:8` |
| Порт CTRL у старых | 0xFFFF → CTRL | `af_qrtr.c:26`, `:498-499` |
| Свой узел по умолчанию | **1** | `af_qrtr.c:102` |
| Эфемерные порты | 0x4000…0x7FFF | `af_qrtr.c:21-22` |
| Узел удалённой стороны | берётся из `src_node_id` первого пакета | `af_qrtr.c:515`, `:409-421` |

Управляющий пакет, адресованный в CTRL удалённого узла, уходит с
`dst_node = узел ребра`, `dst_port = CTRL` (`af_qrtr.c:355-357`).

### 4.4 Управляющие пакеты

Типы (`qrtr.h:17-29`): 1 DATA, **2 HELLO**, **3 BYE**, **4 NEW_SERVER**,
**5 DEL_SERVER**, **6 DEL_CLIENT**, **7 RESUME_TX**, 8 EXIT, 9 PING,
**10 NEW_LOOKUP**, 11 DEL_LOOKUP.

Тело — 20 байт (`qrtr.h:31-47`, `__packed`): le32 `cmd` (= тип) и объединение:

| Смещение | `server` (NEW/DEL_SERVER, NEW/DEL_LOOKUP) | `client` (BYE, DEL_CLIENT, RESUME_TX) |
|---|---|---|
| 4 | service | node |
| 8 | instance (версия \| экземпляр<<8) | port |
| 12 | node | 0 |
| 16 | port | 0 |

Отправитель управляющего пакета — порт CTRL своего узла; тип в заголовке
берётся из первых 4 байт тела (`af_qrtr.c:988-997`). HELLO — тело из нулей с
`cmd = 2` (`ns.c:329-350`).

### 4.5 Как клиент находит службу

В Linux поиск — **локальный**: клиент шлёт `NEW_LOOKUP(service, instance)`
своему узлу на CTRL (`qmi_interface.c:166-193`), локальная служба имён
принимает поиск **только от своего узла** (`ns.c:575-577`), отвечает всеми
известными подходящими серверами и пустым `NEW_SERVER` (node=0, port=0) как
концом списка (`ns.c:598-608`; клиент его пропускает,
`qmi_interface.c:44-46`). Совпадение: `service` равен, `instance` равен
точно, если задан (`ns.c:116-127`, `:529-532`).

Знание об удалённых серверах служба имён получает с ребра:

1. на HELLO удалённого узла отвечает своим HELLO и объявлением своих
   серверов (`ns.c:353-362`); при старте шлёт HELLO широковещательно
   (`ns.c:777`);
2. удалённая служба имён присылает `NEW_SERVER` на CTRL — сервер
   запоминается (`ns.c:499-538`, `:240-294`), и подходящим поискам уходит
   уведомление;
3. `DEL_SERVER` — сервер убран (`ns.c:540-563`); `BYE` от узла — убраны все
   его серверы (`ns.c:364-…`).

**Своему драйверу без локального роутера** этого достаточно (выведено из
кода выше; на эталоне проверить порядок HELLO):

1. открыть канал `IPCRTR`;
2. обменяться HELLO с модемом: свой — v1, type 2, src = (1, CTRL), dst =
   (узел модема, CTRL), size 20, тело `02 00 00 00` + 16 нулей;
3. собирать `NEW_SERVER` на CTRL; нужный — `service = 0x45`,
   `instance = 0x00000001`; запомнить его `node`, `port`;
4. QMI ходит пакетами **DATA** (type 1): src = (1, свой порт, например
   0x4000), dst = (node, port) службы; ответы и индикации приходят DATA на
   свой порт;
5. `DEL_SERVER` с этим port или `BYE` модема — служба ушла (подсистема
   перезапущена): права MSA вернуть, всё сначала;
6. при закрытии своего порта Linux рассылает `DEL_CLIENT` {node = 1,
   port = свой} (`af_qrtr.c:698-718`).

### 4.6 Поток: confirm_rx и RESUME_TX

- **Приём:** если у пришедшего DATA `confirm_rx = 1`, надо ответить
  `RESUME_TX`: заголовок src = (свой узел, порт-получатель), dst = (узел и
  порт отправителя), тело `cmd = 7`, `client.node/port` = свой узел и
  порт-получатель (`af_qrtr.c:1011-1039`, вызов — `:1088-1089`).
- **Передача:** Linux считает DATA на каждую пару (узел, порт) получателя;
  на 5-м неподтверждённом ставит `confirm_rx = 1`, на 10-м останавливается до
  `RESUME_TX` (тот обнуляет счётчик) (`af_qrtr.c:151-152`, `:256-306`,
  `:220-239`). Ответы QMI счётчик **не** сбрасывают. Не-DATA пакеты
  `confirm_rx` не несут (`:264-266`).

---

## 5. Что проверить на эталоне (postmarketOS на Mi Pad 4)

1. **Служба и узлы:** `qrtr-lookup` — строка службы 69 (`0x45`): версия 1,
   экземпляр 0, узел модема и порт. Узел модема (0? 1?) нужен для HELLO.
2. **Дерево:** `/proc/device-tree/soc@0/wifi@18800000/` — наличие
   `qcom,snoc-host-cap-8bit-quirk`, `qcom,no-msa-ready-indicator`,
   отсутствие `qcom,msa-fixed-perm`, `qcom,xo-cal-data`, `wifi-firmware`;
   `/proc/iomem` — `85700000-857fffff`.
3. **Журнал QMI ath10k:** `modprobe ath10k_core debug_mask=0x00200000`
   (`ATH10K_DBG_QMI`, `git:…/ath10k/debug.h:36`; SNOC — 0x00100000) и
   перезапуск модема/драйвера: адреса и `secure_flag` областей MSA
   («qmi msa mem region …», `qmi.c:183-187`), `chip_id`/`board_id`/`soc_id`/
   `fw_version`/`fw_build_id` (`qmi.c:635-639`), «not waiting for msa_ready».
4. **SCM:** `dmesg | grep -i scm` — «qcom_scm: convention: smc arm 64»
   (`qcom_scm.c:296-298`) и «SHM Bridge not supported» (tzmem).
5. **Байты на проводе:** штатного дампа нет. Варианты: `bpftrace`/kprobe на
   `qcom_smd_qrtr_send` (`git:net/qrtr/smd.c:241`, буфер `skb->data`,
   `skb->len`) и на приёмник `qrtr_endpoint_post` (`af_qrtr.c:431`) — снять
   полные пакеты IND_REGISTER … WLAN_MODE: версию заголовка модема (1 или 3),
   первый `txn_id` (0 или 1), порядок HELLO, наличие `confirm_rx`, точные
   `msg_len` (сверить с 15, 4, 18, 552, 11 выше).
6. **INI:** значение `enablefwlog` в пакете 0x2F (константы
   `ATH10K_ENABLE_FW_LOG_*` в доступных исходниках не найдены).
7. **BDF:** длина `board-2.bin`-секции, число сегментов, ответ на последнем
   сегменте (MALFORMED или успех) — от этого зависит, нужна ли причуда CRC.
8. **Тайминги:** промежутки MSA_INFO → assign → MSA_READY и CAL_REPORT →
   FW_READY_IND (из меток dmesg с `debug_mask`).
