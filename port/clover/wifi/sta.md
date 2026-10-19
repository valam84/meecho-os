# WCN3990 (SDM660): обмен WMI/HTT для клиента (managed STA)

Справка для драйвера MEECHO. Источник — Linux ath10k как **справочник
протокола**: отсюда берутся только числа и факты, текст кода не
переносится. Пути ниже — относительно `~/wifi-ref/ath10k/`; всякое число
сопровождается `файл:строка`.

Прошивка: WMI op version TLV (4), HTT op version TLV (3, `hw.h:207`),
fw features: `wowlan`, `no-4addr-pad`, `allows-mesh-bcast`,
`mgmt-tx-by-reference`, `non-bmi`, `single-chan-info-per-channel`
(имена — `core.c:752-774`). Чего у этой прошивки **нет** и что от этого
зависит: `has-wmi-mgmt-tx` (выбор пути передачи mgmt, §3.3), `mfp`
(флаг PMF в peer assoc, `mac.c:2258-2262`), `raw-mode`, `peer-flow-ctrl`
(значит, режим «push»: без TX_FETCH_IND).

Железо по таблице ath10k (`core.c:713-748`): шина SNOC, `target_64bit =
true`, `continuous_frag_desc = true`, `rx_desc_ops = wcn3990_rx_desc_ops`,
`decap_align_bytes = 1`, `rx_ring_fill_level =
HTT_RX_RING_FILL_LEVEL_DUAL_MAC`, `sw_decrypt_mcast_mgmt = true`.

**Чего нет в наборе `wifi-ref`** (и потому не процитировано): `wmi.c`
(обработчики событий `scan`/`mgmt_rx`/`mgmt_tx_compl`, умолчания
`ath10k_wmi_start_scan_init()`, `ath10k_wmi_put_wmi_channel()`,
`ath10k_wmi_put_start_scan_common()`, таблица шифров `wmi_key_cipher`),
`htc.c` есть, `drivers/net/wireless/ath/regd*.c` — нет. Места, где ниже
стоит «по памяти», надо сверить с `wmi.c` той же версии ядра прежде, чем
закладывать в код.

---

## 0. Кадрирование

- **WMI-команда** = HTC-заголовок (8 байт, `htc.h:74-...`) + `wmi_cmd_hdr`
  (одно `__le32`, номер команды в младших 24 битах: `wmi.h:55-62`) +
  последовательность TLV. События приходят с тем же заголовком, номер
  берётся маской `WMI_CMD_HDR_CMD_ID` (`wmi-tlv.c:645-646`).
- **TLV-заголовок** — `struct wmi_tlv { __le16 len; __le16 tag; }`,
  **длина первой** (`wmi-tlv.h:1640-1644`); `len` — длина значения без
  заголовка.
- Номер команды/события = `(группа << 12) | 1` для первого в группе,
  дальше подряд (`wmi-tlv.h:13-14`). Группы: SCAN=3, PDEV=4, VDEV=5,
  PEER=6, MGMT=7, BA_NEG=8, STA_PS=9, …, ROAM=0xb, …, TID_ADDBA=0x1c
  (`wmi-tlv.h:22-70`).
- **Номера тегов TLV** считаются по перечислению `wmi_tlv_tag`
  (`wmi-tlv.h:585-…`): массивы `ARRAY_UINT32=16`, `ARRAY_BYTE=17`,
  `ARRAY_STRUCT=18`, `ARRAY_FIXED_STRUCT=19` (`wmi-tlv.h:586-592`);
  для структурных тегов значение = (номер строки в `wmi-tlv.h`) − 563,
  проверено подсчётом по всему перечислению (первый — `SERVICE_READY_EVENT
  = 32`, строка 595). Конкретные значения ниже даны с этой строкой.
- **HTT** идёт своим HTC-эндпоинтом (`ATH10K_HTC_SVC_ID_HTT_DATA_MSG`,
  `htt.c:304`); первый байт — тип сообщения (`htt.h:52-54`, `536-538`).
  TX_FRM данных HTC-учёт не проходит, заголовок HTC заполняется руками
  (`htt_tx.c:1695-1716`).

---

## 1. Создание интерфейса

### 1.1. Что уже должно быть сделано (`ath10k_start`, `mac.c:5182-5383`)

После `WMI_INIT` / `READY` ath10k шлёт pdev-параметры
(`WMI_TLV_PDEV_SET_PARAM_CMDID = 0x4003`, `wmi-tlv.h:82`; тег
`PDEV_SET_PARAM_CMD = 82`, `wmi-tlv.h:645`; тело `{pdev_id, param_id,
param_value}`, `wmi-tlv.h:1822-1826`, сборка `wmi-tlv.c:1794-1815`):

| Параметр | TLV id | Значение | Где |
|---|---|---|---|
| `PMF_QOS` | 0x22 | 1 | `mac.c:5247-5248`, id `wmi-tlv.h:440` |
| `DYNAMIC_BW` | 0xa | 1 | `mac.c:5254-5255`, `wmi-tlv.h:416` |
| `BURST_ENABLE` (если сервис `BURST`) | 0x30 | 0 | `mac.c:5278-5280`, `wmi-tlv.h:454` |
| `IDLE_PS_CONFIG` | 0x2c | 1 | `mac.c:5287-5288`, `wmi-tlv.h:450` |
| `ARP_AC_OVERRIDE` | 0x23 | 0 | `mac.c:5305-5306`, `wmi-tlv.h:441` |
| `ANI_ENABLE` | 0x25 | 1 | `mac.c:5325-5326`, `wmi-tlv.h:443` |

(id pdev-параметров посчитаны по перечислению `wmi_tlv_pdev_param`,
начало `TX_CHAIN_MASK = 0x1` в `wmi-tlv.h:407`.) Также, если сервис
`SPOOF_MAC_SUPPORT`, — `SCAN_PROB_REQ_OUI` (`mac.c:5261-5266`), а в конце —
регуляторика (§2.4), `mac.c:5374-5375`.

### 1.2. Адрес

- `vdev_create` получает `vif->addr` (`mac.c:5724-5725`); у mac80211 это
  адрес устройства `ar->mac_addr`.
- `ar->mac_addr` берётся из device tree (`device_get_mac_address`,
  `core.c:3452`), иначе — из события `READY` (`mac_addr` в
  `wmi_tlv_rdy_ev`, `wmi-tlv.h:1741-1745`, разбор `wmi-tlv.c:1392-…`,
  поле — `+25` строк от начала функции).
- `PDEV_SET_BASE_MACADDR` у TLV-операций **не реализован** (в
  `wmi_tlv_ops`, `wmi-tlv.c:4532-4620`, нет `gen_pdev_set_base_macaddr`),
  ath10k терпит `-EOPNOTSUPP` (`core.c:3225-3230`).

### 1.3. `ath10k_add_interface` для STA (`mac.c:5558-5891`)

По порядку:

1. **`VDEV_CREATE`** — `WMI_TLV_VDEV_CREATE_CMDID = 0x5001`
   (`wmi-tlv.h:97`), тег `VDEV_CREATE_CMD = 86` (`wmi-tlv.h:649`), тело
   `{vdev_id, vdev_type, vdev_subtype, vdev_macaddr(8 байт)}`
   (`wmi.h:4871-4876`; сборка `wmi-tlv.c:2137-2162`).
   - `vdev_id` — младший свободный бит карты (`mac.c:5605-5610`), то есть 0.
   - `vdev_type = WMI_VDEV_TYPE_STA = 2` (`mac.c:5621-5622`, `wmi.h:4880`).
   - `subtype = WMI_TLV_VDEV_SUBTYPE_NONE = 0` (`mac.c:5611-5612`,
     отображение `wmi-tlv.c:2114-2134`, значения `wmi-tlv.h:1862-1870`).
   - `wmi_mac_addr` — 6 байт адреса, дополненные до 8 (`wmi.h:851-859`).
   - Ответного события у создания нет (ath10k ничего не ждёт).
2. **Self-peer для STA не создаётся**: peer на собственный адрес — только
   для AP/IBSS (`mac.c:5779-5806`); у STA `peer_id = HTT_INVALID_PEERID`.
