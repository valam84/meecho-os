# Mi Pad 4: модем (MSS), транспорт к нему и QRTR

Начато 2026-10-19. Эталон — postmarketOS на этом же планшете
(`hw-pmos.md`, `pmos-20261019/`), исходники его ядра — ветка
`nura/sdm660-ipa` репозитория github.com/valam84/linux (6.19.10, клон в
WSL — `~/valam84-linux`, только чтение). Это GPL: отсюда берутся номера
регистров, протоколы и порядок действий, а не текст. Пользовательские
службы Linux (`rmtfs`, `tqftpserv`, `qrtr`, `pd-mapper` из linux-msm) —
BSD-3, их можно брать как код.

[C] — в коде/дереве Linux, [T] — снято с планшета, [I] — вывод.

## 1. Интерфейс QRTR для клиентов (договорённость с сессией Wi-Fi)

Решено до кода, чтобы драйвер Wi-Fi мог на него опираться.

**QRTR в MEECHO — сокетное семейство, как в Linux.** Служба `qrtr`
(сокетный драйвер, как `uds`/`lwip`: VFS отдаёт ей сокеты своего
семейства по строке `domain` в её `.conf`) держит общую память SMEM,
GLINK к модему, маршрутизатор QRTR и сервер имён (ns — как `net/qrtr/ns.c`,
внутри службы). Клиент — любой процесс, в том числе служба:

```c
#include <sys/socket.h>
#include <netqrtr/qrtr.h>	/* sockaddr_qrtr, QRTR_PORT_CTRL, qrtr_ctrl_pkt */

int s = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
```

- `AF_QIPCRTR` / `PF_QIPCRTR` — **42, как у Linux** (`AF_MAX` стал 43;
  35 у NetBSD-current уже `AF_CAN`). Только `SOCK_DGRAM`, протокол 0.
- `struct sockaddr_qrtr { uint8_t sq_len; sa_family_t sq_family;
  uint32_t sq_node; uint32_t sq_port; }` — 12 байт, поля и смысл как у
  Linux (`sq_len` по обычаю BSD, служба его не читает). Константы, `enum
  qrtr_pkt_type` и `struct qrtr_ctrl_pkt` — те же имена и значения, что в
  `<linux/qrtr.h>`.
- **Узлы:** модем — узел **0** [T] (`qrtr://0`, `node 0 port 35` у IPA);
  свой узел — **1** (как `qrtr_local_nid` у Linux), его и свой порт
  говорит `getsockname()`.
- `bind()` с `sq_port = 0` или первая отправка без `bind()` дают свободный
  порт. `sendto()`/`recvfrom()` — по датаграмме; `poll`/`select`,
  `O_NONBLOCK` работают.
- **Поиск службы — как у Linux:** послать `struct qrtr_ctrl_pkt` с `cmd =
  QRTR_TYPE_NEW_LOOKUP` (+ `server.service`, `server.instance`; 0 — любые)
  на `{node = свой, port = QRTR_PORT_CTRL}`; в ответ на тот же сокет
  приходят `QRTR_TYPE_NEW_SERVER` (по одному на службу, адрес в
  `server.node/port`), затем пустой `NEW_SERVER` (все поля 0) — «известные
  кончились»; позже — новые `NEW_SERVER`/`DEL_SERVER` по мере появления.
  Объявить свою службу — `QRTR_TYPE_NEW_SERVER` туда же.
- **QMI поверх** — дело клиента (заголовок QMI и TLV в теле датаграммы).
  Для портированных программ — библиотека `libqrtr` (linux-msm/qrtr,
  BSD-3) с её `qmi_*` и генератором `qmic`.

Что ещё нужно Wi-Fi от этой работы [C]: модем запущен (`qcom_mss`); QMI
`wlfw` — служба **69**, версия 1, на узле 0; ath10k передаёт модему
область MSA (`wlan-msa-mem@85700000`, 1 МБ) и **отдаёт её TrustZone'ом**
(`qcom_scm_assign_mem`: HLOS → MSS_MSA + WLAN + WLAN_CE) — это операция
ядра `QCOMSCM_ASSIGN_MEM` (раздел 3), она принимает только диапазоны
`no-map` из `/reserved-memory`.

