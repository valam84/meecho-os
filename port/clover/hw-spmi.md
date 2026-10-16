# SDM660: арбитр SPMI PMIC — спецификация для своего клиента

Снято 2026-10-16 из `drivers/spmi/spmi-pmic-arb.c` (sdm660-mainline,
`72bc4bb`) и `sdm630.dtsi:1310-1326` — факты, не код (Linux под GPL).
[C] — сказано в коде или дереве, [I] — вывод.

## Дерево [C]

`qcom,spmi-pmic-arb` (одна шина, «legacy»), `qcom,ee = 0`:

| регион | адрес | размер |
|---|---|---|
| core | 0x0800f000 | 0x1000 |
| chnls | 0x08400000 | 0x1000000 |
| obsrvr | 0x09400000 | 0x1000000 |
| intr | 0x0a400000 | 0x220000 |
| cnfg | 0x0800a000 | 0x3000 |

PMIC: PM660 — USID 0 (и 1), PM660L — USID 2 и 3.

## Версия

`core + 0` — PMIC_ARB_VERSION [C]. Пороги: < 0x20010000 v1, < 0x30000000
v2, < 0x50000000 v3, < 0x70000000 v5… [C]. У v2 и v3 чтение и запись
одинаковы [C]. SDM660 — почти наверняка v3 [I] (16-МиБ окна = шаг 0x8000 ×
512 APID, `intr` 0x220000 под блок v3 на +0x200000). **Клиент отказывает
вне 0x20010000 ≤ ver < 0x50000000.**

## PPID → APID (v2/v3) [C]

- PPID = (SID << 8) | ((addr >> 8) & 0xff), 12 бит.
- Таблица `core + 0x800 + 4*n`, n = 0..511 (до конца `core`); 0 — пусто;
  PPID = (v >> 8) & 0xfff; n — это APID. Один APID на PPID.
- Владелец: `cnfg + 0x700 + 4*n`, EE = v & 7. В v2/v3 софт не проверяет,
  чужой канал отвергает железо (STATUS bit2). **Клиент пишет только в
  каналы, где владелец == 0** [I, как правило v5].

## Чтение (obsrvr) [C]

Канал: `obsrvr + 0x1000*ee + 0x8000*apid`. Регистры: CMD 0x00, CONFIG 0x04,
STATUS 0x08, WDATA0 0x10, WDATA1 0x14, RDATA0 0x18, RDATA1 0x1c.
CMD = (opc << 27) | ((addr & 0xff) << 4) | (len - 1), не больше 8 байт.
Опкоды: EXT_WRITEL 0, EXT_READL 1, EXT_WRITE 2, EXT_READ 13, WRITE 14,
READ 15, ZERO_WRITE 16. Берём EXT_READL/EXT_WRITEL [I].
RDATA — little-endian, байт i = (RDATA0 >> 8i) & 0xff.

STATUS: bit0 done (ждать), затем bit1 FAILED → EIO, bit2 DENIED → EPERM,
bit3 DROPPED → EIO, иначе успех. Срок — 1000 опросов по 1 мкс.
Только 32-битные доступы.

## Запись (chnls) [C]

Тот же сдвиг канала в `chnls`. WDATA0 (и WDATA1 при len > 4), затем CMD —
запись CMD запускает транзакцию; тот же STATUS и срок.

## Опасное

Пробник ничего не пишет [C]. Не трогать `intr` (разрешения/сброс
прерываний), `core`, `cnfg`; только ee = 0. Запись в регистры самих PMIC —
по одному известному регистру за раз: PON (PM660 0x800) держит настройки
сброса/PS_HOLD, регуляторы могут снять питание [I].

## Минимальные последовательности

```
find_apid(sid, a): ppid = sid<<8 | (a>>8)&0xff;
  for n in 0..511: v = rd32(core+0x800+4n); if v && (v>>8)&0xfff == ppid: return n
read1(sid, a):  ch = obsrvr + 0x8000*apid
  wr32(ch, 1<<27 | (a&0xff)<<4); wait(ch+8); return rd32(ch+0x18) & 0xff
write1(sid, a, v): if (rd32(cnfg+0x700+4*apid) & 7) != 0: EPERM
  ch = chnls + 0x8000*apid; wr32(ch+0x10, v); wr32(ch, (a&0xff)<<4); wait(ch+8)
```

Безвредная первая проверка: REVID PM660 (SID 0, 0x0100..0x0105) [I].