3. `VDEV_SET_PARAM` (`0x5008`, `wmi-tlv.h:104`; тег 95, `wmi-tlv.h:658`;
   тело `{vdev_id, param_id, param_value}`, `wmi-tlv.c:2329-2351`):
   - `TX_ENCAP_TYPE` (TLV id **0x30**, `wmi-tlv.h:520`, перечисление с
     `RTS_THRESHOLD = 0x1` в строке 473) = `ATH10K_HW_TXRX_NATIVE_WIFI = 1`
     — всегда, независимо от режима (`mac.c:5541-5543`, `hw.h:423`);
   - `NSS` (0x22, `wmi-tlv.h:506`) — если задан `cfg_tx_chainmask`
     (`mac.c:5765-5777`);
   - `RTS_THRESHOLD` (0x1) — из wiphy (`mac.c:5850`, `833-…`);
   - `DISABLE_4ADDR_SRC_LRN` — у TLV не отображается (нет в
     `wmi_tlv_vdev_param_map`, `wmi-tlv.c:4458-4530`), пропустить.
4. **`STA_KEEPALIVE`** (если сервис `STA_KEEP_ALIVE`) с `enabled = 1`,
   `method = NULL_FRAME (1)`, `interval = 0` — то есть фактически
   выключить: «держать связь» — дело хоста (`mac.c:2017-2047`,
   `wmi.h:6645`, `6649`). Команда `0x1c005` (`wmi-tlv.h:225`), тег 127.
5. **`STA_POWERSAVE_PARAM`** (`0x9002`, `wmi-tlv.h:139`; тег 109; тело
   `{vdev_id, param_id, param_value}`, `wmi.h:6010-6014`):
   `RX_WAKE_POLICY(0) = WAKE(0)`, `TX_WAKE_THRESHOLD(1) = ALWAYS(1)`,
   `PSPOLL_COUNT(2) = NO_MAX(0)` (`mac.c:5817-5841`, `1884-1933`;
   значения `wmi.h:5869`, `5891`, `5909`, `5976-6000`).
6. TXBF: у TLV схема `AFTER_ASSOC` (`wmi-tlv.c:1788-1791`), поэтому на
   создании ничего не шлётся (`mac.c:5493-5494`).
7. Мощность — `ath10k_mac_txpower_recalc` (pdev-параметры
   `txpower_limit2g/5g`, `mac.c:5857-5862`); для первого запуска можно
   оставить умолчание прошивки.

---

## 2. Сканирование

### 2.1. Команда `START_SCAN`

`WMI_TLV_START_SCAN_CMDID = 0x3001` (`wmi-tlv.h:74`). Сборка —
`wmi-tlv.c:1977-2074`. Порядок TLV строго такой:

| # | Тег | Содержимое |
|---|---|---|
| 1 | `START_SCAN_CMD` = 77 (`wmi-tlv.h:640`) | `wmi_tlv_start_scan_cmd`, 100 байт (`wmi-tlv.h:1850-1860`) |
| 2 | `ARRAY_UINT32` = 16 | частоты каналов в МГц, по `__le32` (`wmi-tlv.c:2032-2037`) |
| 3 | `ARRAY_FIXED_STRUCT` = 19 | `wmi_ssid` × n: `{__le32 ssid_len; u8 ssid[32]}` = 36 байт (`wmi.h:3162-3165`, `wmi-tlv.c:2042-2049`) |
| 4 | `ARRAY_FIXED_STRUCT` = 19 | `wmi_mac_addr` × n (8 байт) (`wmi-tlv.c:2054-2059`) |
| 5 | `ARRAY_BYTE` = 17 | IE для probe request, длина округлена до 4 (`wmi-tlv.c:1997`, `2064-2067`) |

TLV пустых массивов всё равно присутствуют (длина 0).

Поля фиксированной части по порядку (`wmi.h:3199-3256` +
`wmi-tlv.h:1850-1860`): `scan_id, scan_req_id, vdev_id, scan_priority,
notify_scan_events, dwell_time_active, dwell_time_passive, min_rest_time,
max_rest_time, repeat_probe_time, probe_spacing_time, idle_time,
max_scan_time, probe_delay, scan_ctrl_flags` (15 × u32), затем
`burst_duration_ms, num_channels, num_bssids, num_ssids, ie_len,
num_probes`, затем `mac_addr`, `mac_mask` (по 8 байт). **Обратите
внимание:** в структуре `num_bssids` стоит **раньше** `num_ssids`
(`wmi-tlv.h:1854-1855`), а TLV-массивы идут в порядке SSID, потом BSSID.

Что выставляет ath10k:

- `num_probes = 3` всегда (`wmi-tlv.c:2020`).
- **Флаг `WMI_SCAN_FILTER_PROBE_REQ` (0x20) инвертируется** для TLV
  (`wmi-tlv.c:2024-2027`, значение `wmi.h:3324`): если хост хочет
  фильтрацию — бит должен быть **снят**.
- `scan_id = ATH10K_SCAN_ID = 0` (`mac.c:6444`, `core.h:37`),
  `vdev_id` — STA-vdev (`mac.c:6443`). Префиксы `0xA000` для `scan_id` и
  `scan_req_id` (`wmi.h:3174`, `3178`) добавляет общий код (для stop —
  явно, `wmi-tlv.c:2095-2099`; для start — `put_start_scan_common` в
  `wmi.c`, **по памяти**).
- Пассивное сканирование: если SSID не заданы — `WMI_SCAN_FLAG_PASSIVE =
  0x1` (`mac.c:6451-6459`, `wmi.h:3314`). Активное: SSID в TLV 3 (пустая
  строка длины 0 = wildcard у mac80211).
- Случайный адрес — `ADD_SPOOFED_MAC_IN_PROBE_REQ = 0x1000` +
  `mac_addr/mac_mask` (`mac.c:6461-6465`, `wmi.h:3336`).
- Каналы — частоты из запроса (`mac.c:6467-6471`), до 64 (`wmi.h:3304`).
- IE — то, что дал mac80211 (`mac.c:6446-6449`), до 256 байт
  (`wmi.h:3182`): там Supported Rates/Ext Rates/HT Caps/… для probe
  request.
- `req->duration` переопределяет dwell (`mac.c:6473-6482`).
- **Умолчания `ath10k_wmi_start_scan_init()` — в `wmi.c`, в набора нет.
  По памяти (сверить!):** `scan_priority = LOW (1)`; `notify_scan_events
  = STARTED|COMPLETED|BSS_CHANNEL|FOREIGN_CHANNEL|DEQUEUED`;
  `dwell_time_active = 50`, `dwell_time_passive = 150`, `min_rest_time =
  50`, `max_rest_time = 500`, `repeat_probe_time = 0`,
  `probe_spacing_time = 0`, `idle_time = 0`, `max_scan_time = 20000`,
  `probe_delay = 5`; флаги `ADD_BCAST_PROBE_REQ | ADD_CCK_RATES |
  ADD_OFDM_RATES | CHAN_STAT_EVENT` (+ `FILTER_PROBE_REQ` до инверсии);
  один BSSID `ff:ff:ff:ff:ff:ff`. Нижняя граница dwell, которую
  отдельные прошивки принимают, — `WMI_SCAN_CHAN_MIN_TIME_MSEC = 40`
  (`wmi.h:3187`).
- Хост ждёт событие STARTED **1 с** (`mac.c:4661`), общий срок —
  `max_scan_time + 200 мс` (`mac.c:6484-6488`).

`STOP_SCAN` = `0x3002`, тег 78, тело `{scan_req_id, scan_id, req_type,
vdev_id}` (`wmi.h:3347-3352`, сборка `wmi-tlv.c:2077-2112`; `req_type`
`STOP_ONE = 0`, `VDEV_ALL = 0x01000000`, `ALL = 0x04000000`,
`wmi.h:3341-3345`).

### 2.2. События сканирования

`WMI_TLV_SCAN_EVENTID = 0x3001` (`wmi-tlv.h:300`), TLV `SCAN_EVENT = 36`
(`wmi-tlv.h:599`), тело `{event_type, reason, channel_freq, scan_req_id,
scan_id, vdev_id}` (`wmi.h:3407-3414`, разбор `wmi-tlv.c:811-841`).