## 2. Как это поднимается (план)

| Шаг | Где | Что |
|---|---|---|
| 1 | ядро | `sys_qcomscm(QCOMSCM_ASSIGN_MEM)` — SCM MP/ASSIGN (0x0c/0x16) |
| 2 | `qcom_mss` | PIL модема: такты GCC, сброс, голоса CX/MX через `qcom_rpm`, MBA → RMB → метаданные → сегменты mpss → «ready» по smp2p |
| 3 | `qrtr` | SMEM (+ hwlock TCSR), GLINK по SMEM к модему, канал `IPCRTR`, маршрутизатор и ns, сокеты |
| 4 | userland | `rmtfs` (служба QMI 14, разделы `modemst1/2`, `fsg`, `fsc` только на чтение — как `rmtfs -r` у pmos), `tqftpserv` |
| 5 | проверка | DMS `GET_REVISION` → `MPSS.AT.3.1-00729.1-…` |
| 6 | потом | IPA (`ipa2_lite`, QMI `INIT_DRIVER`), rmnet → lwip |

Состояние на 2026-10-19: шаги 1–4 написаны и собираются, **на планшете не
запускались** (корень `userdata` занят postmarketOS, см. раздел 7). На QEMU
проверен сокетный слой и сервер имён (раздел 5).

## 3. TrustZone: `QCOMSCM_ASSIGN_MEM` [C]

`qcom_scm_assign_mem()` — SCM MP (0x0c) / ASSIGN (0x16), семь аргументов:
адрес и размер (16) таблицы `{addr, size}` (64-бит), адрес и размер
списка текущих VM (32-бит номера), адрес и размер списка новых `{vmid,
perm, ctx = 0, ctx_size = 0}` (24 байта), 0. В регистрах SMCCC — первые
три (x2..x4), остальные — в буфере, на который указывает x5 (64-битные
слова; у SMC32 — 32-битные). Ответ — x0 и x1 (вердикт TrustZone).

- Таблицы строит ядро в своей странице, очищенной до памяти
  (`assign_buf` в `qcom_scm.c`); вызывающий называет память адресом в своём
  пространстве: либо внутри отображённого им `no-map` диапазона
  `/reserved-memory`, либо свою RAM одним физическим куском
  (`alloc_contig`), которую ядро чистит из кэшей.
- В сообщении новых полей нет: маска текущих VM — `svc`, новые — до
  четырёх байтов `QCOMSCM_DST(vmid, perm)` в `cmd`.
- VMID: HLOS 3, MSS_MSA 15, WLAN 0x18, WLAN_CE 0x19; права R 4, W 2, X 1.

## 4. Модем: `qcom_mss` [C]

Порядок `qcom_q6v5_mss.c` для `MSS_SDM660` (ветка `nura/sdm660-ipa` и
mainline совпадают):

1. Голоса-«прокси»: rpmpd `cx`/`mx` на высший уровень (INT_MAX → TURBO,
   384) и включение; `qcom_rpm` теперь складывает голоса служб (максимум
   уровня, «или» включения) — MX голосует и `adreno`. Такты узла: из GCC
   `iface` (64), `bus` (24), `mem` (47, BOOT_ROM, голосуемый), `snoc_axi`
   (68), `mnoc_axi` (66); `gpll0_mss` (124) **в gcc-sdm660 не
   зарегистрирован** — Linux получает пустой такт, мы тоже ничего не
   делаем; `qdss` и `xo` — такты RPM, включены и так [T: clk_summary pmos,
   `qdss_clk` «hw enable Y» при счётчике 0]. Сброс `MSS_RESTART` (BCR
   0x79000) отпускается.
