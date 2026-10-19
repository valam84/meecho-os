# WCN3990 (SNOC, non-BMI): от «CE подняты» до готовой прошивки — проводная спецификация

Справка для драйвера MEECHO. Источник протокольных фактов — ath10k (GPL): отсюда
берутся **только числа и порядок**, не текст. Все ссылки `файл:строка` — на копию
в `~/wifi-ref/ath10k/`. Сверка с живой системой — снимок postmarketOS
`port/clover/pmos-20261019/pass1/wifi/` (debugfs, dmesg, ethtool).

**Чего в копии нет:** `wmi.c`, `debug.c`, `hif.h`. Всё, что опирается на них
(обёртка отправки WMI, разбор SERVICE_READY/READY в общем коде, формат
`fw_checksums`), помечено **[wmi.c — по памяти, проверить]**.

Отправная точка документа: QMI `WLAN_MODE` (mission) прошёл, таблица CE и карта
«сервис → канал» отданы прошивке (`snoc.c:971-1019`), кольца CE
инициализированы (`snoc.c:1112-1120`). Board data к этому моменту **уже**
загружена в прошивку по QMI (BDF download идёт по `MSA_READY`, до `WLAN_MODE`:
`qmi.c:918-934`), но разбор `board-2.bin` описан здесь же, раз о нём спрошено.

---

## 1. Контейнеры файлов

### 1.1. `firmware-N.bin`

Имя: `firmware-%d.bin` для SNOC (`core.c:2196-2201`), каталог
`ath10k/WCN3990/hw1.0` (`hw.h:125`). Перебор API от 6 к 2: сначала ищется
`firmware-6.bin`, потом `-5` и т.д. (`core.c:2213-2223`, `hw.h:128-129`).

Формат (`core.c:2009-2184`):

| Смещение | Что | Ссылка |
|---|---|---|
| 0 | `"QCA-ATH10K\0"` — 11 байт **вместе с NUL**, сравнивается с NUL | `hw.h:149`, `core.c:2028-2041` |
| 11 | дополнение до 4 → данные IE с 12 | `core.c:2044-2047` |
| 12… | цепочка IE: `le32 id`, `le32 len`, `len` байт данных, дополнение до 4 | `hw.h:158-162`, `core.c:2050-2168` |

Цикл идёт, пока остаток `> 8`. Длина проверяется **без** выравнивания
(`len < ie_len`, `core.c:2059`), а пропуск — **с** выравниванием
(`core.c:2165-2168`): у файла без хвостового дополнения `size_t len` уйдёт в
переполнение. Наш разборщик обязан проверять `len >= ALIGN(ie_len,4)` или
обрезать пропуск по остатку.

ID элементов (`hw.h:164-183`):

| id | имя | содержимое | для WCN3990 |
|---|---|---|---|
| 0 | FW_VERSION | строка без NUL, `len < sizeof(fw_version)` (32), иначе игнор | `core.c:2067-2077`. В снимке строка **пустая** (`dmesg`: `firmware ver  api 5`) |
| 1 | TIMESTAMP | `le32`, принимается только при `len==4` | `core.c:2078-2086` |
| 2 | FEATURES | битовая карта: бит *i* = байт *i/8*, бит *i%8*; обход до `ATH10K_FW_FEATURE_COUNT` или конца IE | `core.c:2087-2110` |
| 3 | FW_IMAGE | образ для BMI-загрузки | **не нужен**: при `NON_BMI` его отсутствие не ошибка (`core.c:2171-2177`); образ живёт в модеме |
| 4 | OTP_IMAGE | OTP для BMI | не нужен |
| 5 | WMI_OP_VERSION | `le32`, только при `len==4` | **4 = TLV** (`hw.h:191`), `core.c:2129-2139` |
| 6 | HTT_OP_VERSION | `le32`, только при `len==4` | **3 = TLV** (`hw.h:207`), `core.c:2140-2150` |
| 7 | FW_CODE_SWAP_IMAGE | code swap для BMI | не нужен |

Неизвестный id — предупреждение и пропуск (`core.c:2158-2161`).

Биты FEATURES (`core.h:744-842`), которые несёт файл платы (по `dmesg`:
`wowlan,no-4addr-pad,allows-mesh-bcast,mgmt-tx-by-reference,non-bmi,single-chan-info-per-channel`):
6 WOWLAN (`core.h:771`), 8 NO_NWIFI_DECAP_4ADDR_PADDING (`:779`),
16 ALLOWS_MESH_BCAST (`:821`), 18 MGMT_TX_BY_REF (`:827`), 19 NON_BMI (`:830`),
20 SINGLE_CHAN_INFO_PER_CHANNEL (`:833`). **Ожидаемые байты IE 2:
`40 01 1d` (+ возможно `00`)** — выведено из списка, сам файл не читался.

Размер файла на устройстве — **60 байт** (`pass1/fw/firmware-list.txt`):
12 (магия) + 12 (FEATURES, 3→4) + 12 (WMI_OP) + 12 (HTT_OP) = 48, остаются 12 —
ещё один IE с 4 байтами данных (TIMESTAMP или пустая/нулевая версия). CRC32
файла `022c9087` (`dmesg`, `fw_checksums.txt`).

Если IE 5/6 нет — выводятся из старых бит (`core.c:2717-2727`, `2803-2822`);
для нас не случается. После разбора при `WMI_OP=TLV` (`core.c:2754-2769`):
`max_num_peers = hw_params.num_peers = 33` (`core.c:732,2795-2796`,
`hw.h:768`), `max_num_stations=32`, `max_num_vdevs=4`, `max_num_pending_tx =
TARGET_TLV_NUM_MSDU_DESC = 1056` (не SDIO; `hw.h:762`), `wow patterns=22`,
`mgmt_max_num_pending_tx=50`; `rx_decap_mode = NATIVE_WIFI = 1`
(`core.c:2665`, `hw.h:423`); `max_num_amsdu=3`, `max_num_ampdu=64`
(`core.c:2687-2688`, `htt.h:2419-2420`). `dmesg` подтверждает:
`htt-ver 3.56 wmi-op 4 htt-op 3 … max-sta 32 raw 0 hwcrypto 1`.

### 1.2. `board-2.bin`

Формат (`core.c:1486-1559`, `1428-1484`, `1338-1426`):

