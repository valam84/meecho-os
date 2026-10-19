# Mi Pad 4: Wi-Fi WCN3990 (ath10k_snoc) — разбор и план

Начато 2026-10-19. Эталон — postmarketOS на этом же планшете
(`hw-pmos.md`, «Wi-Fi»; тексты — `pmos-20261019/pass1/wifi/`,
`pass2/wifi/`). Справка по протоколу — Linux `drivers/net/wireless/ath/
ath10k/` и `drivers/soc/qcom/qmi_*`, `net/qrtr/` (ветка `sdm660-mainline`
`72bc4bb`, та же, что у дерева MEECHO; копия для чтения — `~/wifi-ref` в
WSL, **вне дерева**). Код Linux — GPL: отсюда берутся номера и порядок, не
текст. Метки как в `hw-gpu.md`: [C] — из исходников, [T] — снято с
планшета, [I] — вывод, [?] — не проверено.

**Главная зависимость.** Прошивка Wi-Fi исполняется **в модеме**
(`wlanmdsp.mbn` в `modem/image`); хост говорит с ней QMI-службой wlfw по
QRTR. Весь транспорт к модему (smem, smp2p, GLINK, загрузка MSS, QRTR,
rmtfs, tqftpserv, общая библиотека QMI) делает параллельная сессия
«Модем Mi Pad 4»; её документ — `hw-modem.md`. Здесь — только то, что
поверх QRTR.

## 1. Устройство в дереве [T]

`soc@0/wifi@18800000` (`pmos-20261019/sdm660-xiaomi-clover-pmos.dts`):

| Что | Значение | Что это для драйвера |
|---|---|---|
| `compatible` | `qcom,wcn3990-wifi` | строка `devicetree` в `system.conf` |
| `reg` | `0x18800000`, 8 МБ (`membase`) | регистры copy engine: CE n — `+0x240000 + n·0x1000`, обёртка CE — `+0x24c000` |
| `interrupts` | 12 линий SPI: 413–418, 420–425 (без 419), уровень | **CE0…CE11 по порядку**; INTID = SPI + 32: 445–450, 452–457 |
| `memory-region` | `wlan-msa-mem@85700000`, 1 МБ, `no-map` (+ guard `@85600000`, 1 МБ) | память MSA прошивки; RS выдаёт её по `memory-region` |
| `iommus` | `apps_smmu` (`iommu@16c0000`) SID `0x1a00`, `0x1a01` | DMA copy engine идёт **через SMMU** (§6) |
| `clocks` | `cxo_ref_clk_pin` (RPM `0x16`) | опорная XO к RF; в Linux `clk_bulk_prepare_enable` |
| питание | `vdd-0.8-cx-mx`, `vdd-1.8-xo`, `vdd-1.3-rfa`, `vdd-3.3-ch0` (PM660/PM660L); `vdd-3.3-ch1` нет — dummy | см. §6 |
| `qcom,snoc-host-cap-8bit-quirk` | | HOST_CAP в 8-битном варианте (§3) |
| `qcom,no-msa-ready-indicator` | | не ждать `MSA_READY_IND`: после HOST_CAP/MSA сразу BDF |
| `qcom,calibration-variant` | нет | имя платы без `variant=` |
| `wifi-firmware` (подузел) | нет | Linux: `use_tz = true` — память MSA модему отдаёт TrustZone (`qcom_scm_assign_mem`), своего домена SMMU для прошивки нет |

## 2. Файлы прошивки [T][C]

