# Ускоритель CB2: эталон и как его снимать

Mali-G52 (Bifrost) платы BIGTREETECH CB2, узел `gpu@fde60000`. Всё здесь
снимается с вендорского Linux на плате (6.1.115-btt-rockchip64) **до**
того, как служба `minix/drivers/video/mali/` трогает железо.

| Файл | Что |
|---|---|
| `capture.sh` | снимок: какой драйвер владеет узлом (kbase или panfrost), его отчёт, узел дерева, такты, домен питания, `vdd_gpu`, прерывания; CRU, PMU и регистры ускорителя через `/dev/mem` |
| `ref-idle.txt` | снимок 2026-10-13 как есть: panfrost в простое, домен выключен — регистры ускорителя скрипт не читал |
| `ref-on.txt` | снимок 2026-10-13 с `power/control = on`: домен включён, panfrost на 200 МГц, все регистры ускорителя — **эталон** |

Снимать (плата под Armbian, по ssh, под root):

    sh capture.sh    > idle.txt    как есть
    sh capture.sh on > on.txt      драйвер держит блок включённым
                                   (power_policy always_on у kbase или
                                   power/control on у panfrost), потом
                                   прежнее значение возвращается

Регистры ускорителя скрипт читает, только если PMU говорит, что домен
PD_GPU включён и не в простое, а CRU — что вентили `clk_gpu_src` и
`clk_gpu` открыты и сбросы отпущены: чтение обесточенного или
незатактированного блока на Rockchip вешает шину.

## Что ответила плата (2026-10-13)

**Драйвер — `panfrost`** (модулем; `CONFIG_MALI_BIFROST is not set`, kbase в
этом ядре нет). `dmesg`: `mali-g52 id 0x7402 major 0x1 minor 0x0 status
0x0`, `shader_present=0x1 l2_present=0x1`. Прерывания: GIC 71
`panfrost-gpu`, 73 `panfrost-mmu`, 72 `panfrost-job`.

Регистры ускорителя при включённом блоке (`ref-on.txt`):

| Регистр | Значение | Что значит |
|---|---|---|
| `GPU_ID` | `0x74021000` | Mali-G52 (0x7402), r1p0 |
| `L2/CORE/TILER/MEM_FEATURES` | `0x07110206`/`0x2`/`0x209`/`0x1` | |
| `MMU_FEATURES` | `0x2823` | 35 бит VA, 40 бит PA |
| `AS_PRESENT` / `JS_PRESENT` | `0xff` / `0x7` | 8 адресных пространств, 3 слота заданий |
| `SHADER/TILER/L2/STACK_PRESENT` | `1`/`1`/`1`/`1` | **одно** шейдерное ядро (два исполнительных блока) |
| `THREAD_MAX_THREADS` | `0x300` | 768 |
| `COHERENCY_FEATURES` | `0` | кэши процессора ускоритель не видит — обслуживать их явно |
| `JM_CONFIG` | `0x000f0000` | запись panfrost: размер группы IDVS (особенность G52) |
| `SHADER/TILER/L2_MMU_CONFIG` | `0` | |

Обвязка:

| Регистр | В простое (600 МГц) | За работой (200 МГц) |
|---|---|---|
| PMU `PWR_STATUS` | `0x7f` — PD_GPU выключен | `0x7e` — включён |
| PMU `IDLE_ACK`/`IDLE_ST` | `0x1ee` — шина в простое | `0x1ec` |
| CRU `CLKSEL_CON6` | `0x4951`: gpll/2 = 594, **ядро на PVTPLL** | `0x4155`: **gpll/6 = 198, путь PLL**, aclk /2, pclk /5 |
| CRU `CLKGATE_CON2` | `0x1c0` | `0x1c0` — `clk_gpu_src` и `clk_gpu` открыты |
| CRU `SOFTRST_CON2` | `0` | `0` |
| `vdd_gpu` | 900 мВ, включён | то же |

`CLKSEL_CON6` ставит прошивка по запросу SCMI: на 200 МГц она сама уводит
ядро на путь PLL, PVTPLL — только на высоких частотах. **`clk_summary`
Linux про эти такты неверен** (показывает cpll/500 МГц): мультиплексоры
для Linux только для чтения, и он помнит не то, что в регистре.

Отсюда служба: такт она ставит в слово `0x4155` (делитель считается от
gpll, сравнивается с эталоном) — самая медленная точка таблицы, 0.85 В, а
`vdd_gpu` грузится с 0.9; после подъёма ядер печатает свои регистры
рядом с эталоном, помечая расхождения `DIFFERS`.

## Из дерева

| Что | Значение |
|---|---|
| `compatible` | `arm,mali-bifrost` (только он; mainline пишет перед ним `rockchip,rk3568-mali`) |
| `reg` | `0xfde60000`, 16 КиБ |
| прерывания | SPI 39 `gpu`, SPI 41 `mmu`, SPI 40 `job`, и те же три ещё раз заглавными — для kbase |
| такты | `<scmi_clk 1>` «gpu» — такт прошивки (SCMI через `smc`); `<cru 27>` «bus» — `CLK_GPU`, вентиль `CLKGATE_CON(2)` бит 3 |
| домен | `<power 7>` — `RK3568_PD_GPU`: бит 0 в `PWR_CON`/`PWR_STATUS`, бит 1 в `IDLE_REQ`/`IDLE_ACK`/`IDLE_ST` |
| питание | `mali-supply` → `vdd_gpu` = `DCDC_REG2` PMIC, `always-on`, `boot-on` — **драйвер PMIC для 11.5 не нужен** |
| частоты | `opp-table-1`: 200/300/400 МГц при 0.85 В, 600 — 0.9, 700 — 0.95, 800 — 1.0 (выше загрузочной — веха 11.8) |

Справка о регистрах — `panfrost_regs.h` и regmap kbase вендорского ядра
(`armbian/linux-rockchip`, ветка `rk-6.1-rkr5.1`), `pm_domains.c`,
`clk-rk3568.c`; под GPL, в дерево не кладутся — берутся номера и порядок
действий.