| Смещение | Что |
|---|---|
| 0 | `"QCA-ATH10K-BOARD\0"` — 17 байт с NUL (`hw.h:150`, `core.c:1508-1520`) |
| 17 | дополнение до 4 → IE с **20** (`core.c:1523-1532`) |
| 20… | внешние IE `le32 id, le32 len, data, pad4`: id 0 = BOARD, id 1 = BOARD_EXT (`hw.h:215-219`) |

Внутри внешнего IE — вложенные IE той же формы: id 0 = BOARD_NAME (строка без
NUL), id 1 = BOARD_DATA (`hw.h:221-224`). Порядок важен: DATA принимается,
только если **предыдущий** NAME в этом же внешнем IE совпал
(`core.c:1385-1407`). Совпадение — точная длина (`len == strlen`) и `memcmp`,
регистр значим (`core.c:1373-1380`). Здесь длина проверяется **с**
выравниванием (`core.c:1361`, `1445`).

Имя платы для SNOC/QMI (`core.c:1561-1605`): `qmi_ids_valid` ставит
`qmi.c:868-870` (`board_id`, `chip_id` из QMI-ответа), шина — `snoc`
(`qmi.c:867`). Шестнадцатеричные числа строчными без `0x` (`%x`):

1. `bus=snoc,qmi-board-id=%x,qmi-chip-id=%x[,variant=V]` (`core.c:1583-1587`)
2. то же без `variant` (`core.c:1640-1642`)
3. `bus=snoc,qmi-board-id=%x` (`core.c:1589-1592`, `1650-1652`)

Поиск — три прохода целиком по файлу, в этом порядке (`core.c:1535-1542`).
`variant` берётся из DT: `qcom,calibration-variant`, иначе
`qcom,ath10k-calibration-variant` (`core.c:1158-1180`, зовётся из
`qmi.c:873`).

**Для clover:** `dmesg`: `qmi chip_id 0x30214 … board_id 0xff`; в DT
(`sdm660-xiaomi-clover-pmos.dts`, узел `wifi@18800000`) свойства variant нет.
Значит ищется **`bus=snoc,qmi-board-id=ff,qmi-chip-id=30214`**, запасное —
`bus=snoc,qmi-board-id=ff`. Файл на устройстве — 480048 байт.

Запасной путь при отсутствии файла или совпадения: API 1 — сначала
`board-snoc-<dev_name>.bin` (для clover `board-snoc-18800000.wifi.bin`),
потом `board.bin` целиком (`core.c:1300-1323`, `1675-1682`).
`WCN3990_BOARD_DATA_SZ = 26328` (`targaddrs.h:494`), `EXT = 0` (`:495`).

**Сверка:** `fw_checksums.txt`: `board-N.bin ecd45d37` (весь `board-2.bin`),
`board 380d3b6d` (выбранный BOARD_DATA). Наш разборщик, выбравший верный
блок, должен дать ту же CRC. **Ловушка [debug.c — по памяти, проверить]:**
ath10k считает `crc32_le(0, data, len)` — сырой отражённый CRC-32 с нулевым
начальным значением **без** финальной инверсии; это не `zlib crc32()`
(у того начальное `~0` и `~` в конце). Сравнивать надо тем же способом.

---

## 2. HTC

### 2.1. Каналы (CE) для SNOC

Карта «сервис → CE», которую хост отдаёт прошивке по QMI
(`snoc.c:366-473`):

| Сервис (`htc.h:270-295`) | id | UL (host→tgt) | DL (tgt→host) |
|---|---|---|---|
| RSVD_CTRL | `0x0001` | CE0 | CE2 |
| WMI_CONTROL | `0x0100` | CE3 | CE2 |
| WMI_DATA_BE/BK/VI/VO | `0x0101`…`0x0104` | CE3 | CE2 |
| HTT_DATA_MSG | `0x0300` | CE4 | CE1 |
| HTT_DATA2_MSG | `0x0301` | — | CE9 |
| HTT_DATA3_MSG | `0x0302` | — | CE10 |
| HTT_LOG_MSG (pktlog) | `0x0600` | — | CE11 |
| TEST_RAW_STREAMS | `0xFE00` | CE0 **и** CE5 | CE2 |

`SVC(group, idx) = group<<8 | idx` (`htc.h:270-271`). Подключить по HTC можно
только сервис, у которого есть **и** UL, и DL (`snoc.c:816-817`): поэтому
DATA2/DATA3/LOG не подключаются никогда, а проверка pktlog в `htc_start`
(`htc.c:1279-1285`, `1227-1244`) на SNOC тихо ложна. Сообщения с CE9/CE10/CE11
всё равно идут через общий HTC-приёмник (`snoc.c:617-633`) и разбираются по
`eid` из заголовка. CE5 особый: 8 байт HTC-заголовка отрезаются **без
проверки**, остальное — прямо в HTT T2H (`snoc.c:635-645`).

Хостовые кольца (`snoc.c:137-242`): CE0 src 16×2048; CE1 dst 512×2048;
CE2 dst 64×2048; CE3 src 32×2048; CE4 src 2048×256, **без прерывания**
(завершения опрашиваются при обработке CE1/CE5/CE9/CE10, `snoc.c:617-623`,
`CE_POLL_PIPE 4` — `snoc.c:29`); CE5 dst 512×512; CE9/10/11 dst 512×2048.
Приёмные буферы размера `src_sz_max` (`snoc.c:1430`) постятся во все
приёмные кольца в `hif_start` **до** ожидания HTC READY (`snoc.c:935-953`,
`562-569`).

### 2.2. Заголовок HTC — 8 байт, little-endian (`htc.h:74-94`)

| Байт | Поле | TX (host→tgt) | RX (tgt→host) |
|---|---|---|---|
| 0 | `eid` | endpoint | endpoint |
| 1 | `flags` | `0x01` NEED_CREDIT_UPDATE, `0x02` SEND_BUNDLE (`htc.h:45-48`) | `0x01` RECV_1MORE_BLOCK, `0x02` TRAILER_PRESENT; биты 7:4 (+3:2) — счёт пачки (`htc.h:50-59`) |
| 2-3 | `len` le16 | длина **после** заголовка (`htc.c:98`) | полезная нагрузка **+ трейлер** |
| 4 | `control_byte0` | 0 | `trailer_len` |
| 5 | `control_byte1` | `seq_no`, свой счётчик на endpoint (`htc.c:103-105`) | — |
| 6-7 | `pad_len` le16 | 0 (кроме пачек) | — |