Хост читает два файла — `ath10k/WCN3990/hw1.0/` в `/lib/firmware`
планшета (пакет `firmware-xiaomi-clover`). Копия — **вне дерева**:
`D:\minix\clover-flash\pmos\fw\ath10k\WCN3990\hw1.0\` (снята 2026-10-19
`scp` с планшета; md5 `4cd45786…` и `ac119c17…`).

- **`firmware-5.bin` — 60 байт, кода в нём нет**: магия `QCA-ATH10K`,
  элементы TIMESTAMP, FEATURES (`0x1d0140`: wowlan, no-4addr-pad,
  allows-mesh-bcast, mgmt-tx-by-reference, non-bmi,
  single-chan-info-per-channel), WMI_OP_VERSION 4 (TLV), HTT_OP_VERSION 3;
  элемента FW_VERSION нет — отсюда «`firmware ver  api 5`» с двумя
  пробелами в `dmesg`.
- **`board-2.bin` — 25 плат**, выбор по имени. QMI говорит `board_id
  0xff`, `chip_id 0x30214`; имя с `qmi-chip-id` в файле **отсутствует**,
  совпадает второй запасной — **`bus=snoc,qmi-board-id=ff`**: 19152 байта,
  сумма `380d3b6d` — та самая, что в `fw_checksums` у Linux.
- **Сумма в `fw_checksums` и `dmesg` — `crc32_le(0, …)`**: без начальной
  и конечной инверсии, поэтому у zlib для того же файла число другое
  (`063e198f` против `022c9087`). Первый прогон стенда «не совпал» именно
  на этом.
- `bdwlan.*` в `modem/image` — данные плат для других `board_id`; хосту
  они не нужны, прошивка их не просит.

Разбор файлов — `minix/drivers/net/wcn/wcn_fw.c` (без MINIX); стенд —
`port/test/wcn/run.sh` (`fwtest`: 25 проверок — эталонные суммы, признаки,
выбор платы, все усечения файла, неверная магия, длина за концом, имена с
псевдонимами).

## 3. Подъём прошивки: wlfw поверх QMI поверх QRTR [C]

Подробно, байт в байт, с примерами — `port/clover/wifi/wlfw.md`. Главное:

- **QMI**: заголовок 7 байт (u8 тип: 0 запрос, 2 ответ, 4 индикация; le16
  txn; le16 msg id; le16 длина), дальше TLV (u8 тип, le16 длина).
  Необязательное поле — отсутствие TLV, а не флаг. Счётчик массива — 1 байт,
  если у элемента-счётчика размер 1, иначе 2 (BDF `data` — 2; `tgt_cfg`,
  `svc_cfg`, `shadow_reg`, `mem_region_info` — 1). Ответ сопоставляется по
  txn; общий ответ — TLV 0x02 (result, error по u16).
- **QRTR**: служба wlfw — service **0x45**, version 1, instance **1**;
  транспорт — канал GLINK `IPCRTR` к модему. Всё ниже QMI — работа сессии
  модема; что от неё нужно — §9.
- **Порядок ath10k** (с обоими флажками нашего дерева):

| # | Сообщение | id | Что |
|---|---|---|---|
| 1 | IND_REGISTER | 0x20 | client_id `0x4b4e454c`, fw_ready и msa_ready; ответ с `fw_status & 0x02` (FW_READY) — прошивка уже живёт, шаги 2–9 пропускаются |
| 2 | HOST_CAP | 0x34 | **8-битный вариант**: TLV 0x10 — `10 01 00 00` вместо `10 04 00 00 00 00 00`; NOT_SUPPORTED — не ошибка |
| 3 | MSA_INFO | 0x2d | адрес и размер MSA (0x85700000, 1 МБ); ответ — регионы внутри |
| 4 | — | | 20 мс паузы, затем **hyp-assign** каждого региона (TrustZone SiP 0x0c/0x16): HLOS → MSS_MSA 0xf, WLAN 0x18 (+ WLAN_CE 0x19, если не secure), RW |
| 5 | MSA_READY | 0x2e | отказ печатается, но не считается ошибкой |
| 6 | CAP | 0x24 | chip_id, board_id, soc_id, fw_version, build id — **то, что сверяется с эталоном** |
| 7 | BDF_DOWNLOAD | 0x25 | плата из §2 кусками ≤ 6144 байта; на последнем куске прошивка может ответить MALFORMED (CRC) — терпится |
| 8 | CAL_REPORT | 0x26 | пустой |
| 9 | ждать FW_READY_IND | 0x21 | `no-msa-ready-indicator`: MSA_READY_IND (0x2b) не ждём |
| 10 | WLAN_CFG | 0x23 | таблицы CE для цели (12), служба→труба (21 с нулевой записью), теневые регистры (12) |
| 11 | WLAN_MODE | 0x22 | MISSION (0), hw_debug 0 |

Эталон шагов 1–6 у нас есть словами (`dmesg`: chip_id 0x30214,
chip_family 0x4001, board_id 0xff, soc_id 0x40050000, fw_version
0x101f81f0, `WLAN.HL.1.0.1.c2-00496-QCAHLSWMTPLZ-1`), байтами — нет.
Снять байты на pmos — §10.

## 4. Copy engine [C]

Подробно — `port/clover/wifi/ce.md`. Двенадцать колец DMA; CE n —
`0x240000 + 0x1000·n` от `membase`. Три отличия WCN3990 от PCIe-чипов QCA:
дескриптор 16 байт (адрес 37 бит: 31:0 и 4:0 следующего слова; gather —
бит 31 там же и бит 0 флагов); write index кольца отправки — в **теневой
регистр** `0x32000 + 4·n`; read index'ы движки пишут **в память хоста**
(массив 12×u32, адрес — `0x24c004/8`, бит 19 CTRL1). Прерывание — своя
линия на CE; CE4 (отправка HTT) без прерывания, его опрашивают при приёме.
Маска DMA — 35 бит.

| CE | Хост | Для чего |
|---|---|---|
| 0 | отправка 16 × 2048 | HTC управление |
| 1 | приём 512 × 2048 | HTT + HTC |
| 2 | приём 64 × 2048 | WMI |
| 3 | отправка 32 × 2048 | WMI |
| 4 | отправка 2048 × 256, опрос | HTT |
| 5 | приём 512 × 512 | HTT (без HTC-заголовка) |
| 7 | 2 + 2 | диагностика — буферов не даём |
| 8–11 | приём | uMC, HTT, HTT, pktlog |

**Кэши — решение MEECHO, не Linux.** Linux держит кольца в некэшируемой
памяти. У MEECHO память DMA — обычная кэшируемая, обслуживаемая руками
(`sys_cachectl`), а четыре дескриптора по 16 байт делят строку 64 байта, и
дескрипторы приёма пишет движок. Значит:

1. строка кольца приёма пишется хостом, только когда в ней нет
   дескриптора, выданного движку (`[DRRI, DWI)`) — иначе чистка хоста
   затрёт то, что движок только что заполнил;
2. отсюда буферы приёма выдаются **по строке дескрипторов** (write index
   приёма всегда кратен 4), а заполненный дескриптор хост обратно не пишет
   (Linux обнуляет `nbytes`);
3. буфер приёма перед выдачей — clean+invalidate, после прихода —
   invalidate; дескриптор приёма и слово RRI — invalidate перед чтением;
   дескриптор отправки и его байты — clean до записи индекса;
4. всё, что пишет движок, начинается и кончается на границе строки.

Место в кольце отправки считается от **забранных** завершений, а не от
read index движка, как у Linux: иначе можно записать слот, контекст
которого ещё не отдан.

Код — `minix/drivers/net/wcn/wcn_ce.c` (без MINIX: регистры, память и кэш
— через таблицу функций). Стенд `port/test/wcn/cetest.c`: модель двенадцати
движков (эхо HTC/WMI на CE2, HTT на CE1, gather, `nbytes` позже DRRI, CE4
без прерывания) **за моделью кэша с обратной записью**, построчно: чистка
поверх записи движка, выброшенная запись хоста, чтение движком
невычищенного, чистка строки, которой владеет движок, — нарушения. Два
режима: кольца как у Linux (~80 тыс. проверок) и маленькие медленные (`-s`,
~74 тыс.: только там хост догоняет движок внутри одной строки). Семь
мутантов — без clean, без invalidate, без flush, чужой адрес DMA, без
правила строки, индекс приёма на каждый буфер (как у Linux), место отправки
от индекса движка (как у Linux) — пойманы все (`run.sh`, `mutate.sh`).
**Правило строки без режима `-s` не ловилось**: при кольцах по 512 хост
никогда не догоняет движок, и опасного состояния не возникает; не поймал
его и асинхронный движок — поймала прямая проверка правила в модели.

Чего стенд не проверит: атрибуты памяти, порядок записи движком полей
дескриптора, настоящий кэш — [?] до планшета.

## 5. HTC, WMI-TLV, HTT [C]

Подробно — `port/clover/wifi/htc-wmi.md` (подъём) и `wifi/sta.md`
(сканирование, подключение, данные). Главное:

- **HTC**: заголовок 8 байт; READY от цели на endpoint 0 (CE2), затем
  CONNECT для WMI_CONTROL (0x0100: UL 3 / DL 2) и HTT_DATA (0x0300: UL 4 /
  DL 1, без кредитов, флаг 0x0008), SETUP_COMPLETE_EX. **Кредит у WCN3990
  один** (`use_fw_tx_credits = false`): в полёте одна WMI-команда.
- **WMI-TLV**: id команды — `группа << 12 | n`; заголовок TLV — **сначала
  le16 длина, потом le16 тег**. SERVICE_READY (событие 0x1) → INIT (команда
  0x1; abi 0x01000000/53, ns `0x5f414351`/`0x4c4d`) → READY (событие 0x2).
  Битовая карта служб — 4 бита на 32-битное слово (служба id → слово id/4,
  бит id%4); `wmi_services.txt` эталона переведён в ожидаемые слова
  (`wifi/htc-wmi.md`, 5.1) — сверка на первом прогоне. Нужны ли прошивке
  куски памяти хоста (`num_mem_reqs`) — напечатать на первом прогоне.
- **HTT** — только после READY: VERSION_REQ → CONF (ждём **3.56**),
  FRAG_DESC_BANK_CFG, RX_RING_CFG (кольцо 2048 × le64 адресов, 2047
  буферов по 2048, «индекс прошивки» — le32 в памяти хоста), AGGR_CFG. Все
  варианты 64-битные. Приём кадров — `RX_IN_ORD_PADDR_IND` (0x12) с
  адресами буферов, если служба RX_FULL_REORDER есть (у эталона есть;
  решать после SERVICE_READY, а не как Linux — по прошлому запуску).
- **Станция** (`wifi/sta.md`, таблица из 34 шагов): VDEV_CREATE (STA) →
  PDEV-параметры, SCAN_CHAN_LIST, PDEV_SET_REGDOMAIN → START_SCAN (0x3001),
  кадры beacon/probe response — событием MGMT_RX (0x7001) → VDEV_START,
  PEER_CREATE (ответ — HTT PEER_MAP) → auth и assoc request **кадрами,
  которые строит хост**, через MGMT_TX_SEND (0x7008: кадр по адресу и
  первые 64 байта в команде) → PEER_ASSOC, VDEV_UP → ключи
  VDEV_INSTALL_KEY (CCMP) → PEER_SET_PARAM AUTHORIZE. Данные — native
  Wi-Fi (заголовок 802.11 без QoS + LLC/SNAP), EAPOL — тем же путём с
  `NO_ENCRYPT`.
- Прошивка сама делает шифрование, проверку PN, переупорядочивание,
  выбор скорости и отправку probe request. **Хосту остаётся MLME клиента**
  (кадры auth/assoc/deauth, разбор ответов, кэш BSS, таблица каналов),
  перевод 802.3 ↔ native Wi-Fi и параметры PEER_ASSOC.

## 6. SMMU, питание, такт [T][C][?]

- **SMMU.** CE ходит в память через `apps_smmu` (`iommu@16c0000`,
  `qcom,sdm630-smmu-v2`), SID `0x1a00`, `0x1a01`; тот же SMMU у IPA
  (`0x19c0`, `0x19c2`) — части модема. Все адреса, которые видит прошивка
  (кольца, буферы, RRI, кадры MGMT_TX), — IOVA. Опыт `kgsl_smmu`
  (`hw-gpu.md`, §15–16) переносится целиком: SMR пишутся, **S2CR
  перехватывает гипервизор** — «fault» молча игнорирует, запись из `wzr`
  останавливает машину; каждая запись — одной `str` из обычного регистра,
  значение downstream (`0x300`: translate, банк, SHCFG NSH). Что загрузчик
  оставил в `apps_smmu` для этих SID — неизвестно: первый шаг на планшете
  — **чтение без записей** (sCR0, SMR, S2CR, свободные банки).
- **Кому принадлежит `apps_smmu`.** `devicetree "qcom,sdm630-smmu-v2"` в
  `system.conf` выдал бы все четыре SMMU SoC с этой строкой, а у IPA и
  Wi-Fi он общий. [I] Решение — расширить RS так же, как для
  `memory-region`: выдавать узел, на который указывает `iommus` совпавшего
  узла; делить банки и SID между службами — отдельный вопрос, когда IPA
  тоже придёт.
- **Питание** — четыре LDO PM660 через RPM: `vdd-0.8-cx-mx` — L5,
  `vdd-1.8-xo` — L9, `vdd-1.3-rfa` — L6, `vdd-3.3-ch0` — L19 (общие с
  Bluetooth той же WCN3990). При старте MEECHO `qcom_pmic` видит
  включёнными `1 3 6 8 9 10 11 13 14 19` (`hw-rpm.md`): **L5 выключен** —
  включать запросом RPM (служба `qcom_rpm`).
- **Такт** — `rf_clk1_pin` RPM (`cxo_ref_clk_pin`, 19.2 МГц; в эталоне
  включён одним потребителем — Wi-Fi). [?] Умеет ли `qcom_rpm` MEECHO
  ставить такты RPM — проверить.
- **Линии**: 12 SPI, уровень; CE i — i-я линия (INTID 445–450, 452–457).

## 7. 802.11 и WPA2 — решение [I]

Подробно — `port/clover/wifi/wpa.md`.

- **net80211 не переносим.** В дереве — только заголовки (FreeBSD 2005); сам
  стек — 21 тыс. строк без 802.11n/ac, на mbuf, ifnet, ядерном sysctl;
  вдобавок пришлось бы тянуть ioctl `SIOCS80211` и события
  `RTM_IEEE80211` через `lwip` и `libnetdriver`, где их нет.
- **MLME клиента — в драйвере**, при любом выборе суппликанта: и в
  Linux кадры auth/assoc строит mac80211, а не wpa_supplicant (даже в
  режиме SME у nl80211). Это 1–2 тыс. строк, половина которых нужна
  так или иначе (скорости, HT/VHT, `PEER_ASSOC`).
- **WPA2 — wpa_supplicant (hostap 2.11) из NetBSD-current, нетронутым** в
  `external/bsd/wpa/dist` (BSD-3); reachover под MEECHO без `driver_bsd`,
  `wired`, pcap, kqueue и методов EAP (позже — тем же импортом), с
  `-lcrypto` (OpenSSL 3.5 уже есть). Свой код — в `minix/`:
  `driver_meecho.c` (бэкенд: scan2/get_scan_results2, associate с RSN IE
  суппликанта, set_key, set_supp_port, tx_control_port, события), свой
  `drivers.c` (таблица бэкендов в dist — под `#ifdef`) и
  `l2_packet_meecho.c`.