| `event_type` | Бит | Смысл |
|---|---|---|
| STARTED | 0x1 | скан пошёл (`wmi.h:3385`) |
| COMPLETED | 0x2 | конец; `reason`: 0 completed, 1 cancelled, 2 preempted, 3 timedout, 4 internal failure (`wmi.h:3386`, `3398-3405`) |
| BSS_CHANNEL | 0x4 | вернулись на домашний канал (`wmi.h:3387`) |
| FOREIGN_CHANNEL | 0x8 | ушли на канал `channel_freq` (`wmi.h:3388`, `3410`) |
| DEQUEUED / PREEMPTED / START_FAILED / RESTARTED / FOREIGN_CHANNEL_EXIT | 0x10/0x20/0x40/0x80/0x100 | `wmi.h:3389-3394` |

ath10k (в `wmi.c`, **по памяти**) по FOREIGN_CHANNEL запоминает текущую
частоту и метит ею пришедшие beacon/probe response, если канал в
`MGMT_RX` не годится. Побочно с `CHAN_STAT_EVENT` идёт `CHAN_INFO`
(`0x4002`, `wmi-tlv.h:302`; тег 38; `wmi-tlv.h:1663-1677`; с фичей
`single-chan-info-per-channel` поле `mac_clk_mhz` действительно,
`wmi-tlv.c:1028-1030`) — для «survey», для списка сетей не нужен.

### 2.3. Как приходят beacon / probe response: `MGMT_RX`

`WMI_TLV_MGMT_RX_EVENTID = 0x7001` (`wmi-tlv.h:322`). Два TLV
(`wmi-tlv.c:949-999`):

1. `MGMT_RX_HDR` = 44 (`wmi-tlv.h:607`): `struct wmi_tlv_mgmt_rx_ev`
   (`wmi-tlv.h:1689-1697`), 40 байт:
   `channel, snr, rate, phy_mode, buf_len, status, rssi[4]`.
2. `ARRAY_BYTE` = 17: сам кадр 802.11 (с заголовком, без FCS), длиной
   `buf_len`; проверка `skb->len >= смещение + buf_len`
   (`wmi-tlv.c:986-989`).

`status` — `WMI_RX_STATUS_OK 0, ERR_CRC 0x1, ERR_DECRYPT 0x8, ERR_MIC
0x10, ERR_KEY_CACHE_MISS 0x20, EXT_INFO 0x40` (`wmi.h:3476-3482`).
Шум по умолчанию `-95` дБм (`core.h:46`); **по памяти** ath10k считает
`rssi = snr + (-95)`, а `channel` толкует как номер канала IEEE с
выбором диапазона по номеру (1–14 → 2.4 ГГц, иначе 5), при нуле — частоту
текущего скан-канала. Это в `wmi.c`, сверить.

Тем же событием приходят **все** management-кадры, адресованные нам,
включая Authentication и (Re)Association Response (§3).

### 2.4. Список каналов и регуляторика

- **Да, `SCAN_CHAN_LIST` шлётся до первого скана**: ath10k делает это в
  `ath10k_start` → `ath10k_regd_update` → `ath10k_update_channel_list`
  (`mac.c:5375`, `3521`, `3391-3494`) и повторно на каждое изменение
  региона (`mac.c:3563-3590`).
- `WMI_TLV_SCAN_CHAN_LIST_CMDID = 0x3003` (`wmi-tlv.h:76`). Формат
  (`wmi-tlv.c:2838-2892`): TLV `SCAN_CHAN_LIST_CMD` = 79
  (`wmi-tlv.h:642`) с `{num_scan_chans}` (`wmi-tlv.h:1837-1839`), затем
  `ARRAY_STRUCT` = 18, **внутри которого** по вложенному TLV `CHANNEL` = 80
  (`wmi-tlv.h:643`) на канал.
- `struct wmi_channel` (`wmi.h:2047-2077`), 24 байта: `mhz,
  band_center_freq1, band_center_freq2, flags` (младшие 6 бит — режим
  `wmi_phy_mode`), `reginfo0 = {min_power, max_power, reg_power,
  reg_classid}` (единица мощности 0.5 дБм), `reginfo1 = {antenna_max (1
  дБм), max_tx_power (0.5 дБм)}`. Флаги: `HT40_PLUS 1<<6, PASSIVE 1<<7,
  ADHOC_ALLOWED 1<<8, AP_DISABLED 1<<9, DFS 1<<10, ALLOW_HT 1<<11,
  ALLOW_VHT 1<<12` (`wmi.h:2103-2109`). Перевод аргумента в эти поля —
  `ath10k_wmi_put_wmi_channel()` в `wmi.c` (нет в наборе).
- Что кладёт ath10k на канал (`mac.c:3430-3486`): все не-disabled каналы
  обоих диапазонов; `allow_ht = allow_vht = 1`; `allow_ibss = !NO_IR`;
  `ht40plus = !NO_HT40PLUS`; `passive = NO_IR || RADAR`; `freq =
  band_center_freq1 = center_freq`; `min_power 0`, `max_power =
  max_power×2`, `max_reg_power×2`, `max_antenna_gain`; `reg_class_id 0`;
  **режим всегда легаси**: `MODE_11G (1)` на 2.4, `MODE_11A (0)` на 5
  (`wmi.h:1973-1993`).
- **`PDEV_SET_REGDOMAIN`** (`0x4001`, `wmi-tlv.h:80`; тег 81; тело
  `{pdev_id, regd, regd_2ghz, regd_5ghz, conform_limit_2ghz,
  conform_limit_5ghz}`, `wmi-tlv.h:1828-1835`, сборка
  `wmi-tlv.c:1761-1786`; DFS-регион в TLV-версии **не передаётся**).
  ath10k кладёт один и тот же `reg_domain` во все три поля и CTL из
  `regpair` (`mac.c:3534-3543`), DFS — `UNINIT`, если нет
  `CONFIG_ATH10K_DFS_CERTIFIED` (`mac.c:3527-3532`).
- Откуда домен: `current_rd = hw_eeprom_rd` (`mac.c:9957-9970`; WRDD —
  ACPI, на SoC неприменим), а `eeprom_rd` — из `HAL_REG_CAPABILITIES` в
  `SERVICE_READY` (`wmi-tlv.c:1372`). Перевод кода в `regpair`
  (`reg_2ghz_ctl/reg_5ghz_ctl`) и в набор разрешённых каналов делает
  `ath/regd.c` — **нет в наборе**. Для WCN3990 ничего особого в `mac.c`
  нет: тот же путь, что у QCA6174.
- Для MEECHO: на первом этапе — свой «мировой» набор (1–11 активные,
  12–13 пассивные, 5 ГГц UNII-1 активные, DFS пассивные), домен из
  `SERVICE_READY` отдать обратно как есть, CTL взять из таблицы
  `regd_common.h` (снять отдельно).

---

## 3. Подключение (open auth + assoc, затем WPA2-PSK)

mac80211 у ath10k работает через **channel context**, поэтому `vdev_start`
для STA происходит **до** первого кадра Authentication — в
`assign_vif_chanctx`, когда mac80211 готовит подключение. Порядок
(ath10k-сторона; что делает mac80211 между шагами — по его коду, не в
наборе):

### 3.1. Старт vdev на канале AP

`ath10k_mac_op_assign_vif_chanctx` (`mac.c:8986-9053`) →
`ath10k_vdev_start` → `ath10k_vdev_start_restart(restart = false)`
(`mac.c:1509-1577`):

- **`VDEV_START_REQUEST`** = `0x5003` (`wmi-tlv.h:99`). TLV
  (`wmi-tlv.c:2186-2259`):
  1. `VDEV_START_REQUEST_CMD` = 88 (`wmi-tlv.h:651`):
     `wmi_tlv_vdev_start_cmd` (`wmi-tlv.h:1872-1883`, 72 байта) —
     `vdev_id, requestor_id, bcn_intval, dtim_period, flags, ssid(36),
     bcn_tx_rate, bcn_tx_power, num_noa_descr, disable_hw_ack`. Для STA:
     SSID **пустой** (ssid заполняется только для AP/IBSS,
     `mac.c:1536-1547`), `bcn_intval`/`dtim_period` — что знает vif к этому
     моменту (`mac.c:1523-1524`), `flags`: `HIDDEN_SSID`/`PMF_ENABLED` не
     ставятся.
  2. `CHANNEL` = 80: `wmi_channel` с `freq` = первичный канал,
     `band_center_freq1/2` из chandef, `mode = chan_to_phymode(chandef)`
     (HT20/40, VHT… — `mac.c:576-…`), `min_power 0`, `max_power×2`,
     `max_reg_power×2`, `antenna gain` (`mac.c:1526-1534`).
  3. `ARRAY_STRUCT` = 18 длины 0 (NoA, `wmi-tlv.c:2246-2255`).