TX: `NEED_CREDIT_UPDATE` ставится, если у endpoint включён кредитный поток и он
не пачечный (`htc.c:100-101`). Выравнивание всего на 4 (`htc.h:39-40`,
`htc.c:1189-1191`). RX: `len + 8 <= 4096` (`htc.c:471-477`, `htc.h:340`),
кадр короче `len` отбрасывается (`htc.c:479-486`); сообщение на
неподключённый endpoint отбрасывается (`htc.c:463-467`).

### 2.3. Сообщения управления (endpoint 0)

Тело начинается с `le16 message_id` (`htc.h:140-142`, `96-103`):
1 READY, 2 CONNECT_SERVICE, 3 CONNECT_SERVICE_RESP, 4 SETUP_COMPLETE,
5 SETUP_COMPLETE_EX, 6 SEND_SUSPEND_COMPLETE.

Endpoint 0 подключается **локально**, без обмена: `htc_init` делает фиктивное
подключение RSVD_CTRL — eid 0, `max_msg 256`, кредитный поток выключен
(`htc.c:1291-1323`, `1033-1040`, `1169-1174`).

**READY (tgt→host), ≥ 8 байт** (`htc.h:149-168`, `htc.c:913-1002`):

| Смещ. | Поле |
|---|---|
| 0 | `le16 message_id = 1` |
| 2 | `le16 credit_count` |
| 4 | `le16 credit_size` |
| 6 | `u8 max_endpoints` |
| 7 | pad |
| 8 | *(расш., если тело ≥ 12)* `u8 htc_version` (0 = 2.0, 1 = 2.1; `htc.h:105-108`) |
| 9 | `u8 max_msgs_per_htc_bundle` (берётся `min(…,32)`) |
| 10 | `le16`: биты 11:0 — `alt_data_credit_size` (`htc.h:122`) |

Расширенный вид распознаётся **только по длине** (`htc.c:981-996`).
`credit_size == 0` или итоговые кредиты 0 — ошибка (`htc.c:975-979`).
**WCN3990: `use_fw_tx_credits = false` (`core.c:746`) → `credit_count` из
READY игнорируется, всего кредитов = 1** (`htc.c:962-965`). Ожидание READY —
1 с, потом опрос всех CE и ещё 1 с (`htc.c:921-941`, `htc.h:342`).

**CONNECT_SERVICE (host→tgt), 8 байт** (`htc.h:170-175`, `htc.c:1049-1081`):
`le16 id=2`, `le16 service_id`, `le16 flags`, 2 байта pad.
`flags` (`htc.h:110-120`): биты 15:8 — RECV_ALLOC (кредиты, выдаваемые
сервису), бит 3 — DISABLE_CREDIT_FLOW_CTRL, биты 1:0 — порог.
ath10k даёт все кредиты только WMI_CONTROL (`htc.c:627-639`), всем прочим
ставит бит 3 (`htc.c:1065-1069`). Для WCN3990 получается:

* HTT_DATA_MSG `0x0300`: `flags = 0x0008`
* WMI_CONTROL `0x0100`: `flags = 0x0100` (RECV_ALLOC = 1)

**CONNECT_SERVICE_RESP (tgt→host), ≥ 8 байт** (`htc.h:177-182`,
`htc.c:1091-1120`): `le16 id=3`, `le16 service_id`, `u8 status`, `u8 eid`,
`le16 max_msg_size`. Статусы (`htc.h:124-130`): 0 SUCCESS, 1 NOT_FOUND,
2 FAILED, 3 NO_RESOURCES, 4 NO_MORE_EP. Отказ — `eid >= 9`, `max_msg_size
== 0` или повторное занятие endpoint (`htc.c:1124-1134`). Ожидание — 1 с
(`htc.h:345`). Номер endpoint назначает **прошивка**; брать из ответа.

**SETUP_COMPLETE_EX (host→tgt), 12 байт** (`htc.h:184-192`,
`htc.c:1246-1277`): `le16 id=5`, 2 pad, `le32 flags`, `u8
max_msgs_per_bundled_recv`, 3 pad. Для SNOC всё, кроме id, — нули (флаг
RX_BNDL_EN только для SDIO, `htc.c:1264-1270`). Ответа нет.

### 2.4. Кредиты

Кредиты на SNOC **есть, но только у WMI**: у каждого endpoint по умолчанию
поток включён (`htc.c:623`), подключение выключает его всем, кроме
WMI_CONTROL (`htc.c:1065-1069`, `1169-1174`). Стоимость отправки —
`DIV_ROUND_UP(len_с_HTC_заголовком, tx_credit_size)` (`htc.c:117-136`),
при нехватке — `-EAGAIN`. Стартовый запас WMI = RECV_ALLOC = 1
(`htc.c:1144`), размер кредита — `credit_size` из READY (`htc.c:1145`).

**Следствие для WCN3990: одна WMI-команда в полёте, и команда длиннее
`credit_size` не уйдёт никогда.** Кредит возвращается записью CREDITS в
трейлере входящего кадра (`htc.c:248-281`). У HTT кредитного потока нет, пока
`hif_get_htt_tx_complete` ложен — у SNOC такой операции нет вовсе
(`snoc.c:1187-1206`, `htt.c:319-321`) **[hif.h не извлечён: умолчание
«ложь» — по памяти]**.

### 2.5. Трейлер на приёме (`htc.c:347-436`, `488-514`)

При `flags & 0x02`: последние `trailer_len` байт нагрузки (`4 <= trailer_len
<= len`) — цепочка записей `{u8 id, u8 len, 2 pad}` + `len` байт
(`htc.h:209-221`):

* 1 CREDITS: массив `{u8 eid, u8 credits, 2 pad}` — прибавить к endpoint
  (`htc.h:223-228`, `htc.c:264-279`)
* 2 LOOKAHEAD: `{pre_valid, 3 pad, lookahead[4], post_valid, 3 pad}`; годна,
  только если `pre_valid == ~post_valid` (`htc.h:230-240`, `htc.c:298-299`).
  Прошивка шлёт негодную на каждой загрузке — молча пропускать
* 3 LOOKAHEAD_BUNDLE: массив по 4 байта; при числе 0 или больше
  `max_msgs_per_htc_bundle` — `-EINVAL` и **весь кадр выбрасывается**
  (`htc.c:326-330`, `510-511`)

На SNOC lookahead только проверяется: сюда передаётся `NULL`
(`htc.c:507-509`). После разбора трейлер обрезается; кадр с одним трейлером
без нагрузки выбрасывается (`htc.c:513-518`).

---

## 3. HTT (htt-op 3 = TLV, версия 3.56)