- **EAPOL — по «control port» службы, а не через BPF**: нет гонки M4 с
  установкой ключа, не нужна libpcap (для evbarm64 её нет).
- **Связь суппликанта со службой** — её символьное устройство
  (`/dev/wlan0`): ioctl для команд, `read` для событий и EAPOL. Данные
  идут к `lwip` как от обычного `netdriver` (Ethernet); `lwip` и `ndev` не
  меняются. Служба — и `netdriver`, и `chardriver`: чужие сообщения
  `netdriver_process()` отдаёт в `ndr_other`, оттуда — в
  `chardriver_process()`.
- Готовность MINIX для wpa_supplicant проверена по коду, не на машине:
  `uds` умеет `SOCK_DGRAM` (ctrl_iface), `CLOCK_MONOTONIC` есть,
  `/dev/urandom` есть.

## 8. Раскладка драйвера `minix/drivers/net/wcn/`

| Файл | Что | Без MINIX (стенд) |
|---|---|---|
| `wcn_fw.c` | `firmware-N.bin`, `board-2.bin` | да — `fwtest` (**сделано**) |
| `wcn_ce.c` | copy engine | да — `cetest` (**сделано**) |
| `wcn_wlfw.c` | сообщения wlfw §3 (для `wcnd`, §9) | да — `wlfwtest` против байтов Linux (**сделано**) |
| `wcn_htc.c` | HTC: READY, CONNECT, endpoint'ы, кредит | да — на модели CE |
| `wcn_wmi.c` | WMI-TLV: сборка команд, разбор событий | да — на байтах |
| `wcn_htt.c` | HTT: версия, кольцо приёма, TX_FRM, IN_ORD | да |
| `wcn_mlme.c` | клиент 802.11: скан, auth, assoc, BSS | да |
| `wcn_smmu.c` | банк `apps_smmu`, маршрут SID | нет — планшет |
| `wcn_find.c` | узел, регистры, линии, MSA, iommus | стенд на DTB clover |
| `wcn.c` | служба: `netdriver` + `chardriver`, цикл, состояния | нет |
| `minix/usr.sbin/wcnd` | демон wlfw: сокет QRTR, порядок §3 | нет |

