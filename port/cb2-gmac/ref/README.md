# Справка, а не код — и поэтому её здесь нет

Драйвер `dwmac` писался не из первых принципов, а по работающей на этой
плате системе. Эталоном служили исходники двух ядер Linux и двух чужих
драйверов Ethernet. Из них брались **номера регистров, разрядность полей и
порядок действий** — то есть факты о железе, которые не являются ничьим
выражением. Текст, структура и код не переносились ни в каком виде.

**Сами файлы в репозиторий не входят: они под GPL, а это дерево под
BSD-3-Clause.** Смешивать их в одном дереве нельзя, а ссылаться на них —
можно и нужно: без них ни одну цифру в драйвере проверить невозможно.

Выжимка, по которой драйвер написан, лежит рядом в
[`../registers.md`](../registers.md) — в своих словах и с посчитанной для
этой платы арифметикой. Снимок регистров живой платы — в
[`../linux-reference.txt`](../linux-reference.txt), снимок регистров PHY — в
[`../phy-reference.txt`](../phy-reference.txt). Этого достаточно, чтобы
читать драйвер; исходники ниже нужны только тому, кто хочет перепроверить
вывод.

## Что откуда брать

**Mainline Linux 6.1.115** (`git.kernel.org`, ветка `linux-6.1.y`) —
обвязка Rockchip, мультиплексор выводов, тактовое дерево:

| Файл | Путь в дереве Linux |
|---|---|
| `dwmac-rk.c` | `drivers/net/ethernet/stmicro/stmmac/dwmac-rk.c` |
| `dwmac4*.c`, `dwmac4*.h` | `drivers/net/ethernet/stmicro/stmmac/` |
| `stmmac_mdio.c`, `hwif.c` | `drivers/net/ethernet/stmicro/stmmac/` |
| `pinctrl-rockchip.c`, `.h` | `drivers/pinctrl/pinctrl-rockchip.*` |
| `gpio-rockchip.c` | `drivers/gpio/gpio-rockchip.c` |
| `clk-rk3568.c` | `drivers/clk/rockchip/clk-rk3568.c` |
| `rk3568-cru.h` | `include/dt-bindings/clock/rk3568-cru.h` |
| `motorcomm.c` | `drivers/net/phy/motorcomm.c` |
| `mmc_core.c`, `mmc.h` | `drivers/net/ethernet/stmicro/stmmac/` |

**Вендорское ядро BTT 6.1.115-btt-rockchip64** — то, что реально стоит на
плате, и по нему настраивается PHY. Файлы с приставкой `vendor-` брались
оттуда: `dwmac-rk.c` (и его ветка `-rev2`, которая и работает на плате),
`motorcomm.c`, `clk-rk3568.c`, `rockchip-otp.c`, `rk_vendor_storage.c`,
`flash_vendor_storage.c`, `jhash.h`, а также device tree платы
`rk3566-bigtreetech-cb2.dts`.

> **Это различие — не мелочь.** Mainline для `rgmii` выключает задержки PHY;
> вендорский `yt8531_config_init` их не трогает и пишет аналоговую часть.
> Драйвер, написанный по mainline, поднимает линк и не носит пакеты. Разбор
> — `../../PORTING-LOG.md`, «Сеть на плате работает».

**NetBSD** — `sys/dev/ic/dwc_eqos.c`, `dwc_eqos_reg.h`, `dwc_eqos_var.h`
(BSD-2). Здесь звались `eqos*.c`. Полезны как второй независимый читатель
той же спецификации.

**U-Boot** — `drivers/net/dwc_eth_qos.c` (здесь `ub_eqos.c`): порядок
инициализации без операционной системы вокруг.

## Как разложить их у себя

```sh
git clone --depth 1 -b linux-6.1.y \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git ~/linux-6.1
git clone --depth 1 -b rk-6.1-rkr5 \
    https://github.com/bigtreetech/CB2 ~/btt-cb2      # вендорское ядро
```

Дальше — копировать в этот каталог по таблице выше. Ничего из
скопированного коммитить в это дерево **нельзя**.