### 3.1. Когда и что

HTT на этом пути делается **в два приёма**: память — до HTC READY
(`core.c:3065-3087`), сообщения — **после** WMI READY, в самом конце
`core_start` (`core.c:3255-3275`, `htt.c:391-437`).

Набор T2H типов — таблица TLV (`htt.c:64-94`, `353-356`, `htt.h:434-461`).
WCN3990: `target_64bit = true` (`core.c:736`) → все H2T варианты **64-битные**
(`htt_tx.c:1812-1821`, `1831-1841`); `continuous_frag_desc = true`
(`core.c:719`) → FRAG_DESC_BANK_CFG отправляется.

H2T: заголовок — **один байт** `msg_type` (`htt.h:52-54`), структура
`htt_cmd` упакована (`htt.h:1686-1702`). Отправка — обычный HTC на endpoint
HTT, то есть CE4. Типы H2T (`htt.h:34-50`): 0 VERSION_REQ, 1 TX_FRM,
2 RX_RING_CFG, 3 STATS_REQ, 4 SYNC, 5 AGGR_CFG, 6 FRAG_DESC_BANK_CFG,
7 MGMT_TX, 11 TX_FETCH_RESP.

T2H: первый байт — `msg_type` (`htt.h:536-538`), полезное — сразу за ним
(`htt.h:1704-1731`); приходит по HTC на endpoint HTT (CE1, также CE9/10) или
через CE5 без HTC.

### 3.2. Память, которую хост держит до старта

* **Кольцо приёма** (`htt_rx.c:787-868`): `size = 2048` (`htt.h:239-240`),
  массив адресов `2048 × le64` = 16 КиБ когерентной памяти
  (`htt_rx.c:86-88`, `109-113`); слово `alloc_idx` (`le32`, когерентное) —
  «индекс прошивки» в памяти хоста, обнуляется (`htt_rx.c:826-835`).
  Уровень заполнения `rx_ring_fill_level = HTT_RX_RING_FILL_LEVEL_DUAL_MAC =
  2047` (`core.c:737`, `htt.h:242`) — **почти всё кольцо**, хотя комментарий
  у `fill_n` требует «держать наполовину пустым» (`htt_rx.c:144-149`, это
  про `HTT_RX_RING_FILL_LEVEL = 1023`).
* **Буфер приёма** (`htt_rx.c:135-211`): 2048 + 8 байт, начало выровнено на 8
  (`htt.h:2384`, `2393`); слово флагов `rx_attention` обнуляется до постановки;
  отображается на устройство на всю длину; адрес → `paddrs[idx]`, затем
  барьер и `*alloc_idx = idx` (`htt_rx.c:203-209`). Наполнение — до READY не
  делается; делается после READY перед HTT setup (`core.c:3255`,
  `htt_rx.c:268-286`).
* **Frag desc bank** (`htt_tx.c:349-370`): `max_num_pending_tx ×
  sizeof(htt_msdu_ext_desc_64)` = 1056 × 72 = 76032 байт, одним куском
  (`htt.h:100-106`). Плюс `txbuf_64` той же кратности (`htt_tx.c:272-289`) —
  внутреннее хоста.
* Очередь состояния (`tx_q_state`) — только при `PEER_FLOW_CONTROL`
  (`htt_tx.c:387-416`); у WCN3990 бита нет → поля нули.

**Полный переупорядочиватель (in-order):** `in_ord_rx` берётся из
`WMI_SERVICE_RX_FULL_REORDER` (`core.c:3080-3081`) — **но до SERVICE_READY
текущей загрузки**, то есть из прошлого прохода: на первом `core_start`
(проба) он ложен, на втором (`ath10k_start`) — истинен. В снимке служба
включена (`wmi_services.txt`), значит рабочий режим — in-order: каждый
поставленный буфер заносится в хеш по физическому адресу
(`htt_rx.c:192-196`). Нашему драйверу решать после SERVICE_READY.

**SMMU:** узел `wifi@18800000` стоит за `iommus = <0x84 0x1a00 0x84 0x1a01>`
(DTS) — все адреса выше (кольцо, буферы, `alloc_idx`, frag desc, куски памяти
WMI) прошивка видит как **IOVA**.

### 3.3. Сообщения H2T при старте (порядок `htt.c:398-434`)

**1. VERSION_REQ** — 4 байта: `00 00 00 00` (`htt.h:56-58`,
`htt_tx.c:604-630`). Ответ ждать 3 с (`htt.c:372`, `402-407`).

**VERSION_CONF (T2H 0x00)** — `[0]=0x00`, `[1]=minor`, `[2]=major`, `[3]=rsvd`
(`htt.h:544-549`, `htt_rx.c:4123-4128`). Major должен быть 2 или 3
(`htt.c:381-387`). **Сверено:** `dmesg` — `htt-ver 3.56` → `minor=0x38,
major=0x03`.

**2. FRAG_DESC_BANK_CFG (6), 64-битный** — 64 байта (`htt.h:1509-1516`,
`htt_tx.c:738-798`):

| Смещ. | Поле | Значение для WCN3990 |
|---|---|---|
| 0 | msg_type | 6 |
| 1 | info (`htt.h:1458-1463`) | 0 (тип глубины 0, без PEER_FLOW_CONTROL) |
| 2 | num_banks | 1 |
| 3 | desc_size | 72 |
| 4 | `le64 bank_base_addrs[4]` | [0] = адрес frag desc, остальные 0 |
| 36 | `{le16 min, le16 max} bank_id[4]` | [0] = {0, 1055} |
| 52 | q_state: `le32 paddr, le16 num_peers, le16 num_tids, u8 record_size, u8 multiplier, 2 pad` | 0, 0, 0, **1**, 0 (`htt.h:1473-1474`, `1491-1498`) |

**3. RX_RING_CFG (2), 64-битный** — 48 байт (`htt.h:244-293`,
`htt_tx.c:885-953`):

| Смещ. | Поле | Значение |
|---|---|---|
| 0 | msg_type | 2 |
| 1 | `u8 num_rings` | 1 |
| 2 | `le16 rsvd0` | 0 |
| 4 | `le64 fw_idx_shadow_reg_paddr` | адрес слова `alloc_idx` |
| 12 | `le64 rx_ring_base_paddr` | адрес массива адресов |
| 20 | `le16 rx_ring_len` | **2048** — число элементов, хотя комментарий говорит «в словах» (`htt.h:272`, `htt_tx.c:940`) |
| 22 | `le16 rx_ring_bufsize` | 2048 |
| 24 | `le16 flags` | `0xFFFF` — все 16 бит (`htt.h:219-236`, `htt_tx.c:918-934`) |
| 26 | `le16 fw_idx_init_val` | текущее `*alloc_idx` (после наполнения — 2047) |
| 28 | 10 × `le16` смещений в **4-байтных словах** | см. ниже |