- **Ждать `VDEV_START_RESP`**: `0x5001` (`wmi-tlv.h:309`), TLV 40
  (`wmi-tlv.h:603`), тело `{vdev_id, req_id, resp_type, status}`
  (`wmi.h:5609-5614`, разбор `wmi-tlv.c:1037-1065`); срок ожидания у
  ath10k — 5 с (`core.h:565`).
- Сразу после — `STA_POWERSAVE_MODE` (`0x9001`, `wmi-tlv.h:138`; тег 108;
  `{vdev_id, sta_ps_mode}`, `wmi-tlv.c:2762-2782`): по умолчанию
  `DISABLED = 0` (vdev запущен, `ps` выключен), до старта — `ENABLED = 1`
  (`mac.c:9016`, `1975-2015`; `wmi.h:5825-5827`).

### 3.2. Peer для AP

mac80211 вставляет станцию AP (`NOTEXIST → NONE`) до отправки Auth;
ath10k: `ath10k_sta_state` → `ath10k_peer_create` (`mac.c:7529-7599`,
`737-786`):

- **`PEER_CREATE`** = `0x6001` (`wmi-tlv.h:114`), тег 97
  (`wmi-tlv.h:660`), тело `wmi_tlv_peer_create_cmd {vdev_id,
  peer_addr(8), peer_type}` (`wmi-tlv.h:1893-1897`, сборка
  `wmi-tlv.c:2571-2593`). `peer_type = DEFAULT (0)` — даже для BSS-peer
  ath10k шлёт 0 (`mac.c:7534`, `wmi-tlv.h:1885-1891`).
- **Ответ — не WMI, а HTT `PEER_MAP`** (T2H `0x3`, `htt.h:438`): `{u8
  vdev_id; __le16 peer_id; u8 addr[6]; 2 резервных}` (`htt.h:800-806`,
  разбор `htt_rx.c:4137-4143`); ath10k ждёт его
  (`ath10k_wait_for_peer_created`, `mac.c:760`). `peer_id` нужен для
  разбора RX (`rx_in_ord_ind.peer_id`).
- Ограничение: `num_peers + число vdev < max_num_peers` — каждый vdev
  держит запись peer (`mac.c:749-751`).

### 3.3. Передача Authentication / Association Request

Путь выбирается `ath10k_mac_tx_h_get_txpath` (`mac.c:3996-4019`): для
mgmt-кадров (`ATH10K_HW_TXRX_MGMT = 3`, `mac.c:3755-3756`, `hw.h:426`):

- если есть фича `has-wmi-mgmt-tx` **или** сервис `WMI_SERVICE_MGMT_TX_WMI`
  (TLV-бит, отображение `wmi-tlv.h:1612-1613`) — **WMI**;
- иначе при HTT ≥ 3 — обычный HTT `TX_FRM` с типом пакета MGMT (§4.1).

У этой прошивки фичи `has-wmi-mgmt-tx` нет — путь решает сервисный бит
из `SERVICE_READY`/`SERVICE_AVAILABLE`. Нужно поддержать оба; ожидаемо
для WCN3990 — WMI по ссылке.

**WMI, «по ссылке»** (`mgmt-tx-by-reference`; `mac.c:4242-4282`):
кадр кладётся в DMA-буфер хоста, **физический адрес** которого идёт в
команде; прошивка забирает кадр сама.

- `WMI_TLV_MGMT_TX_SEND_CMD` = **`0x7008`** (`wmi-tlv.h:131`; в карте
  команд `wmi-tlv.c:4225`). Старый `MGMT_TX_CMDID 0x7006` у TLV не
  реализован (`wmi-tlv.c:4587`).
- TLV 1: `MGMT_TX_CMD` = **422** (`wmi-tlv.h:985`):
  `wmi_tlv_mgmt_tx_cmd {vdev_id, desc_id, chanfreq, __le64 paddr,
  frame_len, buf_len}`, 28 байт, packed (`wmi-tlv.h:2675-2682`).
  - `chanfreq = 0` (домашний канал vdev), `paddr` — адрес всего кадра,
    `frame_len` — полная длина, `buf_len` — длина копии в TLV 2
    (`wmi-tlv.c:3143-3148`).
  - `desc_id` — свободный номер `< mgmt_max_num_pending_tx`
    (`wmi-tlv.c:3062-3084`), по нему приходит завершение.
- TLV 2: `ARRAY_BYTE` = 17 с **первыми `min(len, 64)` байтами кадра**,
  длина округлена до 4 (`WMI_TLV_MGMT_TX_FRAME_MAX_LEN = 64`,
  `wmi-tlv.h:18`; `wmi-tlv.c:3123-3127`, `3153-3158`). Хвост
  дополнять нулями (ath10k копирует `buf_len`, округлённый вверх, —
  читает за конец кадра, повторять не надо).
- Для защищённых action/deauth/disassoc (PMF) к кадру добавляется место
  под MIC CCMP (8) (`wmi-tlv.c:3115-3121`) — для Auth/Assoc не нужно.
- Разрешены только mgmt и (QoS) NullFunc (`wmi-tlv.c:3108-3111`).
- Буфер держать отображённым до события **`MGMT_TX_COMPLETION`** =
  `0x7006` (`wmi-tlv.h:327`), TLV 423 (`wmi-tlv.h:986`): `{desc_id,
  status, pdev_id, ppdu_id, ack_rssi}` (`wmi-tlv.h:1679-1685`, разбор
  `wmi-tlv.c:844-874`; `ack_rssi` — только при сервисе
  `TX_DATA_ACK_RSSI`). Бывает пакетное `0x7007` (`wmi-tlv.h:328`):
  TLV `MGMT_TX_COMPL_BUNDLE_EVENT` (строка 1115 → 552) с `num_reports`,
  затем массивы `ARRAY_UINT32`: `desc_ids`, `status`, `ppdu_ids`,
  `ack_rssi` — в этом порядке (`wmi-tlv.c:876-947`). Значения `status`
  (ок / сброшен / без ACK) — в `wmi.c`, сверить.
- Очередь хоста — до 128 кадров (`core.h:50`, `mac.c:3985-3988`).
- Номер последовательности в заголовке mgmt-кадра проставляет
  mac80211 — у нас это делает драйвер.

**HTT-путь для mgmt** (если сервисного бита нет): `TX_FRM` с
`pkt_type = MGMT (3)`, `MAC_HDR_PRESENT`, `ext_tid = 17`
(`HTT_DATA_TX_EXT_TID_MGMT`), а **`frags_paddr` = адрес самого кадра**,
без фрагментного дескриптора (`htt_tx.c:1686-1692`, `1172-1173`,
`htt.h:169`); завершение — обычное `TX_COMPL_IND` (§4.3).

### 3.4. Ответы AP

Authentication (seq 2) и (Re)Association Response приходят событием
`MGMT_RX` (§2.3). Разбор кадров (статус, AID, наборы скоростей, HT/VHT
capabilities, WMM parameter element) — работа MLME хоста.

### 3.5. После ассоциации: `PEER_ASSOC`, `VDEV_UP`

mac80211 переводит AP-станцию в `ASSOC`, но ath10k на этом переходе для
**STA-vif ничего не делает** (обработка только для AP/mesh/IBSS,
`mac.c:7697-7711`). Всё делает `bss_info_changed(BSS_CHANGED_ASSOC)` →
`ath10k_bss_assoc` (`mac.c:6269-6282`, `3088-3188`):