## 9. С модемом: QRTR — сокеты, wlfw — демон

Сессия модема решила интерфейс (`hw-modem.md`, §1): **QRTR — сокетное
семейство `AF_QIPCRTR` (42, как у Linux; `<netqrtr/qrtr.h>`)**, служба
`qrtr` — сокетный драйвер, как `uds`/`lwip`; модем — узел 0, свой — 1;
поиск службы — `NEW_LOOKUP` на `{1, QRTR_PORT_CTRL}`, как у Linux, ответы и
дальнейшие NEW/DEL_SERVER — на тот же сокет; DATA до 64 КБ; что модем
поднят — ключ DS `qcom_mss.up`. QMI — дело клиента (`libqrtr` linux-msm,
BSD-3, `-I/usr/include/libqrtr -lqrtr`). Hyp-assign — `QCOMSCM_ASSIGN_MEM`
(`1ea1f7732`, `sys_qcomscm_assign()`), только `no-map` из
`/reserved-memory`, отображённый вызывающим (`vm_map_phys`).

**Отсюда решение [I]: wlfw ведёт пользовательский демон `wcnd`, а не
служба.** Служба `wcn` — `chardriver` для VFS (суппликант, `wcnd`) и
`netdriver` для `lwip`; вызов сокета через VFS из неё — это служба, которую
VFS сам ждёт, ждущая VFS: блокирующий `recvfrom` поставил бы два процесса в
зависимость друг от друга, неблокирующий — опрос. Демону это не грозит, и
так же устроена Linux-сторона у Qualcomm (cnss-daemon). Разделение:

- `wcnd`: сокет QRTR, шаги 1–9 §3 (IND_REGISTER … FW_READY_IND), файлы
  прошивки с корня, затем WLAN_CFG и WLAN_MODE — таблицы для них даёт
  служба; переживает падение модема (DEL_SERVER → начать заново);
- служба `wcn`: по ioctl от `wcnd` — hyp-assign регионов MSA (права
  `QCOMSCM` у службы, не у демона), «прошивка готова — поднимай CE»;
  дальше HTC/WMI/HTT и всё остальное — её.

Кодирование wlfw — `wcn_wlfw.c` (без MINIX, общий для демона и стенда), а
не таблицы `qmi_elem_info` `libqrtr`: таблицы wlfw есть только в коде
ath10k (GPL) — переносить их «почти дословно» в дерево BSD нельзя, а свой
кодировщик уже сверен байт в байт. `libqrtr` демону — для сокета, если
пригодится.

## 10. Ступени и эталон, который ещё снять

1. **wlfw → CE → WMI READY** (готово, когда `fw_version` и build id
   совпадут с §3, а службы READY — с `wmi_services.txt`). Нужны: служба
   `qrtr` и запущенный модем (сессия модема), L5 и `rf_clk1`, банк SMMU.
2. **Скан со списком сетей** (эталон — `pass2/wifi/scan-summary.txt`: 21
   сеть, 13 на 2.4 и 8 на 5 ГГц).
3. **WPA2 и ping**: MLME, HTT данных, wpa_supplicant.

Снять на pmos до первой ступени (нужен root — пароль sudo вводит человек):
байты QMI wlfw (`debug_mask=0x00200000` у ath10k и/или kprobe на
`qrtr_endpoint_post`), `qrtr-lookup` (узел и порт wlfw), ответ на последний
кусок BDF, значение INI; что Linux ставит в `apps_smmu` для SID
0x1a00/0x1a01 (`/sys/kernel/debug/iommu`).

**Где идут прогоны (решение пользователя, 2026-10-19).** postmarketOS
остаётся на `userdata` — эталон снимается и дальше. Корень MEECHO для
прогонов модема и Wi-Fi — раздел **`system`** (`mmcblk1p13`, 3 ГБ), один на
обе работы; прошивает его сессия модема. Свои программы и службы Wi-Fi
кладутся в этот корень по ssh, а не отдельной прошивкой `system`: две
прошивки подряд — только по согласованию с сессией модема.