Смещения дескриптора WCN3990 (`htt_rx_desc_v2`, `htt.h:2111-2126`,
`htt.c:168-182`; **посчитаны gcc по `rx_desc.h`**, размер дескриптора
360 байт): mac80211_hdr (`rx_hdr_status`) **74**, msdu_payload **90**,
ppdu_start **27**, ppdu_end **37**, mpdu_start **4**, mpdu_end **26**,
msdu_start **7**, msdu_end **12**, rx_attention **1**, frag_info **2**.
Нагрузка буфера: 2048 − 360 = 1688 байт (`htt.h:2398-2401`).

**4. AGGR_CFG (5), v2** — 4 байта: `05 40 03 00` (A-MPDU 64, A-MSDU 3,
reserved; `htt.h:355-360`, `htt_tx.c:1053-1098`).

Ни на 2–4 ответа нет.

### 3.4. T2H, по которым приходят кадры

Типы TLV (`htt.h:434-461`): 0x00 VERSION_CONF, 0x01 RX_IND, 0x02 RX_FLUSH,
0x03 PEER_MAP, 0x04 PEER_UNMAP, 0x05/0x06 ADDBA/DELBA, 0x07 TX_COMPL_IND,
0x08 PKTLOG, 0x09 STATS_CONF, 0x0a RX_FRAG_IND, 0x0b SEC_IND, 0x0e
MGMT_TX_COMPL_IND, 0x0f TX_CREDIT_UPDATE_IND, 0x10 RX_PN_IND, 0x11
RX_OFFLOAD_DELIVER_IND, **0x12 RX_IN_ORD_PADDR_IND**, 0x14
WDI_IPA_OP_RESPONSE, 0x15 CHAN_CHANGE, 0x16 RX_OFLD_PKT_ERR. Тип ≥
`HTT_TLV_T2H_NUM_MSGS` — пропуск (`htt_rx.c:4115-4119`).

**RX_IN_ORD_PADDR_IND (0x12)** — основной путь при полном переупорядочивании
(`htt.h:1090-1123`, `htt_rx.c:3270-3330`, `722-785`):
`[0]=0x12`, `[1] info` (биты 4:0 TID, 5 OFFLOAD, 6 FRAG), `[2] le16 peer_id`,
`[4] vdev_id`, `[5] rsvd`, `[6] le16 msdu_count`, с `[8]` — `msdu_count`
записей по 12 байт: `le64 msdu_paddr`, `le16 msdu_len`, `u8 fw_desc`,
`u8 rsvd`. Хост находит свой буфер **по физическому адресу**; без OFFLOAD в
буфере сначала 360 байт дескриптора, кадр — дальше, `msdu_len` байт; бит
`MSDU_DONE` в `rx_attention` обязан стоять. (Проверка длины в ath10k
меряет 8-байтными записями даже в 64-битном виде — `htt_rx.c:3304`.)

RX_IND (0x01) — путь без переупорядочивания (`htt_rx.c:4129-4136`).
Кадры управления у TLV-прошивки приходят по **WMI** `MGMT_RX_EVENTID =
0x7001` (`wmi-tlv.h:322`, `wmi-tlv.c:666-669`).

---

## 4. WMI-TLV

### 4.1. Обёртка

**Команда/событие:** `HTC(8) | le32 cmd_id | TLV…`. `cmd_id`: биты 23:0 —
идентификатор, биты 31:24 — PLT_PRIV (`wmi.h:55-62`); разбор событий так и
маскирует (`wmi-tlv.c:645-649`). Endpoint — назначенный для WMI_CONTROL, CE3
наружу, CE2 внутрь. Заголовок команде добавляет `ath10k_wmi_cmd_send`
**[wmi.c — по памяти: PLT_PRIV = 0, ожидание кредита до 3 с]**.

**TLV:** `le16 len`, затем `le16 tag` — **длина первой** (`wmi-tlv.h:1640-1644`);
`len` — без 4-байтного заголовка. Разбор: остаток ≥ 4, `len ≤ остатка`,
минимальная длина для известных тегов (`wmi-tlv.c:71-122`, `28-69`).

Номера: `WMI_TLV_CMD(g) = WMI_TLV_EV(g) = g<<12 | 1` (`wmi-tlv.h:13-14`),
группы с 3 (`wmi-tlv.h:22-70`). Теги (`wmi-tlv.h:585-…`, посчитаны по enum):
0x10 ARRAY_UINT32, 0x11 ARRAY_BYTE, 0x12 ARRAY_STRUCT, 0x20 SERVICE_READY_EVENT,
0x21 HAL_REG_CAPABILITIES, 0x22 WLAN_HOST_MEM_REQ, 0x23 READY_EVENT,
0x36 ECHO_EVENT, 0x4a INIT_CMD, 0x4b RESOURCE_CONFIG, 0x4c
WLAN_HOST_MEMORY_CHUNK, 0x51 PDEV_SET_REGDOMAIN_CMD, 0x52 PDEV_SET_PARAM_CMD,
0x73 PDEV_SUSPEND_CMD, 0x95 ECHO_CMD, 0x22f SERVICE_AVAILABLE_EVENT.

События (`wmi-tlv.h:296-…`): **0x1 SERVICE_READY**, **0x2 READY**,
**0x3 SERVICE_AVAILABLE**, 0x7001 MGMT_RX, 0x1d001 ECHO, 0x1d003 DEBUG_MESG,
0x1d004 UPDATE_STATS. Диспетчер — `wmi-tlv.c:665-805`.

Команды (`wmi-tlv.h:72-…`): **0x1 INIT** (`:73`), 0x4001 PDEV_SET_REGDOMAIN
(`:80`), 0x4003 PDEV_SET_PARAM (`:82`), 0x400d PDEV_SET_BASE_MACADDR (`:92`),
0x5001 VDEV_CREATE (`:97`), 0x7008 MGMT_TX_SEND (`:131`), 0x11001
PDEV_SUSPEND (`:175`), 0x16001 REQUEST_STATS (`:196`), 0x1d001 ECHO (`:227`),
0x21001 RESMGR_ADAPTIVE_OCS (`:250`).