1. **`PEER_ASSOC`** = `0x6005` (`wmi-tlv.h:118`). TLV
   (`wmi-tlv.c:2672-2759`):
   1. `PEER_ASSOC_COMPLETE_CMD` = 101 (`wmi-tlv.h:664`):
      `wmi_tlv_peer_assoc_cmd` (`wmi-tlv.h:1899-1917`, 76 байт) —
      `mac_addr(8), vdev_id, new_assoc, assoc_id, flags, caps,
      listen_intval, ht_caps, max_mpdu, mpdu_density, rate_caps, nss,
      vht_caps, phy_mode, ht_info[2], num_legacy_rates, num_ht_rates`.
   2. `ARRAY_BYTE`: легаси-скорости, длина округлена до 4.
   3. `ARRAY_BYTE`: HT MCS-индексы, округлено до 4.
   4. `VHT_RATE_SET` = 102 (`wmi-tlv.h:665`): `{rx_max_rate, rx_mcs_set,
      tx_max_rate, tx_mcs_set}` (`wmi.h:6369-6374`).

   Как заполняет ath10k (`ath10k_peer_assoc_prepare`, `mac.c:2823-2841`):
   - `mac_addr` = BSSID; `new_assoc = 1` (`wmi-tlv.c:2707`);
     `assoc_id` = **AID из Assoc Response** (`mac.c:2197-2198`);
     `listen_intval = 1` для STA — обход нестабильного PS прошивки
     (`mac.c:2170-2184`); `nss = 1` по умолчанию; `caps` =
     capability info из ответа (`mac.c:2187-2209`).
   - Флаги — **TLV-значения** (`wmi-tlv.c:4622-4639`, `wmi-tlv.h:567-582`):
     `AUTH 0x1` всегда (`mac.c:2205`); `NEED_PTK_4_WAY 0x4`, если у BSS
     есть RSN или WPA IE; `NEED_GTK_2_WAY 0x10` — только при WPA IE
     (`mac.c:2247-2256`); `QOS 0x2`, если AP с WMM (`mac.c:2702-2705`);
     `HT 0x1000`, `40MHZ 0x2000`, `STBC 0x8000`, `LDPC 0x10000` — по HT
     caps (`mac.c:2358-2395`); `VHT 0x2000000`, `80MHZ 0x4000000`;
     `PMF` у TLV = **`0x08000000`** (не `0x10000000`, как в `wmi.h:6436`),
     но без фичи `mfp` не ставится.
   - Легаси-скорости: для каждой скорости из пересечения наборов —
     байт `ceil(bitrate_100kbps / 5)` (то есть в единицах 500 кбит/с) с
     **битом 7 для CCK** (`mac.c:125-129`, `2265-2301`). Бит 7 здесь — не
     «basic rate» из IE, а признак CCK.
   - HT: `max_mpdu = (1 << (13 + ampdu_factor)) - 1`, `mpdu_density`
     (≤ 16, `wmi-tlv.c:2682-2683`), `ht_caps` = HT Capabilities Info,
     `rate_caps |= WMI_RC_HT_FLAG (0x8)`, `CW40 0x2`, `SGI 0x4`,
     `TX_STBC 0x20`, RX STBC в битах 6–7, `DS 0x1`/`TS 0x200` по числу
     потоков (`mac.c:2358-2400`, `wmi.h:6485-6495`); MCS — индексы
     поддерживаемых AP; если пусто — MCS 0..7 (прошивка иначе падает,
     `mac.c:2409-2426`).
   - `phy_mode`: 2.4 ГГц — `11NG_HT40 (7)`/`11NG_HT20 (5)`/`11G (1)`/`11B
     (2)`; 5 ГГц — VHT…/`11NA_HT40 (6)`/`11NA_HT20 (4)`/`11A (0)`
     (`mac.c:2754-2821`, `wmi.h:1973-1993`).
2. Peer SMPS (`PEER_SET_PARAM smps_state`, TLV id 0x1,
   `wmi-tlv.h:549`) по HT caps (`mac.c:3138`, `2843-…`); TXBF (`mac.c:3145`).
3. `PDEV_SET_PARAM PEER_STATS_INFO_ENABLE (0x8b) = 1` (`mac.c:3161-3164`,
   `wmi-tlv.h:468`) — статистика, можно пропустить.
4. **`VDEV_UP`** = `0x5005` (`wmi-tlv.h:101`), тег 92 (`wmi-tlv.h:655`),
   тело `{vdev_id, vdev_assoc_id = AID, vdev_bssid(8)}`
   (`wmi.h:4999-5003`, сборка `wmi-tlv.c:2283-2305`, вызов
   `mac.c:3166`).
5. Обход прошивочного PS: `PEER_SET_PARAM DUMMY_VAR (0xff) = 1`
   (`mac.c:3177-3187`, `wmi-tlv.h:563`).

Вместе с ассоциацией mac80211 присылает и другие изменения BSS — ath10k
переводит их в `VDEV_SET_PARAM`: `BEACON_INTERVAL` (TLV id 3,
`mac.c:6128-6140`), `DTIM_PERIOD` (0xd, `mac.c:6173-6186`), `SLOT_TIME`
(7; LONG = 1, SHORT = 2, `mac.c:6233-6249`, `wmi.h:5596-5598`),
`PREAMBLE` (8; LONG = 1, SHORT = 2, `mac.c:6251-6267`,
`wmi.h:5600-5602`), CTS-защита (`mac.c:6217-6231`). id — по
перечислению `wmi-tlv.h:472-…` (строка − 472). Параметры WMM из Assoc
Response — `VDEV_SET_WMM_PARAMS` (`0x500d`, `wmi-tlv.h:109`; тег 199;
четыре AC в порядке BE, BK, VI, VO; `txop` в мкс = TXOP×32;
`mac.c:7860-7920`, `wmi-tlv.c:2495-2524`, `wmi.h:4341-4348`) —
необязательно для первого пинга.

### 3.6. Ключи (WPA2-PSK, CCMP)

4-way handshake ведёт supplicant, кадры EAPOL идут как данные через HTT
(§4.4). Когда supplicant выдаёт ключи, mac80211 зовёт `set_key`
(`mac.c:6562-6715`):

- **`VDEV_INSTALL_KEY`** = `0x5009` (`wmi-tlv.h:105`). TLV
  (`wmi-tlv.c:2354-2406`):
  1. `VDEV_INSTALL_KEY_CMD` = 96 (`wmi-tlv.h:659`):
     `wmi_vdev_install_key_cmd` (`wmi.h:5066-5083`, фиксированная часть
     92 байта) — `vdev_id, peer_macaddr(8), key_idx, key_flags,
     key_cipher, key_rsc_counter(8), key_global_rsc_counter(8),
     key_tsc_counter(8), wpi_key_rsc_counter[16], wpi_key_tsc_counter[16],
     key_len, key_txmic_len, key_rxmic_len`.
  2. `ARRAY_BYTE`: ключ (`key_len` байт, округлено до 4).
- Поля, как их ставит ath10k (`mac.c:239-305`, `6593-6660`):
  - `peer_macaddr` — **BSSID** и для попарного, и для группового ключа
    у STA (`mac.c:6593-6603`);
  - `key_idx` — индекс ключа (PTK обычно 0, GTK 1 или 2; максимум 3,
    `wmi.h:5029`);
  - `key_flags`: `WMI_KEY_PAIRWISE = 0x0` или `WMI_KEY_GROUP = 0x1`
    (`mac.c:6633-6636`, `wmi.h:5032-5033`);
  - `key_cipher` для CCMP — `AES_CCM = 4` (`mac.c:257-259`;
    одинаково в `wmi_cipher_suites` и `wmi_tlv_cipher_suites`,
    `wmi.h:5041-5063`; у GCM значения расходятся: 8 против 9);
  - `key_len = 16`, `txmic/rxmic = 0` (TKIP — 8/8, `mac.c:261-264`);
  - **счётчики RSC/TSC ath10k не заполняет — нули** (поля не трогаются
    ни в `mac.c:245-252`, ни в `wmi-tlv.c:2381-2390`).
- **Ждать `VDEV_INSTALL_KEY_COMPLETE`** = `0x5003` (`wmi-tlv.h:311`; тег
  42, `wmi-tlv.h:605`), срок 3 с (`mac.c:327-331`).
