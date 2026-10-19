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