### 4.2. SERVICE_AVAILABLE (0x3) — может прийти первым

Флаги служб ≥128 приходят здесь, а не в SERVICE_READY, и событие
«непосредственно предшествует» SERVICE_READY (`wmi-tlv.h:1390-1398`). ath10k
читает в TLV 0x22f первое `le32` как длину, биты — со следующего слова
(`wmi-tlv.c:1422-1455`), и сопоставляет 4 службы: 140 THERM_THROT, 155
SPOOF_MAC, 174 TX_DATA_MGMT_ACK_RSSI, 179 SUPPORT_EXTEND_ADDRESS
(`wmi-tlv.h:1622-1636`; бит = `(id−128)%32`, слово `(id−128)/32`,
`wmi-tlv.h:1456-1460`). В снимке все четыре «-», так что раскладку этого
события снимок не подтверждает и не опровергает.

### 4.3. SERVICE_READY (0x1) (`wmi-tlv.c:1279-1390`)

TLV в теле (порядок как у прошивки; ath10k узнаёт их по тегам, а два
`ARRAY_UINT32` — по очерёдности):

1. 0x20 `wmi_tlv_svc_rdy_ev`, 104 байта (`wmi-tlv.h:1721-1739`):
   `+0 fw_build_vers`, `+4..+27 abi{ver0, ver1, ns0..ns3}`,
   `+28 phy_capability`, `+32 max_frag_entry`, **`+36 num_rf_chains`**,
   **`+40 ht_cap_info`**, **`+44 vht_cap_info`**, `+48 vht_supp_mcs`,
   `+52 hw_min_tx_power`, `+56 hw_max_tx_power`, `+60 sys_cap_info`
   (бит 1 — RFKILL, `wmi-tlv.h:2396`), `+64 min_pkt_size_enable`,
   `+68 max_bcn_ie_size`, **`+72 num_mem_reqs`**, `+76 max_num_scan_chans`,
   `+80 hw_bd_id`, `+84 hw_bd_info[5]` (по 4 байта).
2. 0x21 `hal_reg_capabilities`, 36 байт (`wmi.h:2227-2242`): `eeprom_rd`,
   `eeprom_rd_ext`, `regcap1`, `regcap2`, `wireless_modes`,
   `low/high_2ghz_chan`, `low/high_5ghz_chan`.
3. 0x10 — **битовая карта служб** (первый `ARRAY_UINT32`).
4. 0x12 — запросы памяти: вложенные TLV 0x22 по 16 байт `{req_id, unit_size,
   num_unit_info, num_units}` (`wmi.h:2251-2269`), не больше 16
   (`wmi.h:6792`, `wmi-tlv.c:1259-1277`).
5. 0x10 — второй `ARRAY_UINT32` (DBS hw mode), игнорируется.

Без любого из 1–4 — `-EPROTO` (`wmi-tlv.c:1340-1341`). **Проверка ABI**
(`wmi-tlv.c:1248-1257`, `1354-1360`): `abi_ver0 = 0x01000000`,
`ns0 = 0x5F414351` ("QCA_"), `ns1 = 0x00004C4D` ("ML"), `ns2 = ns3 = 0`;
`abi_ver1` не сверяется. Иначе `-EOPNOTSUPP`.

**Битовая карта — 4 бита на 32-битное слово.** `WMI_SERVICE_IS_ENABLED`:
слово `id / sizeof(u32)` = `id/4`, бит `id % 4` (`wmi.h:521-524`; комментарий
`wmi.h:526-529` прямо говорит, что прошивка использует 4 бита каждого слова).
Служба 0..127 занимает 32 слова = 128 байт. Сопоставление номеров TLV с
номерами ath10k — `wmi-tlv.h:1469-1620`, номера TLV — `wmi-tlv.h:1259-1388`.
[wmi.c — по памяти: `len` для проверки `id < len` — длина TLV в байтах (128)].

**Запросы памяти.** Обработку (выделение кусков) делает wmi.c [не извлечён].
Нужна ли WCN3990 хост-память, снимок не говорит; единственный известный
по коду — IRAM recovery, `req_id 8` (`wmi.h:3105`), только при фиче
`IRAM_RECOVERY` (её у этой прошивки нет). **Первый прогон должен напечатать
`num_mem_reqs` и все запросы.** Адрес куска в INIT — 32 бита; старшая
половина пишется, только если есть `SUPPORT_EXTEND_ADDRESS`
(`wmi-tlv.c:1834-1842`), а у этой прошивки её нет → **куски (IOVA) обязаны
лежать ниже 4 ГиБ**.

**fw_version.** Строки версии в `firmware-5.bin` нет, а `ethtool` показывает
`1.0.0.571` — её собирает wmi.c из `sw_ver0 = abi_ver0` и `sw_ver1 = abi_ver1`
(`wmi-tlv.c:1367-1368`) [формула `major=ver0>>24, minor=ver0&0xffffff,
release=ver1>>16, build=ver1&0xffff` — по памяти]. Если так, в SERVICE_READY
**`abi_ver1 = 0x0000023B`** (571) — проверяемое число.

### 4.4. INIT (0x1, host→tgt) (`wmi-tlv.c:1856-1974`)

Тело: `[TLV 0x4a, len 28][TLV 0x4b, len 176][TLV 0x12, len n×20]`, где
элементы массива — TLV 0x4c по 16 байт `{req_id, ptr, size, ptr_high}`
(`wmi-tlv.h:1792-1809`). При `n = 0` массив всё равно есть, с длиной 0.
Итого при `n=0`: 216 байт тела + 4 WMI + 8 HTC = **228 байт на проводе**.

`wmi_tlv_init_cmd` (28): `abi{0x01000000, 53, 0x5F414351, 0x4C4D, 0, 0}`
(**`abi_ver1 = 53`**, `wmi-tlv.c:1257`, `1900-1905`), `num_host_mem_chunks`.

`wmi_tlv_resource_config` (44 слова, порядок полей — `wmi-tlv.h:1747-1789`),
значения для WCN3990 при службах из снимка (RX_FULL_REORDER есть,
TX_DATA_ACK_RSSI нет):