- **Авторизация порта**: сразу после установки **попарного** ключа —
  `PEER_SET_PARAM AUTHORIZE (0x3) = 1` для BSSID (`mac.c:6705-6710`,
  `wmi-tlv.c:4443`, `wmi-tlv.h:551`). `PEER_SET_PARAM` = `0x6004`
  (`wmi-tlv.h:117`), тег 100 (`wmi-tlv.h:663`), тело `{vdev_id,
  peer_macaddr(8), param_id, param_value}` (`wmi.h:6339-6344`).
  Переход mac80211 `ASSOC → AUTHORIZED` для не-TDLS ath10k не
  обрабатывает (`mac.c:7712-7714`). В открытой сети без ключей
  авторизовать нечем — там `NEED_PTK_4_WAY` не ставится, и прошивка,
  по всей видимости, пропускает данные сразу (вывод, не цитата).
- Снятие ключа: попарного — `cipher NONE`, без данных; группового —
  перезапись случайным ключом (`mac.c:292-302`).

---

## 4. Путь данных

### 4.1. Режим передачи

- Умолчание ath10k: `frame_mode = NATIVE_WIFI (1)` (`core.c:42`), vdev
  `TX_ENCAP_TYPE = NATIVE_WIFI` (`mac.c:5541-5543`), приём —
  `rx_decap_mode = NATIVE_WIFI` в `WMI_INIT` (`core.c:2665`,
  `wmi-tlv.c:1936`); `ETHERNET (2)` для TX используется ath10k только
  при offload инкапсуляции mac80211 и для TDLS-обхода
  (`mac.c:3749-3750`, `3788-3789`, `5535-5539`); `RAW (0)` — только с
  `raw-mode`, которого нет.
- Native Wifi на передаче: заголовок 802.11 **без QoS Control** (ath10k
  его вырезает и снимает подтип QoS, `mac.c:3818-3844`), затем LLC/SNAP и
  полезная нагрузка. TID передаётся в дескрипторе, а не в заголовке.
- Значит, Ethernet-драйверу нужно 802.3 → 802.11 (nwifi) самому: FC =
  Data, `ToDS = 1`, `Protected = 1` после установки ключа; `addr1 =
  BSSID, addr2 = свой, addr3 = DA`; затем `AA AA 03 00 00 00` + EtherType
  + данные. Место под IV/MIC **не нужно**: ath10k для CCMP просит
  mac80211 генерировать IV только для mgmt (`GENERATE_IV_MGMT`,
  `mac.c:259`), данные шифрует прошивка; лишние 8 байт MIC добавляются
  только для защищённых mgmt и RAW (`htt_tx.c:1626-1639`).

### 4.2. HTT `TX_FRM` (64-битный вариант, `ath10k_htt_tx_64`)

Функция — `htt_tx.c:1589-1799`; выбор 64-битных операций — по
`target_64bit` (`htt_tx.c:1812-1840`).

- **Дескриптор** `htt_data_tx_desc_64` (`htt.h:203-217`), 19 байт packed:
  `u8 flags0; __le16 flags1; __le16 len; __le16 id; __le64 frags_paddr;
  {__le16 peerid; __le16 freq}` или `__le32 peerid`.
  - `flags0` (`htt.h:133-141`): `MAC_HDR_PRESENT 0x1` (nwifi/raw/mgmt),
    `NO_AGGR 0x2`, `NO_ENCRYPT 0x4`, `NO_CLASSIFY 0x8`; `PKT_TYPE` в
    битах 5–7 = режим (`ATH10K_HW_TXRX_*`: 0 raw, 1 nwifi, 2 eth, 3 mgmt,
    `hw.h:417-428`) (`htt_tx.c:1652-1693`).
  - `flags1` (`htt.h:143-155`): `VDEV_ID` биты 0–5, `EXT_TID` биты 6–10,
    `POSTPONED 1<<11` — **ставится всегда**, чтобы прошивка не слала
    TX_INSPECT (`htt_tx.c:1734-1738`), `CKSUM_L3/L4_OFFLOAD 1<<13/1<<14`
    при частичной контрольной сумме.
  - `EXT_TID`: TID 0–15 для QoS-данных, **16** — не-QoS/мультикаст,
    **17** — mgmt (`htt.h:167-171`, `htt_tx.c:1167-1178`).
  - `len` = длина кадра; `id` = msdu_id (номер слота, по нему
    придёт завершение).
  - `peerid/freq`: при HTT ≥ 3.4 и TLV — `peerid = 0xFFFF`
    (`HTT_INVALID_PEERID`, `htt.h:173`), `freq = 0` (домашний канал);
    иначе `__le32 peerid = 0xFFFF` (`htt_tx.c:1748-1756`,
    `mac.c:3968-3979`).
- **Фрагментный дескриптор** (у WCN3990 `continuous_frag_desc = true`):
  массив `htt_msdu_ext_desc_64` по msdu_id в одной DMA-области
  (`htt.h:100-106`: `tso_flag[5]`, `ip_identification`, `flags`,
  `reserved`, `frags[6]`; 72 байта). Для кадра заполняется `frags[0]` в
  формате **triple-word**: `paddr_lo (32), paddr_hi (16), len (16)`
  (`htt.h:78-90`, `htt_tx.c:1658-1672`); остальное нули. `frags_paddr`
  в дескрипторе = физический адрес этой записи. Контрольная сумма —
  `tso_flag[3] |= 0x1f << 16` (`htt.h:120-131`, `htt_tx.c:1723-1732`).
- Банк фрагментных дескрипторов объявляется один раз на старте:
  `FRAG_DESC_BANK_CFG` (H2T 6, `htt.h:41`), 64-битный вариант
  `htt_frag_desc_bank_cfg64 {info, num_banks=1, desc_size=72,
  bank_base_addrs[], bank_id[] = {0, max_pending-1}, q_state}`
  (`htt.h:1509-1516`, `htt_tx.c:738-798`).
- **Что уходит в HTC** (`htt_tx.c:1712-1785`): два куска scatter-gather
  — (1) HTC-заголовок (8 байт, `eid` = HTT, `len` = 1 + 19 +
  prefetch) + байт типа `TX_FRM = 1` (`htt.h:36`) + дескриптор (19);
  (2) первые `prefetch_len` байт **самого кадра**: `36 + 4 + 8 + 2 = 50`
  → округлено до 52 (`htt.c:338-342`, `htt_tx.c:1619-1620`) — для
  классификатора прошивки. Весь кадр прошивка забирает по
  `frags[0].paddr`.
- Буфер кадра держать до завершения; слот `txbuf_64`
  (`htt.h:1765-1770`: `frags[2]`, HTC-заголовок, тип, дескриптор) — в
  одной DMA-области по msdu_id (`htt_tx.c:1622-1624`).

### 4.3. Завершение передачи

HTT T2H `TX_COMPL_IND` = `0x7` (`htt.h:442`), тело
`htt_data_tx_completion` (`htt.h:979-991`): `u8 flags` (статус в битах
0–2: `OK 0`, `DISCARD 1`, `NO_ACK 2`, `POSTPONE 3`; TID в 3–6),
`u8 num_msdus`, `u8 flags2`, затем `__le16 msdus[num_msdus]`
(`htt.h:880-895`, разбор `htt_rx.c:2951-3010`). У WCN3990 при бите
`flags2 & 0x8` (`HTT_TX_DATA_RSSI_ENABLE_WCN3990`, `htt.h:587`;
`hw.c:1114-1118`) после списка идут ещё массивы (ACK RSSI, retries,
timestamp), список msdu при нечётном числе добивается `0xFFFF`
(`htt_rx.c:2989-3005`, `hw.c:1120-1133`). msdu_id `0xFFFF` —
недействительный (`htt.h:895`).

### 4.4. EAPOL (0x888E)

- **Особого пути у ath10k нет** (поиск `0x888e`/`PAE` по `mac.c`,
  `htt_tx.c`, `htt_rx.c`, `txrx.c` пуст). Всё решают флаги mac80211:
  кадр без бита `Protected` → `ATH10K_SKB_F_NO_HWCRYPT`
  (`mac.c:3798-3816`, `3918-3919`) → `flags0 |= NO_ENCRYPT (0x4)`
  (`htt_tx.c:1718-1719`). Тот же TX_FRM, nwifi, TID по приоритету.