2. Область mpss (126 МБ @ 0x8ac00000) и область MBA (2 МБ @ 0x94900000)
   отдаются модему (HLOS → MSS_MSA RW). `RMB_MBA_IMAGE` = адрес MBA;
   QDSP6v62: STRAP_ACC = 0x20; RESET |= CORE_ARES|BUS_ARES|STOP_CORE;
   XO_CBCR CLKEN и ожидание CLKOFF = 0; PWR_CTL |= BHS_ON, ожидание
   BHS_STATUS бит 0; LDO_BYP; снять CLAMP_QMC_MEM; L2DATA_STBY_N|SLP_RET_N;
   MEM_PWR_CTL (0x34) биты 28..0 по одному с 1 мкс; снять CLAMP_WL,
   CLAMP_IO; снять CORE_ARES; GFMUX_CTL |= CLK_ENABLE; снять STOP_CORE.
   Затем `RMB_PBL_STATUS` = 1 (до 1 с), `RMB_MBA_STATUS` = 1 или 2 (до 5 с).
3. Метаданные — заголовки ELF + хэш-сегмент (`modem.mdt` = 0x3f4 + 0x1ce8
   = 8412 байт) — в буфер нашей RAM, отданный модему; адрес в
   `RMB_PMI_META_DATA`, `RMB_MBA_COMMAND` = 1; ждать статус 3; буфер
   назад.
4. Область mpss: назад себе (ошибку можно игнорировать), затем общая
   (HLOS+MSS); сегменты (`modem.b02`…`b29`; образ перемещаемый, база
   0x8ac00000 = начало области) — после каждого `RMB_PMI_CODE_LENGTH` =
   накопленная длина, перед первым `CODE_START` = адрес области и
   `COMMAND` = 2 (LOAD_READY); отрицательный статус — отказ. Затем область
   только модему и статус 4 (AUTH_COMPLETE, до 10 с).
5. «ready» — бит 1 записи `slave-kernel` в smp2p-предмете модема (SMEM
   428); бит 2 — handover (снять голоса-прокси), бит 0 — fatal. Наш
   предмет (435, запись `master-kernel`, бит 0 — stop) создаём до старта
   модема.

Всё опросом, без прерываний. Загрузка — после SEF-инициализации (у RS на
неё 10 с), затем ключ `qcom_mss.up` в DS. `stage=probe` только читает SMEM,
такты и сброс (до включения тактов MSS его регистры не читаются вовсе),
`stage=mba` останавливается после MBA.

Образ [T, `modem.mdt`]: 30 программных заголовков, все загружаемые —
перемещаемые, с 0x8ac00000 по 0x92200000; MBA (`mba.mbn`, 238256 байт)
кладётся в свою область целиком, без разбора.

## 5. SMEM, GLINK и QRTR: служба `qrtr` [C]

- **SMEM** (`smem.c`, общий с `qcom_mss`): версия из `version[7] >> 16`
  (11 — одна глобальная куча, 12 — разделы); таблица разделов `$TOC` в
  последних 4 КиБ, раздел `$PRT` с некэшируемыми предметами снизу
  (заголовок 16 байт, канарейка 0xa5a5) и кэшируемыми сверху; выделение —
  под мьютексом TCSR №3 (`0x1f40000 + 3·0x1000`: записать 1, прочитать 1;
  0 — отпустить). Мьютекс отличает процессоры, а не наши службы: они
  выделяют только при старте, по очереди (rc).
- **GLINK по SMEM**: предмет 478 — четыре слова (наш хвост/голова, хвост/
  голова модема), 479 — наш FIFO 16 КиБ, 480 — FIFO модема; мы пишем свою
  голову и хвост модема, после записи — бит 15 регистра IPC APCS
  (`0x17911008`). Команды — как у RPM (`hw-rpm.md`), но край с модемом
  **не intentless**: данные идут только в объявленный получателем буфер.
  Мы объявляем 8 × 4 КиБ при открытии `IPCRTR` и по `RX_INTENT_REQ`;
  чужой — самый маленький подходящий, иначе `RX_INTENT_REQ` и пакет ждёт
  в очереди; по прочтении — `RX_DONE_W_REUSE` (или `RX_DONE` для
  разового). `IPCRTR` открывает модем; отвечаем `OPEN_ACK` и своим
  `OPEN`. Остальные каналы (`DATA*`, `glink_ssr`, `apr_*`) Linux без
  драйвера оставляет без ответа — мы тоже.