| # | поле | значение | откуда |
|---|---|---|---|
| 0 | num_vdevs | 4 | `hw.h:757` |
| 1 | num_peers | 33 | `core.c:732`, `hw.h:768` |
| 2 | num_offload_peers | 4 | RX_FULL_REORDER, `wmi-tlv.c:1917-1923` |
| 3 | num_offload_reorder_bufs | 4 | то же |
| 4 | num_peer_keys | 2 | |
| 5 | num_tids | 66 (33×2) | `wmi-tlv.c:1926-1929` |
| 6 | ast_skid_limit | 16 | `hw.h:769` |
| 7 | tx_chain_mask | 7 | |
| 8 | rx_chain_mask | 7 | |
| 9-12 | rx_timeout_pri[4] | 100, 100, 100, 40 | |
| 13 | rx_decap_mode | 1 (native wifi) | `core.c:2665` |
| 14 | scan_max_pending_reqs | 4 | |
| 15 | bmiss_offload_max_vdev | 4 | |
| 16 | roam_offload_max_vdev | 4 | |
| 17 | roam_offload_max_ap_profiles | 8 | |
| 18-20 | num_mcast_groups, num_mcast_table_elems, mcast2ucast_mode | 0, 0, 0 | |
| 21 | tx_dbg_log_size | 0x400 | |
| 22 | num_wds_entries | 2 | `hw.h:770` |
| 23-26 | dma_burst_size, mac_aggr_delim, rx_skip_defrag…, vow_config | 0 | |
| 27 | gtk_offload_max_vdev | 2 | |
| 28 | num_msdu_desc | 1056 | `hw.h:762` |
| 29 | max_frag_entries | 2 | |
| 30 | num_tdls_vdevs | 1 | `hw.h:760` |
| 31 | num_tdls_conn_table_entries | 0x20 | |
| 32 | beacon_tx_offload_max_vdev | 2 | |
| 33 | num_multicast_filter_entries | 5 | |
| 34 | num_wow_filters | 22 | `hw.h:764` |
| 35 | num_keep_alive_pattern | 6 | |
| 36 | keep_alive_pattern_size | 0 | |
| 37-38 | max_tdls_concurrent_sleep/buffer_sta | 1, 1 | |
| 39 | wmi_send_separate | 0 | |
| 40-42 | num_ocb_vdevs/channels/schedules | 0 | |
| 43 | host_capab | 0x200 (MGMT_BUNDLE_TX_COMPL, `wmi-tlv.h:1661`); +0x40000 при TX_ACK_RSSI (`wmi-tlv.h:20`) — у нас нет | |

Значения из `wmi-tlv.c:1908-1968`, если не указано иное. INIT — одна команда
на единственный кредит WMI.

### 4.5. READY (0x2) (`wmi-tlv.c:1392-1420`)

TLV 0x23, 36 байт (`wmi-tlv.h:1741-1745`, `wmi.h:851-859`):
`abi{6×le32}`, `mac_addr` (6 байт + 2 pad), `le32 status`. Ожидание
SERVICE_READY и READY — по 5 с (`wmi.h:2352-2353`).

MAC: на clover `dmesg` пишет `invalid MAC address; choosing random`
(`mac.c:10001-10004`) — из DT адреса нет (`core.c:3452`), значит и READY не
дал годного [wmi.c — по памяти: адрес из READY копируется, если своего нет].
**Ожидать в READY нулевой/негодный MAC**; адрес на Android приходит из
persist/NV — отдельный вопрос.

### 4.6. После READY в `core_start` (`core.c:3219-3289`)

* `compat_services` — только для 10.x, TLV не трогает (`core.c:2876-2896`);
  в снимке THERM_THROT «-», что согласуется.
* `pdev_set_base_macaddr` — у TLV генератора **нет** → `-EOPNOTSUPP`,
  команда 0x400d **не уходит** (`wmi-ops.h:536-549`, `core.c:3225-3230`;
  в таблице ops `wmi-tlv.c:4532-…` её нет).
* фиктивный vdev create/delete + ECHO — только при
  `hw_filter_reset_required`; у WCN3990 `false` (`core.c:740`, `3245-3253`).
* `EXT_RES_CFG` (до INIT) — не шлётся: службы нет (`core.c:3148`, снимок).
* наполнение кольца приёма, затем HTT setup (раздел 3.3).
* `set_target_log_mode` — это **QMI**, не HTC (`snoc.c:1134-1145`).

Регдомен, `pdev_set_param` и прочее шлёт не `core_start`, а `ath10k_start`
(`mac.c:5232-5375`) после него: PDEV_SET_PARAM (0x4003) с параметрами TLV
(`wmi-tlv.h:406-…`, карта `wmi-tlv.c:4342-…`): PMF_QOS **34**=1,
DYNAMIC_BW **10**=1, IDLE_PS_CONFIG **44**=1, TX/RX_CHAIN_MASK **1/2**
(`__ath10k_set_antenna`), ARP_AC_OVERRIDE **35**=0, ANI_ENABLE **37**=1;
RESMGR_ADAPTIVE_OCS 0x21001 `{enable=1}` (служба ADAPTIVE_OCS есть,
`mac.c:5269-5276`, тег 0xcf); BURST нет → без `burst_enable`; SPOOF_MAC нет →
без `scan_prob_req_oui`; потом `ath10k_regd_update` → PDEV_SET_REGDOMAIN
0x4001 (`mac.c:5375`).

ath10k проходит `core_start` **дважды**: при пробе (`core.c:3474-3482`,
после — `core_stop`: PDEV_SUSPEND 0x11001 с `opt = 1`
SUSPEND_AND_DISABLE_INTR, `core.c:3335-3337`, `wmi.h:4638-4643`, потом QMI
wlan disable) и при подъёме интерфейса.

---

## 5. Последовательность от «CE подняты» до READY (и HTT после)

Обозначения: `E_htt`, `E_wmi` — номера endpoint из ответов прошивки.
**[✓]** — проверяемо по снимку, **[~]** — косвенно, без пометки — по коду.

0. Хост: постит буферы во все CE, у которых есть приёмное кольцо
   (`dest_nentries > 0`: CE1, CE2, CE5, CE7, CE8–CE11; `snoc.c:540-544`),
   включает прерывания (`snoc.c:935-953`). Память HTT уже выделена:
   кольцо, `alloc_idx = 0`, frag desc (`core.c:3065-3087`).
1. **tgt→host, CE2, eid 0: HTC READY (msg 1)** — `credit_size`, расширение по
   длине. Хост: кредитов = 1 (`htc.c:962-965`).
2. **host→tgt, CE0, eid 0: CONNECT_SERVICE (2)**, `service 0x0300`,
   `flags 0x0008` (`core.c:3107-3113`, `htt.c:288-324`).
