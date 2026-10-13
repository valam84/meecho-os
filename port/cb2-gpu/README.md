# Ускоритель CB2: эталон и как его снимать

Mali-G52 (Bifrost) платы BIGTREETECH CB2, узел `gpu@fde60000`. Всё здесь
снимается с вендорского Linux на плате (6.1.115-btt-rockchip64) **до**
того, как служба `minix/drivers/video/mali/` трогает железо.

| Файл | Что |
|---|---|
| `capture.sh` | снимок: какой драйвер владеет узлом (kbase или panfrost), его отчёт, узел дерева, такты, домен питания, `vdd_gpu`, прерывания; CRU, PMU и регистры ускорителя через `/dev/mem` |

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

## Что известно до снимка

Из дерева работающей системы (`../cb2-display/live.dtb`, вендорское дерево
с оверлеем `hdmi`, который включает и `gpu`):

| Что | Значение |
|---|---|
| `compatible` | `arm,mali-bifrost` (только он; mainline пишет перед ним `rockchip,rk3568-mali`) |
| `reg` | `0xfde60000`, 16 КиБ |
| прерывания | SPI 39 `gpu`, SPI 41 `mmu`, SPI 40 `job` (линии GIC 71, 73, 72), и те же три ещё раз заглавными — для kbase |
| такты | `<scmi_clk 1>` «gpu» — такт прошивки (SCMI через `smc`, его частоту ставит BL31); `<cru 27>` «bus» — `CLK_GPU`, вентиль `CLKGATE_CON(2)` бит 3 |
| домен | `<power 7>` — `RK3568_PD_GPU`: бит 0 в `PWR_CON`/`PWR_STATUS` PMU, бит 1 в `IDLE_REQ`/`IDLE_ACK`/`IDLE_ST`; такты домена — `aclk_gpu_pre`, `pclk_gpu_pre` (делители от `clk_gpu_src`) |
| питание | `mali-supply` → `vdd_gpu` = `DCDC_REG2` PMIC, `regulator-always-on`, `regulator-boot-on`, 0.9 В при загрузке — **драйвер PMIC для 11.5 не нужен** |
| частоты | `operating-points-v2` → `opp-table-1` (частота выше загрузочной — веха 11.8) |

Источник такта — `CLKSEL_CON(6)`: мультиплексор 7:6 (mpll, gpll, cpll,
npll), делитель 3:0, бит 11 — `clk_gpu_src` или PVTPLL ускорителя. Шина
регистров ускорителя (`pclk_gpu_pre`) идёт от того же мультиплексора.

Из снимка экрана (`../cb2-display/ref-720p-forced.txt`, 2026-10-12, Linux
в простое — CRU и PMU там сняты целиком):

| Регистр | Значение | Что значит |
|---|---|---|
| PMU `PWR_STATUS` 0x98 | `0x7f` | PD_GPU (бит 0) **выключен** — kbase гасит домен в простое |
| PMU `IDLE_ST` 0x68 / `IDLE_ACK` 0x60 | `0x1ee` | шина GPU (бит 1) в простое |
| CRU `CLKSEL_CON6` 0x118 | `0x4951` | gpll/2 = 594 МГц, aclk /2, pclk /5, **ядро на PVTPLL** (бит 11) |
| CRU GPLL 0x40/0x44 | `0x2063`/`0x1441` | 24 × 99 / 1 / 2 / 1 = 1188 МГц |
| CRU `CLKGATE_CON2` 0x308 | `0x1c0` | `clk_gpu_src` и `clk_gpu` открыты (закрыты только PVTM) |
| CRU `SOFTRST_CON2` 0x408 | `0` | сбросы отпущены |

PVTPLL отсюда не проверить, поэтому служба, найдя его под ядром,
переводит мультиплексор на путь PLL с делителем не выше 400 МГц
(gpll/3 = 396) — эта точка в таблице дерева разрешена при 0.85 В, а
`vdd_gpu` грузится с 0.9. Разбор — `../PORTING-LOG.md`, «Этап 11.5,
первая половина».

Справка о регистрах — `panfrost_regs.h` и regmap kbase вендорского ядра
(`armbian/linux-rockchip`, ветка `rk-6.1-rkr5.1`), `pm_domains.c`,
`clk-rk3568.c`; под GPL, в дерево не кладутся — берутся номера и порядок
действий.