- До установки PTK: сообщения 2 и 4 отправлять с `Protected = 0` и
  `NO_ENCRYPT`. После установки ключей (rekey) EAPOL идёт шифрованным,
  как обычные данные.
- Приём: EAPOL приходит как обычные данные (§4.5) с `enctype =
  NONE (7)` (`rx_desc.h:243-255`), `is_decrypted = false` — кадр отдаётся
  как есть (`htt_rx.c:2011-2014`, `2099-2100`).
- `VDEV_PARAM_DROP_UNENCRY` (TLV 0x2f, `wmi-tlv.h:519`) ath10k не
  трогает.

### 4.5. Приём: какое сообщение несёт данные

- Ring буферов хоста объявляется `RX_RING_CFG` (H2T 2, `htt.h:37`),
  64-битный вариант (`htt_tx.c:885-953`): `htt_rx_ring_setup_64 {hdr
  {num_rings = 1}, rings[1]}` (`htt.h:269-293`) — `fw_idx_shadow_reg_paddr`
  (адрес слова-индекса, которое пишет хост), `rx_ring_base_paddr`,
  `rx_ring_len = 2048`, `rx_ring_bufsize = 2048`, `flags` — все 16 бит
  `HTT_RX_RING_FLAGS_*` (`htt.h:219-236`), `fw_idx_init_val`, смещения
  частей дескриптора в 4-байтных словах (`htt.h:244-256`, заполняет
  `htt.c:168-182`).
- Кольцо — 2048 записей `__le64` физических адресов (`htt.h:238-240`,
  `htt_rx.c:86-113`), буфер 2048 байт, выровнен на 8 (`htt.h:2384`,
  `2393`); перед выдачей обнулять слово attention; после заполнения —
  барьер и запись индекса (`htt_rx.c:135-211`).
- **Какое T2H-сообщение**: `RX_IN_ORD_PADDR_IND` = `0x12` (`htt.h:453`),
  если прошивка объявила сервис `RX_FULL_REORDER` (`core.c:3076-3080`;
  тогда и `WMI_INIT` с `num_offload_peers/reorder_bufs`,
  `wmi-tlv.c:1917-1923`), иначе классический `RX_IND` = `0x1`
  (`htt_rx.c:4129-…`). Для WCN3990 в ath10k есть 64-битная форма
  in-order (`htt.h:1097-1102`, `htt_rx.c:722-785`) — ожидаемо именно
  она; **проверить по сервисному биту на плате**.
- `htt_rx_in_ord_ind` (`htt.h:1104-1124`): `u8 info` (TID 0–4,
  `OFFLOAD` бит 5, `FRAG` бит 6), `__le16 peer_id`, `u8 vdev_id`,
  `u8 reserved`, `__le16 msdu_count`, затем `msdu_count` записей
  `htt_rx_in_ord_msdu_desc_ext {__le64 msdu_paddr; __le16 msdu_len; u8
  fw_desc; u8 reserved}` (12 байт). Буфер находится по физическому
  адресу (`htt_rx.c:741-743`); в нём: дескриптор (360 байт), затем
  MSDU длиной `msdu_len` (`htt_rx.c:763-778`); `MSDU_DONE` в attention
  обязателен. Кадры приходят **уже упорядоченными**, A-MSDU может
  растягиваться на несколько индикаций (`htt_rx.c:3309-3378`).

### 4.6. Дескриптор приёма WCN3990 (`htt_rx_desc_v2`, `htt.h:2108-2125`)

Размер **360 байт** (`rx_desc_size = sizeof(htt_rx_desc_v2)`,
`htt.c:270-271`); раскладка подсчитана компиляцией объявлений
`rx_desc.h` (смещения в байтах):

| Часть | Смещение | Размер | Определение |
|---|---|---|---|
| `fw_desc` (база) | 0 | 4 | `htt.h:2098-2106`, `rx_desc.h:1305-1307` |
| `attention` | 4 | 4 | `rx_desc.h:48-50`, флаги `rx_desc.h:13-45` |
| `frag_info` | 8 | 8 | `rx_desc.h:200-219` |
| `mpdu_start` | 16 | 12 | `rx_desc.h:273-284` |
| `msdu_start` | 28 | 20 | `rx_desc.h:474-495` (+`wcn3990`: info2, info3) |
| `msdu_end` | 48 | 56 | `rx_desc.h:589-638` (+`wcn3990`: 9 слов) |
| `mpdu_end` | 104 | 4 | `rx_desc.h:374-376` |
| `ppdu_start` | 108 | 40 | `rx_desc.h:748-763` |
| `ppdu_end` | 148 | 148 | `rx_desc.h:1163-1168`, `1149-1161` |
| `rx_hdr_status` | 296 | 64 | `htt.h:2085`, исходный заголовок 802.11 |
| `msdu_payload` | 360 | — | данные MSDU |

Нужные поля:
- `attention.flags`: `MSDU_DONE` бит 31, `FCS_ERR` 30, `DECRYPT_ERR` 29,
  `TKIP_MIC_ERR` 28, `PEER_IDX_INVALID` бит 3, `MCAST_BCAST` бит 2
  (`rx_desc.h:14-45`).
- `mpdu_start.info0`: `ENCRYPT_TYPE` биты 28–31 (`rx_desc.h:261-262`;
  `AES_CCM_WPA2 = 6`, `NONE = 7`, `rx_desc.h:243-255`), `ENCRYPTED`
  бит 13, `PEER_IDX` 0–10, `SEQ_NUM` 16–27.
- `msdu_start.info0`: длина MSDU биты 0–13; `info1`: **`DECAP_FORMAT`
  биты 8–9** (`rx_desc.h:416-428`): `RAW 0, NATIVE_WIFI 1,
  ETHERNET2_DIX 2, 8023_SNAP_LLC 3` (`rx_desc.h:457-472`).
- `msdu_end.info0`: `FIRST_MSDU` бит 14, `LAST_MSDU` бит 15
  (`rx_desc.h:583-584`).
- L3-выравнивания у WCN3990 нет (`rx_desc_get_l3_pad_bytes` не задан,
  `htt.c:270-286`, `htt.h:2190-2195`).

### 4.7. Разбор принятого кадра в Native Wifi

- В буфере после дескриптора: **nwifi-заголовок** (3-адресный, без QoS
  Control), затем LLC/SNAP и данные; заголовка подкадра A-MSDU нет даже
  внутри A-MSDU — каждый MSDU отдельно (`htt_rx.c:1570-1579`).
- Длина nwifi-заголовка — `ieee80211_hdrlen`, округлённая до 4, **если
  нет** фичи `no-4addr-pad`; у этой прошивки фича есть — не округлять
  (`htt_rx.c:1414-1424`).
- ath10k восстанавливает исходный заголовок 802.11 из `rx_hdr_status`
  (первые 64 байта, `htt_rx.c:1975-1979`), а DA/SA берёт из nwifi
  (`htt_rx.c:1581-1613`). Для Ethernet-драйвера проще: из
  nwifi-заголовка взять DA (`addr1`) и SA (`addr3` при FromDS), пропустить
  LLC/SNAP (`AA AA 03 00 00 00`) и собрать `DA, SA, EtherType` + данные.
- Расшифрованный кадр: прошивка сняла IV/MIC (`RX_FLAG_IV_STRIPPED`,
  `MMIC_STRIPPED`), ath10k снимает бит `Protected` (`htt_rx.c:2049-2108`).
  Проверка PN при снятом IV хосту недоступна — её делает прошивка
  (вывод: mac80211 с `IV_STRIPPED` PN не проверяет).
- Кадры с `FCS_ERR`/`DECRYPT_ERR`/`TKIP_MIC_ERR` — выбрасывать
  (`htt_rx.c:2002-2028`). Mgmt через HTT (`MGMT_TYPE` в attention) ath10k
  отдаёт только монитору — mgmt приходят через WMI `MGMT_RX`
  (`htt_rx.c:2041-2047`).

### 4.8. Порядок подъёма HTT (`ath10k_htt_setup`, `htt.c:391-436`)