3. **tgt→host, CE2, eid 0: CONNECT_SERVICE_RESP (3)**, `status 0`, `eid = E_htt`.
4. **host→tgt, CE0, eid 0: CONNECT_SERVICE (2)**, `service 0x0100`,
   `flags 0x0100` (`core.c:3115`) [wmi.c].
5. **tgt→host, CE2, eid 0: CONNECT_SERVICE_RESP (3)**, `eid = E_wmi`.
6. **host→tgt, CE0, eid 0: SETUP_COMPLETE_EX (5)**, 12 байт, нули
   (`core.c:3121`). Подключение pktlog пропускается (§2.1).
7. *(возможно)* **tgt→host, CE2, E_wmi: WMI 0x3 SERVICE_AVAILABLE** (TLV 0x22f).
   [~] все 4 расширенные службы в снимке «-».
8. **tgt→host, CE2, E_wmi: WMI 0x1 SERVICE_READY** (`core.c:3127-3133`).
   **[✓] битовая карта** — таблица ниже; [~] `abi_ver1 = 571` (через
   `ethtool 1.0.0.571`); `num_mem_reqs` — снимок не знает.
9. **host→tgt, CE3, E_wmi: WMI 0x1 INIT** (228 байт при 0 кусков,
   `core.c:3206`), тратит единственный кредит; кредит вернётся записью CREDITS
   в трейлере.
10. **tgt→host, CE2, E_wmi: WMI 0x2 READY** (`core.c:3213`). [~] MAC — ожидаемо
    негодный (`dmesg`).
    — дальше уже после READY —
11. Хост: 2047 буферов в кольцо, `*alloc_idx = 2047` (`core.c:3255`).
12. **host→tgt, CE4, E_htt: HTT VERSION_REQ (0)** (`core.c:3270`).
13. **tgt→host, CE1, E_htt: HTT VERSION_CONF (0x00)** — **[✓] 3.56** (`dmesg`).
14. **host→tgt, CE4, E_htt: FRAG_DESC_BANK_CFG (6)**, 64 байта.
15. **host→tgt, CE4, E_htt: RX_RING_CFG (2)**, 48 байт.
16. **host→tgt, CE4, E_htt: AGGR_CFG (5)** v2 `40 03 00`.
17. QMI: режим журнала прошивки (`core.c:3281`) — вне HTC.

**[✓] Файлы:** `firmware-5.bin` CRC `022c9087`, `board-2.bin` `ecd45d37`,
выбранная board data `380d3b6d`, `athwlan/otp/codeswap = 0` — образа в
контейнере нет (non-BMI) (`fw_checksums.txt`; способ счёта — §1.2).
**[✓] Опвер:** `wmi-op 4 htt-op 3`, фичи — `dmesg`.

`fw_stats.txt` к старту отношения не имеет; он подтверждает только, что
путь REQUEST_STATS (0x16001) → UPDATE_STATS (0x1d004) с маской
`PDEV|VDEV|PEER|PEER_EXTD` (`core.c:2765-2766`, `wmi.h:4605-4609`) работает,
и что кадры идут мимо RX_IND: «MSDUs delivered to HTT 0» при живой сети —
согласуется с in-order путём (0x12).

### 5.1. Карта служб из снимка → биты SERVICE_READY

Слово = `id/4`, бит = `id%4` (§4.3). Включено в снимке (номер TLV):
0 BEACON_OFFLOAD, 2 ROAM_SCAN_OFFLOAD, 3 BCN_MISS_OFFLOAD, 4 STA_PWRSAVE,
5 STA_ADVANCED_PWRSAVE, 6 AP_UAPSD, 8 11AC, 10 PHYERR, 11 BCN_FILTER, 12 RTT,
13 WOW, 16 ARPNS_OFFLOAD, 17 NLO, 18 GTK_OFFLOAD, 19 SCAN_SCH, 20 CSA_OFFLOAD,
23 PACKET_POWER_SAVE, 24 FORCE_FW_HANG, 25 GPIO, 27 STA_UAPSD_BASIC_AUTO_TRIG,
28 STA_UAPSD_VAR_AUTO_TRIG, 29 STA_KEEP_ALIVE, 30 TX_ENCAP,
31 AP_PS_DETECT_OUT_OF_SYNC, 33 STA_SMPS, 35 STA_WMMAC, 36 TDLS,
39 ADAPTIVE_OCS, 41 FILTER_IPSEC_NATKEEPALIVE, 45 QPOWER, 47 THERMAL_MGMT,
48 RMC, 52 NAN, 54 ESTIMATE_LINKSPEED, 55 OBSS_SCAN, 56 TDLS_OFFCHAN,
57 TDLS_UAPSD_BUFFER_STA, 58 TDLS_UAPSD_SLEEP_STA, 60 LPASS, 61 EXTSCAN,
62 D0WOW, 63 HSOFFLOAD, 64 ROAM_HO_OFFLOAD, **65 RX_FULL_REORDER**,
67 STA_RX_IPA_OFFLOAD_SUPPORT, **76 MGMT_TX_WMI**, **92 SYNC_DELETE_CMDS**.

Минимальное содержимое слов карты (по смещению байта в массиве 0x10):

| слово (байт) | маска | слово (байт) | маска |
|---|---|---|---|
| 0 (0) | 0xd | 10 (40) | 0x2 |
| 1 (4) | 0x7 | 11 (44) | 0xa |
| 2 (8) | 0xd | 12 (48) | 0x1 |
| 3 (12) | 0x3 | 13 (52) | 0xd |
| 4 (16) | 0xf | 14 (56) | 0x7 |
| 5 (20) | 0x9 | 15 (60) | 0xf |
| 6 (24) | 0xb | 16 (64) | 0xb |
| 7 (28) | 0xf | 19 (76) | 0x1 |
| 8 (32) | 0xa | 23 (92) | 0x1 |
| 9 (36) | 0x9 | | |

Сопоставленные, но выключенные (эти биты обязаны быть нулями): 1, 7, 9, 14,
15, 21, 22, 26, 32, 34, 37, 38, 40, 42, 43, 44, 46, 49, 50, 51, 53, 59, 66,
68, 69, 103 (MESH_11S), 116 (PEER_STATS_INFO). Службы TLV, которых ath10k
не сопоставляет (например 70–75, 77–91), снимок проверить не может.

Как это использовать: напечатать сырые 128 байт карты на первом прогоне и
сверить с таблицей — совпадение подтверждает сразу и раскладку TLV, и схему
«4 бита на слово».
