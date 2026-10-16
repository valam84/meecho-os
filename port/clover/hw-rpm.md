# SDM660 Mi Pad 4: регуляторы RPM через GLINK

Снято 2026-10-16 из sdm660-mainline (`drivers/rpmsg/qcom_glink_rpm.c`,
`qcom_glink_native.c`, `drivers/soc/qcom/smd-rpm.c`,
`drivers/regulator/qcom_smd-regulator.c`, `qcom_spmi-regulator.c`,
`drivers/mailbox/qcom-apcs-ipc-mailbox.c`, регуляторное ядро) и дерева
clover. Факты, не код. [C] — в коде/дереве, [I] — вывод, [T] — снято с
планшета.

## Зачем [T]

Сенсор FocalTech на `i2c@c177000` @ 0x38 молчит (NACK на всех адресах
шины), хотя QUP, такты, выводы и сброс — как в Linux, а LineageOS на том же
планшете работает с тем же чипом (`fts` на gpio67, 3721 прерывание; QUP3
~30 000). SDA/SCL высокие, сброс загрузчик держит в нуле. Питание сенсора
`vcc-supply = <&vreg_l11a_1p8>` — регулятор RPM, а не PMIC напрямую.

## Регуляторы [C]

- В дереве: `rpm-requests` → `regulators-0` (`qcom,rpm-pm660l-regulators`)
  и `regulators-1` (`qcom,rpm-pm660-regulators`); дети по имени (`l11`).
- Таблица Linux (`rpm_pm660_regulators`): s1–s6 → `smpa` 1–6, l1–l19 →
  `ldoa` n (L4 нет). PM660L (`rpm_pm660l_regulators`): s1, s2 → `smpb`;
  s3 → `rwcx` 0; s5 → `rwmx` 0; l1–l8 → `ldob` n; l9 → `rwlc` 0; l10 →
  `rwlm` 0; bob → `bobb` 1.
- Регуляторное ядро при регистрации включает каждый с
  `regulator-always-on` или `regulator-boot-on`; если min == max, сначала
  ставит напряжение (apply_uV). `rpm_reg_write_active()` шлёт `swen` = 1 и,
  если напряжение задано, `uv`; `ma` — только при set_load.
- На clover это l3b (3.0 В, min = max → `uv` 3000000), s4a, l11a (сенсор),
  l13a (boot-on). l1a — `always-on` закомментирован.
- SPMI-вид тех же регуляторов: PM660 — SID 1 (`pmic@1`), LDO n по
  0x4000 + (n − 1)·0x100, TYPE (0x04) = 0x04 (`SPMI_REGULATOR_TYPE_LDO`;
  0x21 — ULT_LDO), включён — ENABLE (0x46) бит 7. Писать их приложение не
  может: владелец — RPM.

## Транспорт: GLINK RPM [C]

- `rpm-glink` → `qcom,rpm-msg-ram` = `sram@778000`, 0x7000 байт;
  `mboxes = <&apcs_glb 0>`; прерывание SPI 168 (здесь не нужно — опрос).
- Оглавление — последние 256 байт RAM: `magic` 0x67727430 ("grt0"),
  `count`, записи `{id, offset, size}`. `ap2r` (0x61703272) — наше на
  передачу, `r2ap` (0x72326170) — на приём. У каждого: `offset` — tail,
  `offset + 4` — head, `offset + 8` — кольцо длиной `size`.
- Только 32-битные обращения (`__ioread32_copy`). Мы пишем tx head и rx
  tail, RPM — tx tail и rx head. При старте Linux пишет tx head = 0 и rx
  tail = 0.
- Запись: заголовок (кратен 4), данные, дополнение до 8 байт.
- Звонок RPM: APCS (`qcom,sdm660-apcs-hmss-global`, 0x17911000), регистр
  IPC по смещению 8 (`msm8994_apcs_data`), записать `BIT(0)`.

## GLINK [C]

- Заголовок 8 байт: `cmd` (16), `param1` (16), `param2` (32).
- Команды: VERSION 0, VERSION_ACK 1, OPEN 2, CLOSE 3, OPEN_ACK 4,
  INTENT 5, RX_DONE 6, RX_INTENT_REQ 7, RX_INTENT_REQ_ACK 8, TX_DATA 9,
  CLOSE_ACK 11, TX_DATA_CONT 12, READ_NOTIF 13, RX_DONE_W_REUSE 14,
  SIGNALS 15.
- Старт: VERSION(1, features 0). На VERSION удалённого — VERSION_ACK(1,
  features &= его); на VERSION_ACK с другими features — снова VERSION.
- Канал `rpm_requests` открывает RPM: OPEN(rcid, длина имени | prio << 16)
  + имя, выровненное до 8. Linux (`qcom_glink_create_remote`): OPEN_ACK
  (rcid), затем OPEN(lcid = 1, длина 13) + "rpm_requests" (всего 24 байта),
  ждёт OPEN_ACK(lcid). Обратный порядок (`_create_local`): OPEN, ждать
  OPEN_ACK, ждать OPEN удалённого, ответить OPEN_ACK.
- RPM без intents: TX_DATA {cmd 9, lcid, iid 0, chunk, left} + данные;
  RX_DONE не шлётся. Приём TX_DATA: заголовок 16 байт, сдвиг хвоста —
  ALIGN(16 + chunk, 8).
- Сдвиги хвоста при приёме: 8 для VERSION/ACK, OPEN_ACK, CLOSE(_ACK),
  SIGNALS, READ_NOTIF (и звонок в ответ), RX_DONE; OPEN — 8 + ALIGN(len, 8);
  INTENT — ALIGN(8 + 8·count, 8).

## Запрос RPM [C]

- `req` (0x00716572), длина = 20 + данные; затем `msg_id` (с 1),
  `flags` = 0 (active set), `type`, `id`, `data_len`; данные — тройки
  {ключ, 4, значение}. Ключи: `swen` 0x6e657773, `uv` 0x00007675,
  `ma` 0x0000616d. Пакет < 256 байт.
- Ответ: `req` + длина; сообщения {тип, длина, данные}, шаг
  8 + ALIGN(длина, 4): `msg#` (0x2367736d) — id; `err` (0x00727265) —
  текст; "resource does not exist" → ENXIO, иное → EINVAL.

## Порядок на планшете [I]

`qcom_pmic` (печатает LDO PM660 как найдены) → `qcom_rpm` (включает l3b,
s4a, l11a, l13a) → `qcom_touch` (сброс, ожидание до 2 с) → через 10 с
`qcom_pmic` печатает LDO снова.

## Что показал планшет [T]

После `qcom_rpm` сенсор ответил с первой попытки (`chip id 0x3b`). Но
L11A **был включён и до него**: `PM660 LDOs on: 1 3 6 8 9 10 11 13 14 19`.
Сенсору не хватало другого регулятора из четырёх — по всей видимости, L3B
(3.0 В, PM660L): у FocalTech два питания, аналоговое и I/O, а дерево
mainline называет только `vcc` (1.8 В) и держит L3B включённым без
указания потребителя. Регуляторы PM660L этот снимок не читал (SID 3), так
что «L3B» — вывод, а не измерение.
