# Справка, а не код

Здесь лежат исходники ядра Linux **той же версии, что стоит на плате**
(6.1.115 — ветка `rk-6.1-rkr5.1` у `armbian/linux-rockchip`, чей Makefile
объявляет ровно `SUBLEVEL = 115`):

- `vendor-phy-rockchip-inno-usb2.c` — драйвер USB2-PHY: настройка в `usbgrf`
  и «подстраивание» аналоговой части, которого нет в mainline;
- `vendor-core.c`, `vendor-core.h` — ядро DWC3: порядок сброса, режим порта,
  описание HS-PHY-интерфейса;
- `vendor-dwc3-of-simple.c`, `vendor-xhci-plat.c` — обвязка;
- `vendor-pm_domains.c`, `vendor-rk3568-power.h` — домен питания PD_PIPE;
- `vendor-clk.h` — смещения регистров CRU и PMUCRU;
- `vendor-rk356x.dtsi` — узлы USB целиком: что на чём висит;
- `phy-rockchip-inno-usb2.c` — та же вещь из mainline 6.1.y, для сравнения.
  Сравнение оказалось нужным: mainline не делает подстраивания вовсе.

**Они под GPL, а дерево MEECHO под BSD-3-Clause.** Отсюда берутся номера
регистров, разрядность полей и порядок действий — то есть факты о железе,
которые не являются ничьим выражением. Текст, структура и код не переносятся
ни в каком виде.

Выжимка, по которой написан драйвер, — в `../registers.md`: своими словами и
с посчитанной для этой платы арифметикой. Числа там сверены не с этими
файлами, а со снимками самой платы (`../reference*.txt`).

Как взять заново (с виндовой стороны — из WSL `raw.githubusercontent.com`
режется):

    https://raw.githubusercontent.com/armbian/linux-rockchip/rk-6.1-rkr5.1/<путь>