- **QRTR**: заголовок v1 (восемь 32-битных слов) на отправку, v1 и v2 на
  приём; узел модема — 0 (или src узла его HELLO), наш — 1; пакеты к
  управляющему порту — серверу имён внутри службы (`ns.c` Linux:
  HELLO→HELLO + наши серверы, NEW/DEL_SERVER, NEW/DEL_LOOKUP, DEL_CLIENT,
  BYE); поток — `confirm_rx` на пятом пакете, `RESUME_TX` в ответ.
- **Проверено на QEMU** (без модема): `port/test/qrtr/qemu-qrtr.py` — 15
  из 15 (сервер объявлен, найден, обмен в обе стороны, усечение, поздний
  сервер, `DEL_SERVER` при закрытии, отказы).

## 6. rmtfs и tqftpserv [C]

linux-msm `rmtfs` и `tqftpserv` (и `libqrtr` под ними) — BSD-3, лежат в
`external/bsd/` как есть, правки под `__minix`:

- общая память — служба `qcom_rmtfs_mem`: узел `qcom,rmtfs-mem`
  (0x85e00000, 2 МБ, client-id 1, vmid 15) отдаётся TrustZone'ом HLOS+15
  RW, как в `rmtfs_mem.c`; `/dev/qcom_rmtfs_mem1` (старший 135),
  `pread`/`pwrite` и ioctl `RMTFS_MEM_INFO` (адрес и размер);
- разделы — `sdmmc gptro0=modemst1,modemst2,fsg,fsc` (окна только на
  чтение, p3..p0), ссылки `/dev/modem/<имя>`; `rmtfs -r -P -o /dev/modem`
  держит записи модема в памяти — так его запускает pmos [T: `rmtfs -r -P
  -s`]; размер раздела — `DIOCGETP` (у узла устройства MINIX
  `lseek(SEEK_END)` = 0, и модем получил бы пустую ФС);
- `tqftpserv`: файлы «readonly» — из каталога прошивки модема, «readwrite»
  — `/var/db/tqftpserv`.

Проверка — `port/test/qrtr/qmidms.c` (`qmidms rev`).

## 7. Что нужно для первого прогона на планшете

- **Корень MEECHO в `userdata`.** Сейчас там postmarketOS (эталон снят и
  лежит в дереве). Прошивка модема — 100 МБ (`modem.b*` — 57 МБ) — в
  ramdisk не помещается разумно; ей место на корне, в
  `/libdata/firmware/qcom/clover/modem` (копия `modem/image/` из
  `D:\minix\clover-flash\pmos\fw\`, туда — `scp`, в дерево не кладётся).
  Перезапись `userdata` — решение пользователя (`fastboot flash`).
- **Образ загрузки из текущего дерева**: ядро с `ASSIGN_MEM`, VFS и procfs
  с новыми `PF_MAX`/`NR_DEVICES`, командная строка с
  `gptro0=modemst1,modemst2,fsg,fsc` (и без `modem=1` на первом круге —
  шаги руками по ssh).
- **Порядок первого круга**, от безопасного:
  1. `minix-service up /service/qcom_mss -args stage=probe` — читает SMEM,
     такты, сброс; ничего не пишет;
  2. `qcom_rmtfs_mem` (TrustZone отдаёт 2 МБ модему — первая проверка
     `ASSIGN_MEM` на железе);
  3. `stage=mba` — MBA и стоп (сброс модема обратно);
  4. `stage=boot`, `qrtr`, `rmtfs`, `tqftpserv`, `qmidms rev`.
- **Не проверено ничего из того, что QEMU не проверяет**: SMC ASSIGN на
  этой прошивке TrustZone, порядок питания QDSP6, кэш/атрибуты при записи
  образа (область отображена как устройство, запись словами).