`VERSION_REQ` (H2T 0) → ждать `VERSION_CONF` (T2H 0x0) →
`FRAG_DESC_BANK_CFG` (H2T 6) → `RX_RING_CFG` (H2T 2) → `AGGR_CFG`
(H2T 5; у 64-битных — v2, `htt_tx.c:1053-…`) (`htt.c:398-433`).

---

## 5. Минимальный набор

### 5.1. «Скан → список сетей»

| # | Команда / событие | Id | Назначение |
|---|---|---|---|
| 1 | HTT VERSION_REQ / VERSION_CONF | H2T 0 / T2H 0x0 | рукопожатие HTT (`htt.h:35`, `435`) |
| 2 | HTT FRAG_DESC_BANK_CFG | H2T 6 | банк фрагментных дескрипторов (`htt.h:41`) |
| 3 | HTT RX_RING_CFG | H2T 2 | кольцо приёма (`htt.h:37`) |
| 4 | WMI SERVICE_READY / INIT / READY | ev 0x1 / cmd 0x1 / ev 0x2 | старт WMI (`wmi-tlv.h:73`, `297-298`) |
| 5 | PDEV_SET_PARAM ×n | 0x4003 | `PMF_QOS`, `DYNAMIC_BW`, `IDLE_PS_CONFIG`, `ARP_AC_OVERRIDE`, `ANI_ENABLE` (`wmi-tlv.h:82`) |
| 6 | SCAN_CHAN_LIST | 0x3003 | разрешённые каналы, пассивность, мощность (`wmi-tlv.h:76`) |
| 7 | PDEV_SET_REGDOMAIN | 0x4001 | домен и CTL (`wmi-tlv.h:80`) |
| 8 | VDEV_CREATE | 0x5001 | STA-vdev, тип 2 / подтип 0 (`wmi-tlv.h:97`) |
| 9 | VDEV_SET_PARAM `TX_ENCAP_TYPE = 1` | 0x5008 | nwifi (`wmi-tlv.h:104`) |
| 10 | STA_POWERSAVE_PARAM ×3 | 0x9002 | правила пробуждения (`wmi-tlv.h:139`) |
| 11 | START_SCAN | 0x3001 | аппаратный скан (`wmi-tlv.h:74`) |
| 12 | SCAN event | ev 0x3001 | STARTED / FOREIGN_CHANNEL / COMPLETED (`wmi-tlv.h:300`) |
| 13 | MGMT_RX event | ev 0x7001 | beacon и probe response (`wmi-tlv.h:322`) |
| 14 | STOP_SCAN | 0x3002 | отмена (`wmi-tlv.h:75`) |

### 5.2. «Подключение WPA2-PSK → ping» (сверх 5.1)

| # | Команда / событие | Id | Назначение |
|---|---|---|---|
| 15 | VDEV_START_REQUEST | 0x5003 | vdev на канал AP (`wmi-tlv.h:99`) |
| 16 | VDEV_START_RESP event | ev 0x5001 | подтверждение старта (`wmi-tlv.h:309`) |
| 17 | STA_POWERSAVE_MODE | 0x9001 | PS выкл. (0) (`wmi-tlv.h:138`) |
| 18 | PEER_CREATE | 0x6001 | peer = BSSID (`wmi-tlv.h:114`) |
| 19 | HTT PEER_MAP | T2H 0x3 | peer создан, его `peer_id` (`htt.h:438`) |
| 20 | MGMT_TX_SEND (по ссылке) | 0x7008 | Auth, Assoc Request (`wmi-tlv.h:131`) |
| 21 | MGMT_TX_COMPLETION (+BUNDLE) | ev 0x7006 (0x7007) | освобождение буфера mgmt (`wmi-tlv.h:327-328`) |
| 22 | MGMT_RX event | ev 0x7001 | Auth Response, Assoc Response, Deauth/Disassoc |
| 23 | PEER_ASSOC | 0x6005 | возможности и скорости AP, AID, флаги (`wmi-tlv.h:118`) |
| 24 | VDEV_UP | 0x5005 | AID + BSSID, vdev «в сети» (`wmi-tlv.h:101`) |
| 25 | VDEV_SET_PARAM (beacon interval, DTIM, slot, preamble) | 0x5008 | параметры BSS |
| 26 | HTT TX_FRM | H2T 1 | данные, в т. ч. EAPOL с `NO_ENCRYPT` (`htt.h:36`) |
| 27 | HTT TX_COMPL_IND | T2H 0x7 | завершение данных (`htt.h:442`) |
| 28 | HTT RX_IN_ORD_PADDR_IND (или RX_IND) | T2H 0x12 (0x1) | принятые данные (`htt.h:453`, `436`) |
| 29 | VDEV_INSTALL_KEY ×2 | 0x5009 | PTK (pairwise, idx 0) и GTK (group) для BSSID, CCMP = 4 (`wmi-tlv.h:105`) |
| 30 | VDEV_INSTALL_KEY_COMPLETE event | ev 0x5003 | подтверждение ключа (`wmi-tlv.h:311`) |
| 31 | PEER_SET_PARAM `AUTHORIZE (3) = 1` | 0x6004 | открыть порт после PTK (`wmi-tlv.h:117`, `551`) |
| 32 | PEER_STA_KICKOUT / ROAM event | ev 0x6001 / 0xb001 | потеря AP (`wmi-tlv.h:315`, `333`) |
| 33 | PEER_DELETE / VDEV_DOWN / VDEV_STOP | 0x6002 / 0x5007 / 0x5006 | разрыв (`wmi-tlv.h:115`, `102-103`) |
| 34 | HTT TX_CREDIT_UPDATE_IND / WMI TX_PAUSE | T2H 0xf / ev 0x4005 | управление потоком — обработать хотя бы игнорированием (`htt.h:450`, `wmi-tlv.h:305`) |

Необязательно для первого пинга: `VDEV_SET_WMM_PARAMS` (0x500d),
`PEER_SET_PARAM SMPS_STATE (1)`, `STA_KEEPALIVE` (0x1c005),
`PDEV_SET_PARAM PEER_STATS_INFO_ENABLE`, `PEER_SET_PARAM DUMMY_VAR`
(обход PS — пригодится, если включать PS).

---

## 6. Что за mac80211 придётся сделать драйверу

- **MLME клиента**: построение Authentication (Open System, алгоритм 0,
  seq 1), Association Request (capability, listen interval, SSID,
  Supported/Extended Rates, RSN IE от supplicant, HT Capabilities, WMM
  Information Element при желании QoS), Deauth/Disassoc; номера
  последовательности mgmt-кадров; разбор Auth Response (статус, seq 2),
  Assoc Response (статус, AID & 0x3fff, скорости, HT/VHT, WMM
  Parameter), Deauth/Disassoc от AP; таймауты и повторы.
- **Сканирование**: блоб IE для probe request (TLV 5 команды скана),
  разбор beacon/probe response (SSID, BSSID, DS Parameter Set → канал,
  RSSI = SNR − 95, capability, RSN/WPA IE, наборы скоростей), кэш BSS
  с устареванием.
- **Расчёт аргументов `PEER_ASSOC`**: пересечение наборов скоростей в
  формате ath10k (500 кбит/с + бит 7 для CCK), HT MCS, `phy_mode`, флаги
  (`AUTH`, `QOS`, `NEED_PTK_4_WAY`, `HT`, `40MHZ`…); chandef для
  `VDEV_START` (HT40± по HT Operation).
- **Конвертация 802.3 ↔ nwifi** с LLC/SNAP, выбор TID по приоритету,
  `NO_ENCRYPT` для EAPOL до ключа.
- **Интерфейс supplicant'у**: приём/отправка EAPOL как Ethernet-кадров
  0x888E, команды «установить ключ» (PTK/GTK, idx, шифр) и «авторизовать»,
  уведомления о связи/разрыве. 4-way handshake и PSK→PMK — в supplicant.
- **Регуляторика**: свой список каналов и CTL (кода `ath/regd.c` в
  наборе нет).
- **Не нужно**: расшифровка, PN-проверка, дедупликация и переупорядочение
  (in-order: прошивка), повторы и выбор скорости (прошивка), probe
  request в скане (прошивка), beacon-miss — прошивка сообщает
  `PEER_STA_KICKOUT`/`ROAM` (обработчики — в `wmi.c`).
