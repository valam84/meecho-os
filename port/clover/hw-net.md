# SDM660 Mi Pad 4: сеть до планшета — варианты, факты, план

Снято 2026-10-16. Источники: sdm660-mainline (`72bc4bb`) —
`drivers/usb/dwc3/{core.c,core.h,gadget.c,ep0.c,dwc3-qcom.c,dwc3-qcom-legacy.c}`,
`drivers/phy/qualcomm/phy-qcom-qusb2.c`, `drivers/clk/qcom/{gcc-sdm660.c,gdsc.c}`,
`drivers/power/supply/qcom_smbx.c`, `drivers/regulator/qcom_usb_vbus-regulator.c`,
`drivers/power/reset/qcom-pon.c`, `drivers/crypto/qcom-rng.c`,
`drivers/usb/gadget/function/f_ecm.c`, `drivers/net/usb/cdc_ether.c`,
`drivers/usb/core/hub.c`, `drivers/remoteproc/qcom_q6v5_mss.c`,
`drivers/net/wireless/ath/ath10k/{snoc.c,qmi.c}`; деревья `sdm630.dtsi`,
`pm660.dtsi`, `sdm660-xiaomi-clover-common.dtsi` и собранное
`sdm660-xiaomi-clover.dtb`; хаб — только чтение по ssh. Факты, не код.
[C] — в коде/дереве, [I] — вывод, [T] — снято с устройства.

## Итог

**Рекомендация — USB-C планшета в режиме устройства, функция CDC-ECM,
кабель к хабу.** На хабе появляется `usb0`, на планшете — сетевой
драйвер для `lwip`, дальше всё как на CB2: `sshd`, `ssh` через
`ProxyJump cb2-hub`. Решают три вещи:

- **Тот же кабель — это и fastboot.** ABL отвечает хабу по нему же
  (fastboot с хаба уже работал, `reference.md`). MEECHO умеет попросить
  перезагрузку в загрузчик (регистр PON, §5), значит круг «собрал →
  `fastboot boot` с хаба → ssh» замыкается без человека и без фото.
- **Ни одной записи в PMIC, кроме причины перезагрузки, и ни одной в
  eMMC.** Худший исход нового драйвера — «хаб не видит устройства».
- **Mainline другого не умеет**: во всех 16 деревьях телефонов и
  планшетов SDM630–SDM670, где роль задана, — `dr_mode = "peripheral"`
  (`host` — только у отладочной платы `sda660-inforce-ifc6560`), драйвера
  VBUS/OTG для PM660 нет. Хост (клавиатура, LAN-адаптер) — вторым шагом:
  обвязка и PHY из A переходят к нему целиком (§2, «Хост после A»).

Wi-Fi — месяцы работы и закрытые прошивки (§3); не предлагаю.

## 1. Что есть на этом железе

### Порт USB [C]

- Один USB-C. Контроллер — `usb@a8f8800` (`qcom,sdm660-dwc3`,
  `qcom,dwc3`; это QSCRATCH, 0x400) с дочерним `usb@a800000`
  (`snps,dwc3`, 0xc8d0). У clover: **`dr_mode = "peripheral"`,
  `maximum-speed = "high-speed"`**, `qcom,select-utmi-as-pipe-clk`,
  `phys = <&qusb2phy0>` (только USB 2.0). QMP USB3 PHY `phy@c010000` —
  `disabled`. Второй контроллер `usb@c2f8800` — `disabled`.
- Прерывания: ядро DWC3 — SPI 131. У обвязки — `pwr_event` SPI 180,
  `qusb2_phy` SPI 347, `hs_phy_irq` SPI 133, `ss_phy_irq` SPI 243 (для
  сна и пробуждения; первому кругу не нужны [I]).
- **`iommus` и `dma-coherent` у `usb@a800000` нет**: DMA по физическим
  адресам, кэш обслуживать явно (`sys_cachectl`, как у `dwmac`). ОЗУ
  кончается на `0x100000000` [T, фото загрузки] — хватает 32 бит.
- Драйвер дерева — `dwc3-qcom-legacy.c` (`"qcom,dwc3"`), QSCRATCH = `reg`
  обвязки.

### Хаб [T, 2026-10-16]

- Armbian 26.02 (Ubuntu 24.04), ядро 6.18.9. Модули есть: `cdc_ether`,
  `cdc_ncm`, `cdc_eem`, `cdc_subset`, `rndis_host`, `ax88179_178a`,
  `r8152`, `usbnet`.
- Одна шина USB 2.0, сейчас пустая. `fastboot` и `adb` стоят.
- NetworkManager активен, **`dnsmasq` нет** — режим `shared` (DHCP-сервер)
  не поднимется; адреса задавать статически.
- Подсети: `eth0` 192.168.33.2/24, `tun0` 172.19.0.1/30 — **10.42.0.0/24
  свободна**.
- quirk `18d1:d00d:k,18d1:4e11:k` (без LPM для ABL) стоял только до
  перезагрузки хаба. С 2026-10-16 — в командной строке ядра хаба
  (`usbcore.quirks=` в `APPEND` `/boot/uEnv.txt`; usbcore встроен, так что
  `modprobe.d` не годится); копия прежнего файла —
  `/boot/uEnv.txt.bak-meecho-20261016`. Перезагрузкой хаба не проверено.

## 2. Варианты

| Вариант | Чего нет в MEECHO | Что уже есть | Риск | Итог |
|---|---|---|---|---|
| **A. Устройство + CDC-ECM** | обвязка Qualcomm (GDSC, такты, QSCRATCH), PHY QUSB2, DWC3 в режиме устройства, функция ECM как сетевой драйвер, источник случайности SoC | `lwip`, `sshd`, сетевые драйверы на `libnetdriver` (`dwmac`), `qcom_soc` (GCC), `qcom_rpm` | низкий: PMIC и eMMC не трогаются | **рекомендую** |
| B. Хост OTG + USB-Ethernet | та же обвязка и PHY; клей `xhci` для Qualcomm; буст OTG и Type-C у PM660; драйвер класса USB-Ethernet + мост к `lwip` | `xhci` (DWC3 в хосте, CB2), `usb_hub`, `usb_kbd` | средний: запись в зарядник PM660 без эталона в mainline | **вторым, после A** (§2, «Хост после A») |
| C. Wi-Fi WCN3990 | загрузчик модема, QRTR/QMI, SMMU, ath10k, 802.11, WPA | — | высокий + закрытые прошивки | нет |
| D. UART на тестовых точках | вскрыть корпус, припаяться | драйвер UARTDM не написан | вскрытие планшета | только консоль, не сеть |

### B подробнее [C]

- Буст: `OTG_CFG` = заряд. база 0x1000 + 0x153 (`qcom_smbx.c`),
  `OTG_EN_SRC_CFG` (bit 1) = 0 — программное управление. Включает
  `CMD_OTG` bit 0; у `qcom_usb_vbus-regulator.c` это база OTG + 0x40,
  то есть **0x1140 у PM660 — вывод по той же раскладке [I]**: драйвер
  знает только `qcom,pm8150b-vbus-reg`.
- Роль Type-C: `qcom_smbx.c` про это — `FIXME: This will be handled by
  the type-c driver`; драйвера нет. Эталон был бы только в downstream
  4.19 LineageOS, а её на планшете больше нет (userdata стёрт).
- Кабель занят адаптером — fastboot с хаба по нему невозможен; адаптер
  питается от аккумулятора, зарядки нет.
- Плюс B — USB-клавиатура и флешки на планшете. Это отдельная цель, и
  обвязка с PHY из варианта A к ней переходит целиком.

### Хост после A [C/I]

Клавиатура и LAN-адаптер на планшете возможны; это не «нет», а «не
первым». Из чего состоит:

- **Общее с A** — GDSC, такты, BCR, QSCRATCH, PHY QUSB2: те же регистры.
  В режиме хоста обвязка снимает подмену VBUS
  (`dwc3_qcom_vbus_override_enable(false)`) [C].
- **Контроллер** — ядро DWC3 в роли хоста (GCTL PRTCAPDIR = 1) с xHCI
  внутри, как на CB2: `xhci` есть, нужен файл обвязки Qualcomm рядом с
  `xhci_rk.c` [I].
- **Клавиатура** — после этого ничего нового: `usb_hub`, `usb_kbd`,
  `input` работают с `xhci` на CB2 [T, CB2].
- **LAN-адаптер** — драйвер класса CDC-ECM хоста есть с 2026-10-17:
  `usb_ecm` (`cdce0`), написан для CB2 и от Qualcomm не зависит; `xhci`
  выбирает конфигурацию ECM у RTL8153 (на хабе для неё модуль
  `r8153_ecm` [T]). Проверен стендами, на плате ещё нет (`PORTING-LOG.md`,
  «USB CDC Ethernet на хосте»). AX88179 — свой протокол.
- **VBUS 5 В** — буст PM660: `CMD_OTG` 0x1140 bit 0 [I по раскладке
  pm8150b], `OTG_CFG` 0x1153 bit 1 = 0 [C]. Включать только при
  подключённом приёмнике и без зарядника на входе.
- **Роль Type-C** — CC разбирает железо PM660; состояния — в
  `TYPE_C_STATUS_1..5` (0x130b–0x130f), «подключён приёмник» — поле DFP
  `TYPE_C_STATUS_2` (`DFP_RD_OPEN` bit 3 и соседние), VBUS — `STATUS_4`
  bit 6 [C: имена в `qcom_smbx.c`; смысл полей — I]. Mainline их для
  выбора роли не использует. Эталон — downstream-ядро LineageOS
  (`qpnp-smb2`), исходники открыты; живые регистры можно попробовать снять
  из recovery LineageOS (то же ядро) с адаптером в порту — если adb там
  от root, проверить.
- **Физически** — порт один: либо хост, либо устройство. Переходник
  USB-C → USB-A, устройства питаются от аккумулятора, зарядки нет. Пока
  планшет хост, fastboot и ECM с хабом недоступны — круг идёт через сам
  LAN-адаптер, а журнал прогона без сети переживает сброс на корне eMMC
  и читается по ssh после возврата к образу с ECM.

### C подробнее [C]

`wifi@18800000` (`qcom,wcn3990-wifi`) `okay`, но прошивка WLAN живёт на
модеме: ath10k ищет службу QMI `WLFW` (`qmi.c`), а модем поднимает
`remoteproc@4080000` (`qcom,sdm660-mss-pil`, `mba.mbn` + `modem.mdt`,
`need_mem_protection` — вызовы TrustZone). Дальше QRTR поверх GLINK к
модему, SMMU (`iommus = 0x1a00, 0x1a01`), copy engines/HTT/WMI ath10k
(snoc.c + qmi.c — 3 тыс. строк, ядро ath10k — в разы больше), 802.11 и
WPA2 — ничего этого у MEECHO нет, прошивки — проприетарные из `vendor`.

### Прочее [I]

Bluetooth той же WCN3990 (PAN) — стек BT с нуля. Модем LTE — тот же
загрузчик модема плюс QMI. Последовательный гаджет (CDC-ACM) + PPP — та
же работа над DWC3 и ещё PPP; ACM имеет смысл позже как консоль рядом с
ECM, не вместо.

## 3. Факты для варианта A

### GCC: такты, GDSC, сбросы [C]

| Что | Регистр | Как |
|---|---|---|
| GDSC `USB_30` | 0xf004 | `VOTABLE`; снять SW_COLLAPSE (bit 0), ждать PWR_ON (bit 31) |
| BCR `GCC_USB_30_BCR` | 0xf000 | bit 0: 1, ≥10 мкс, 0 (Linux: до включения тактов) |
| BCR `GCC_QUSB2PHY_PRIM_BCR` | 0x12000 | то же, ≥100 мкс (в `qusb2_phy_init`) |
| `GCC_CFG_NOC_USB3_AXI_CLK` (cfg_noc) | CBCR 0x05018 | корень 0xf014 |
| `GCC_USB30_MASTER_CLK` (core) | CBCR 0xf008 | корень 0xf014, **120 МГц** |
| `GCC_AGGRE2_USB3_AXI_CLK` (iface) | CBCR 0xf03c | корень 0xf014 |
| `GCC_USB30_SLEEP_CLK` | CBCR 0xf00c | — |
| `GCC_USB30_MOCK_UTMI_CLK` | CBCR 0xf010 | корень 0xf028, 19.2 МГц |
| `GCC_USB_PHY_CFG_AHB2PHY_CLK` (PHY cfg_ahb) | CBCR 0x6a004 | — |
| `GCC_RX0_USB2_CLKREF_CLK` (PHY ref) | 0x88018 bit 0 | `BRANCH_HALT_VOTED` |

Корень `usb30_master_clk_src`: CMD_RCGR 0xf014, родители
`{XO 0, GPLL0 1, GPLL0_EARLY_DIV 6}`, 120 МГц = GPLL0 / 5 → CFG_RCGR:
SRC_SEL (10:8) = 1, SRC_DIV (4:0) = 2·5 − 1 = 9, MODE = 0; GPLL0 —
600 МГц (из той же таблицы). `usb30_mock_utmi_clk_src`: 0xf028, XO —
это `qsoc_clk_enable(id, 19200000)` уже умеет. Все CBCR выше уже в
таблице `sdm660_gcc.h` (`qcom_soc`).

### QSCRATCH (0x0a8f8800) [C]

- `GENERAL_CFG` 0x08: при `select-utmi-as-pipe-clk` — `PIPE_UTMI_CLK_DIS`
  (bit 8) = 1, 100 мкс, `PIPE_UTMI_CLK_SEL` (bit 0) | `PIPE3_PHYSTATUS_SW`
  (bit 3) = 1, 100 мкс, `PIPE_UTMI_CLK_DIS` = 0.
- Режим устройства без extcon: `SS_PHY_CTRL` 0x30 |= `LANE0_PWR_PRESENT`
  (bit 24), `HS_PHY_CTRL` 0x10 |= `UTMI_OTG_VBUS_VALID` (bit 20) |
  `SW_SESSVLD_SEL` (bit 28) — сессия «есть» независимо от детектора VBUS.
  PMIC для этого не нужен.

### PHY QUSB2 (0x0c012000, `qcom,sdm660-qusb2-phy`) [C]

- Питание: `vdd` = L1B (0.8–0.925 В), `vdda-pll` = L10A (1.78–1.95 В),
  `vdda-phy-dpdm` = L7B (2.7–3.125 В). **Ни одно не always-on/boot-on** —
  нынешний `qcom_rpm` их не включает. **Но на планшете все три уже
  включены** [T, 2026-10-16, ramdisk-образ после ABL, до `qcom_rpm`]:
  `PM660 LDOs on: 1 3 6 8 9 10 11 13 14 19`, `PM660L LDOs on: 1 2 4 5 7 9
  10`. Значит, первому кругу запрос к `qcom_rpm` не нужен; драйвер всё
  равно печатает их состояние перед PHY.
- Конфигурация `sdm660_phy_cfg`: таблица msm8996, раскладка msm8996,
  `has_pll_test`, `se_clk_scheme_default = false` (TCSR нет → опора
  **дифференциальная**, нужен `ref`), `mask_core_ready = PLL_LOCKED`.
- Регистры: PLL_TEST 0x04 (bit 7 CLK_REF_SEL), PLL_TUNE 0x08,
  PLL_USER_CTL1 0x0c, PLL_USER_CTL2 0x10, PLL_PWR_CTRL 0x18,
  PLL_AUTOPGM_CTL1 0x1c, PLL_STATUS 0x38 (bit 5 PLL_LOCKED), TUNE1–5
  0x80/0x84/0x88/0x8c/0x90, TEST2 0x9c, PORT_POWERDOWN 0xb4 (bit 5
  CLAMP_N_EN, bit 1 FREEZIO_N, bit 0 POWER_DOWN).
- Порядок `qusb2_phy_init`: питание → cfg_ahb → сброс PHY (BCR, 100 мкс)
  → POWERDOWN |= 0x23 → PLL_TEST сохранить → таблица (TUNE1 0xf8, TUNE2
  0xb3, TUNE3 0x83, TUNE4 0xc0, PLL_TUNE 0x30, USER_CTL1 0x79, USER_CTL2
  0x21, TEST2 0x14, AUTOPGM_CTL1 0x9f, PWR_CTRL 0x00) → подстройка из
  efuse → POWERDOWN &= ~bit 0 → 150 мкс → включить `ref` → PLL_TEST =
  сохранённое & ~CLK_REF_SEL → 100 мкс → PLL_STATUS bit 5, иначе
  «pll lock failed».
- Efuse: QFPROM 0x780000, ячейка `0x243`, биты 1..3 (`bits = <1 3>`);
  ненулевое значение → TUNE2 биты 7:4 (`HSTX_TRIM`). Ноль — оставить
  табличное.

### DWC3 в режиме устройства [C]

- Порядок `dwc3_core_init`: GUSB2PHYCFG (0xc200) — SUSPHY (bit 6) и
  ENBLSLPM (bit 8) **снять** (`dis_u2_susphy_quirk`,
  `dis_enblslpm_quirk`), PHYIF/USBTRDTIM не трогать (режим PHY в дереве
  не задан) → **инициализация PHY** → программный сброс ядра
  (DCTL 0xc704 CSFTRST bit 30, ждать снятия) → GCTL 0xc110 PRTCAPDIR
  (13:12) = 2 → буфер событий (GEVNTADRLO/HI 0xc400/0xc404, GEVNTSIZ
  0xc408, GEVNTCOUNT 0xc40c; 4096 байт).
- Старт гаджета: DCFG 0xc700 SPEED = 0 (high-speed), IGNSTRMPP (bit 23);
  DEPSTARTCFG на ep0; SETEPCONFIG ep0 OUT/IN (control, 512 до соединения);
  DALEPENA 0xc720; TRB SETUP на 8 байт; DEVTEN 0xc708 (reset,
  conndone, disconnect, cmd complete, overflow, errata, wakeup); DCTL
  RUN_STOP (bit 31), ждать DSTS 0xc70c DEVCTRLHLT (bit 22) = 0.
- Команды конечных точек: DEPCMDPAR2/1/0, DEPCMD по 0xc800 + n·0x10;
  CMDACT bit 10, статус 15:12. TRB 16 байт: адрес, длина (23:0), ctrl:
  HWO bit 0, LST 1, CHN 2, CSP 3, тип 9:4 (normal 1, setup 2, status2 3,
  status3 4, data 5), ISP_IMI 10, IOC 11.
- Connect Done: DSTS.CONNECTSPD = 0 → HS, ep0 MPS 64. Linux тут же ставит
  **DCFG.LPM_CAP** (≥ 1.94a) — **мы не ставим**: ABL с LPM на этом хабе
  ломался [T], а хост включает LPM только по BOS (`usb_device_supports_lpm`
  в `hub.c`). Значит `bcdUSB = 0x0200` и никакого BOS.
- Ревизия ядра SDM660 не известна: GSNPSID (0xc120) — первая строка
  драйвера.

### CDC-ECM и хост [C]

- Интерфейсы `f_ecm.c`: управление (класс 2, подкласс 6 ECM, протокол 0;
  функциональные: header `bcdCDC 0x0110`, union, ethernet —
  `iMACAddress` строкой из 12 hex, `wMaxSegmentSize 1514`), interrupt IN
  16 байт, 32 мс; данные (класс 0x0a): alt 0 без точек, alt 1 — bulk
  IN/OUT по 512.
- Хост (`cdc_ether`): шлёт `SET_ETHERNET_PACKET_FILTER` (0x43), читает
  строку MAC (без неё не привязывается), берёт MTU из `wMaxSegmentSize`;
  `NETWORK_CONNECTION` (wValue 1/0) двигает carrier; флаги `FLAG_ETHER |
  FLAG_POINTTOPOINT`.
- Кадр — одна передача; кратный 512 закрывается ZLP [I по правилам bulk].

### Случайность [C]

`rng@793000` — `qcom,prng-ee`, такт `GCC_PRNG_AHB_CLK` (голос 0x52004
bit 13, в таблице). Для `-ee` Linux ничего не настраивает: PRNG_STATUS
(0x04) bit 0 → PRNG_DATA_OUT (0x00), 32 бита. Без него `sshd` получит
ключ хоста из непосеянного пула — то, что на CB2 уже было (`rc`
печатает `WARNING: /dev/random is not seeded`).

### Перезагрузка в fastboot [C]

PON PM660, SID 0, база 0x800 (`qcom,pm8998-pon`, `mode-bootloader =
<0x2>`, `mode-recovery = <0x1>`): `PON_SOFT_RB_SPARE` 0x88f, биты 7:1 =
режим << 1 — для загрузчика записать **0x04** чтением-изменением-записью,
затем PSCI SYSTEM_RESET. Переживает ли значение сброс, который делает
MEECHO (тип сброса PS_HOLD, `hw-pmic.md` §5), — **проверить на планшете**.

## 4. План

Каждый шаг — отдельный образ через `fastboot boot`; в разделе `boot`
остаётся рабочий. Фото нужно только до шага 3; дальше агент читает
`dmesg` хаба, а с шага 4 — сам планшет.

### Шаг 0. Подключение (без нового кода) — **сделано 2026-10-16** [T]

Кабель планшета — в хабе; `fastboot devices` с хаба: `93140a9 fastboot`,
`product: clover`, `unlocked: yes`. L1B, L7B, L10A включены (§3, PHY).
quirk на хабе сделан постоянным (§1).

### Шаг 1. «Перезагрузить в fastboot» из MEECHO

**Сделано и проверено на планшете 2026-10-16** [T]: на экране
`next boot: bootloader (0x88f: 0x00 -> 0x04)`, `sysctl minix.pmic` →
`minix.pmic.nextboot = bootloader`. Круг с хаба без человека: `fastboot
boot meecho-clover-nextboot.img` → через 2 с планшета на шине нет →
на 129-й секунде он снова в fastboot (`bootwd=120` + загрузчик). Причина
пережила сброс с тем PS_HOLD, что оставил ABL, — тип сброса менять не
нужно. Сценарий — `port/clover/hub-cycle.sh` (на хабе — в
`/root/meecho-clover/` рядом с образами).

`qcom_pmic` на старте печатает PON и, если задано, пишет причину
следующей загрузки:

- параметр загрузки **`nextboot=bootloader`** (`recovery`, `normal`) —
  для пробных образов: сброс по `bootwd` уводит в fastboot;
- **`sysctl -w minix.pmic.nextboot=bootloader`** от root, затем
  `/sbin/reboot` — для работы по ssh. Узел — удалённое поддерево MIB
  (`rmib`, `MINIX_PMIC` = 4 в `<minix/sysctl.h>`), право записи проверяет
  библиотека; в `system.conf` у `qcom_pmic` добавлен `mib`;
- `sysctl` положен в ramdisk aarch64: на ramdisk-корне повернуть ручку
  больше нечем.

Тип сброса PS_HOLD драйвер **не меняет**, только печатает: один сброс
покажет, переживает ли причина то, что оставил загрузчик.

- QEMU: сборка; `pmic(info)no SPMI PMIC arbiter in the tree; nothing to
  do`, вход, `sysctl` в ramdisk отвечает, `minix.pmic` нет. Архив сверен с
  архивом комплекта: отличается только `memory` (ramdisk).
- Планшет: образ `meecho-clover-nextboot.img` — ядро и командная строка
  `meecho-clover-boot.img` плюс `nextboot=bootloader bootwd=120`. Строки:
  `pmic(info)PON rev N: PS_HOLD drop = … (type N, enabled|disabled); next
  boot normal (0x88f = 0x00), writable` и `pmic(info)next boot:
  bootloader (0x88f: 0x00 -> 0x04)`; `sysctl minix.pmic` →
  `minix.pmic.nextboot = bootloader`. Через 120 с — **экран fastboot**, а
  не MEECHO из `boot`. Если MEECHO — фото строки PON: дальше — тип сброса
  PS_HOLD, как у Linux (7, жёсткий).

### Шаг 2. Новый драйвер `dwc3_ecm`, проба без записей

**Сделано 2026-10-16** (`4edea27c6`), на планшете — образ
`meecho-clover-usbprobe.img` (`netdrv=dwc3_ecm nextboot=bootloader
bootwd=600`). Служба `minix/drivers/net/dwc3_ecm`: дерево (`dwc3_find.c`),
обвязка (`dwc3_qcom.c`) и PHY (`qusb2.c`) — файлами за таблицами
`compatible → ops`, жизнь службы — `dwc3_ecm.c`. GCC — общий `qcom_soc`
через `.PATH` (в библиотеку — когда появится третий потребитель); у него
теперь есть взгляды только на чтение (`qsoc_clk_on`, `qsoc_gdsc_on`,
`qsoc_bcr_asserted`, `qsoc_clk_root`) и `qsoc_init_gcc()` — без TLMM.
GDSC и BCR генерируются из `gcc-sdm660.c` тем же `gen-sdm660-tables.py`.
**Регистры обвязки, ядра и PHY не отображаются вовсе, пока GCC не показал
GDSC, сбросы и такты** — на этой SoC чтение неготового блока вешает шину.
Ramdisk несёт службу; его `rc` поднимает `netdrv=`, если ramdisk остаётся
корнем.

- Стенд `port/test/dwc3_ecm` (78 проверок) на дереве clover: разбор против
  выписанного из исходника дерева; шесть состояний GCC — что отображено;
  после каждого прогона все модели побайтно прежние; три испорченных
  дерева отвергнуты, ничего не отображено. QEMU: «no USB wrapper … nothing
  to do», система грузится.
- Планшет, строки (`dwc3(info)…`):
  `usb@a8f8800 (qcom,dwc3): qscratch 0xa8f8800, core 0xa800000 irq 163;
  peripheral, high-speed, utmi as pipe`; `USB_30_GDSC on|off (0x…),
  GCC_USB_30_BCR released|held`; `clocks: cfg_noc … core … iface … sleep …
  mock_utmi …`; `roots: core: root on, src N div M; mock_utmi: …`; при
  всём включённом — `core GSNPSID 0x5533…, GCTL … (device|host), …`,
  `device DCFG …, DCTL … (running|stopped), DSTS …`, `qscratch …`; иначе
  `not reading the wrapper or the core: …`. PHY: `phy … supplies vdd l1
  (pm660l), vdda-pll l10 (pm660), vdda-phy-dpdm l7 (pm660l)`, `phy clocks:
  cfg_ahb … ref …; GCC_QUSB2PHY_PRIM_BCR …`, `phy: fuse 0x780243 = 0x…,
  bits 1..3: trim N`, `phy PLL_STATUS … (locked|not locked), POWERDOWN …`.
- **Планшет, 2026-10-16** [T, фото; образ поставлен хранителем с хаба,
  то есть после `fastboot boot`]:
  - `USB_30_GDSC on (0xa0222000)`, `GCC_USB_30_BCR released`; все пять
    тактов обвязки `on`; корни: `core` — src 1 div 5 = GPLL0/5 = 120 МГц,
    как у Linux; `mock_utmi` — **src 6 div 7.5 = GPLL0_EARLY_DIV/7.5 =
    40 МГц** (Linux ставит 19.2).
  - Ядро: `GSNPSID 0x5533300a` — **DWC3 3.00a**, та же ревизия, что у
    CB2; `GCTL 0x00112000` (device), `GUSB2PHYCFG 0x00002400` (UTMI
    8 бит, USBTRDTIM 9, SUSPHY и ENBLSLPM сняты), `GHWPARAMS3 0x10420085`
    (HS PHY — UTMI).
  - Устройство: `DCFG 0x00480880` — HS, адрес 16, NUMP 4 и **LPM_CAP
    (bit 22) поднят**: ABL объявляет LPM — отсюда `EPROTO` fastboot на этом
    хабе; `DCTL 0x10f00000` — **остановлено**; `DSTS 0x00d30641` —
    DEVCTRLHLT, COREIDLE, линия 4 (disconnected).
  - **Буфер событий ABL оставлен: `0x9b566000`, 4096 байт** — это
    свободная память MEECHO (кусок `0x95100000–0x9cc00000` в карте ядра).
    Пока ядро стоит, событий ждать неоткуда [I], но первое, что делает
    захват, — маска событий (GEVNTSIZ bit 31) и сброс ядра, и только
    потом свой буфер.
  - QSCRATCH: `GENERAL_CFG 0x0000000d` — utmi-as-pipe ABL уже выбрал;
    `HS_PHY_CTRL 0`, `SS_PHY_CTRL 0` — подмены VBUS нет.
  - PHY: `cfg_ahb`, `ref` on, сброс снят; fuse `0x18` → trim 4;
    `PLL_STATUS 0x30` (locked), `POWERDOWN 0x22` (CLAMP_N_EN + FREEZIO_N,
    POWER_DOWN снят — включён), `PLL_TEST 0x00` (опора дифференциальная),
    `TUNE1 0xf8` (как таблица), **`TUNE2 0xa3`** — формула mainline дала
    бы `0x43` (trim 4 в биты 7:4): у загрузчика своя поправка.
  - **Вывод для шага 3**: после `fastboot boot` весь путь USB уже поднят
    загрузчиком, и рабочим на этом хабе. Первым шагом — принять его, как
    eMMC: такты, PHY и его подстройку не трогать, ядро сбросить и
    запустить в режиме устройства. Полный подъём (для обычной загрузки из
    `boot`, где ABL USB не поднимает) — отдельно, с регистрами ABL как
    эталоном.
- Образы с хаба ставит `hub-keeper.sh` (шаг 5): fastboot ABL через
  время гасит планшет, поэтому в fastboot планшет бывает секунды — между
  пробами он запаркован в MEECHO (`meecho-clover-park.img`, 10 минут).

### Шаг 3. Перечисление (только ep0) — сначала на том, что поднял ABL

**3а сделан и работает на планшете, 2026-10-16** (`3d900bd9a`, образ
`meecho-clover-usbenum.img`, поставлен хранителем с хаба) [T, `dmesg` и
`lsusb -v` хаба, без фото]:

```
usb 1-2: new high-speed USB device number 21 using xhci-hcd
usb 1-2: New USB device found, idVendor=0525, idProduct=a4a1, bcdDevice= 1.00
usb 1-2: Product: MEECHO USB (dwc3_ecm)
usb 1-2: Manufacturer: MEECHO
```

Через 5.3 с после ухода fastboot с шины (загрузка MEECHO до `rc`),
перечисление — 0.32 с; скорость 480, `bConfigurationValue` 1; `lsusb -v`
через 3.5 минуты — дескрипторы байт в байт как в стенде, qualifier,
`Device Status: Self Powered` (GET_STATUS). LPM хаб не включал: BOS нет.
Стенд — 141 проверка на модели ядра, `mutate.sh` — 10 из 10.

После шага 2 порядок такой (решено по снимку с планшета):

- **3а. Принять состояние загрузчика** (образ запущен `fastboot boot`):
  проверить, что GDSC, такты, сбросы и PHY такие, как в снимке шага 2
  (иначе — отказ строкой, ничего не трогая); GCC, PHY, QSCRATCH-выбор
  utmi **не трогать**. Ядро: маска событий (GEVNTSIZ bit 31) → DCTL
  CSFTRST и ждать → свой буфер событий (4 КиБ, физически непрерывный,
  ниже 4 ГиБ) → подмена VBUS в QSCRATCH (HS_PHY_CTRL bits 20, 28;
  SS_PHY_CTRL bit 24 — как Linux для `peripheral`) → GUSB2PHYCFG как есть
  → DCFG HS, **LPM_CAP снят** → ep0 → DEVTEN → RUN_STOP. ep0 отвечает
  дескрипторами (`bcdUSB 0x0200`, без BOS). VID:PID — `0525:a4a1` (Linux
  g_ether по умолчанию; `cdc_ether` берёт класс, не ID).
- **3б. Полный подъём** — для загрузки из `boot` без fastboot: GDSC, BCR,
  корни (`mock_utmi` — как ABL, 40 МГц, или 19.2 как Linux — решить
  замером), питание PHY (L1B, L7B, L10A уже включены, §3), PHY по Linux с
  `TUNE2` как у ABL. Эталон — снимок шага 2.

- Стенд: модель DWC3 со стороны устройства (как `port/test/xhci/`):
  события Reset/ConnectDone/XferNotReady/XferComplete, SETUP-пакеты из
  перечисления Linux; проверка TRB и команд, мутации; и что до записи
  своего буфера события замаскированы. Ловит ошибки логики, не
  непонимание железа.
- Планшет: `dwc3(info)adopted: …`, `dwc3(info)core reset`,
  `dwc3(info)run`, `dwc3(info)reset`, `dwc3(info)connect: high-speed`,
  `dwc3(info)address N`, `dwc3(info)configuration 1`. **Хаб**: `dmesg` —
  `new high-speed USB device … idVendor=0525` — это агент видит сам, без
  фото. Отказ: строка с этапом и регистром (`core reset timeout`,
  `no reset from the host in 10 s: DSTS 0x…`).

### Шаг 4. ECM + `lwip`, затем ssh

**4а (функция ECM) и 4б (сеть на ramdisk) — 2026-10-16.** `ecm.c`:
раскладка `f_ecm.c` для HS, два уведомления (`NETWORK_CONNECTION`,
`SPEED_CHANGE`), один перенос на кадр, ZLP после кадра, кратного 512.
Стенд — 173 проверки, `mutate.sh` — 17 из 17. QEMU (`netdrv=virtio_net`,
тот же `rc` ramdisk): `vio0` получает адрес и пингует шлюз, 3 из 3.
Планшет, первый прогон [T, `dmesg` и `tcpdump` хаба]: `cdc_ether …
usb0: … CDC Ethernet Device, 06:4d:45:43:48:4f`, carrier 1; IPv6 в обе
стороны (эхо на `ff02::1` — 3 из 3, NS/NA), а ARP — нет. **По USB кадр
идёт без добивки** (ARP-запрос — 42 байта), а `libnetdriver` выбрасывает
всё короче 60 и добивку оставляет драйверу; теперь `ecm_recv()` добивает
нулями. Стенд это ловит (кадр 42 байта после 98-байтного, добивка —
нули). Второй прогон [T]: `ping 10.42.0.2` — 5 из 5 через 6 с после
`fastboot boot`; кадры 512 (с ZLP), 513, 1024, 1514 — по 20 из 20; 200
кадров по 1000 байт через 10 мс — без потерь; 0.4–10 мс (опрос раз в
тик).

**4в пройден на планшете, 2026-10-16** [T, всё по ssh, без фото]:
`userdata` записан с хаба (`fastboot flash`, 108 с, с согласия
пользователя: данные Android там не нужны), `meecho-clover-emmcnet.img` —
и `ssh clover-usb` пускает по ключу. `kmsg` на планшете: `/dev/c0d0:
clean`, `Root is on c0d0 (userdata), driver sdmmc`, `qprng(info)
qcom,prng-ee at 0x793000, GCC_PRNG_AHB_CLK on: a source for the pool`,
`dwc3 … address 29, configuration 1, link up`. Программа, собранная
кросс-компилятором (`kmsg`), — на планшете через `scp` и запущена за
5.4 с. `ssh clover-usb /sbin/reboot` уводит в fastboot
(`nextboot=bootloader`), хранитель грузит образ из очереди; после такого
круга метка в `/root` и ключ хоста на месте, `fsck` — `clean`.

Как было подготовлено:
`rc.clover` поднимает `netdrv=`, `lwip`, адрес и `sshd`; `random` берёт
`qcom,prng-ee` (`qcom_prng.c`; на планшете ещё не исполнялся). QEMU [T]:
тот же корень (`root-clover-net.img`, `mkroot.sh` с `RC_FILE=` и
`AUTHKEYS=`) как диск virtio с `netdrv=virtio_net` — драйверы clover
говорят, что узлов нет, `vio0` с адресом, ключ хоста, вход по ключу
снаружи. Чтение eMMC планшетом есть на снимке шага 2 (EXT_CSD, GPT,
`userdata` 51927 МиБ); запись и монтирование корня там не проверены.
Хаб: постоянный профиль NetworkManager `meecho-clover` (через netplan,
`/etc/netplan/90-NM-5df2ef50….yaml`, совпадение по MAC хоста,
10.42.0.1/24, без шлюза). Рабочая машина: `clover-usb` в `~/.ssh/config`
(`ProxyJump cb2-hub`, `known_hosts` не ведётся — ключ хоста свой у
каждого корня). Образ загрузки — `meecho-clover-emmcnet.img`
(`rootdevname=c0d0 blkdrv=sdmmc gptpart0=userdata … netdrv=dwc3_ecm
nextboot=bootloader bootwd=1800`). **Запись `userdata` портит данные
Android** (LineageOS после неё загрузится только после `fastboot -w`) —
только с явного согласия.

Функция ECM — драйвер `libnetdriver` (образец — `dwmac`), MAC пары
постоянные, локально администрируемые (аргументы `hwaddr=`, `hostaddr=`).
`rc.clover`: `netdrv=dwc3_ecm`, `lwip`, `ifconfig 10.42.0.2/24`,
источник `qcom,prng-ee` в `random` (файл рядом с `trng.c`), `ssh-keygen`,
`sshd`; корень с `AUTHKEYS=`. На хабе — профиль NetworkManager,
привязанный к MAC хоста, 10.42.0.1/24 (имя интерфейса — `usb0` или
`enx…`, решает udev хаба; профиль по MAC годится для обоих).
`~/.ssh/config`: `clover-usb` через `ProxyJump cb2-hub`.

- QEMU: тот же корень clover с `netdrv=virtio_net` — `rc.clover`, `lwip`,
  `sshd`, вход по ключу (`QEMU_HOSTFWD`); драйвер ECM не проверяется.
- Стенд: ECM-запросы, кадры, ZLP на 512, notification.
- Планшет/хаб: `cdc_ether …: register 'cdc_ether' … CDC Ethernet Device`,
  `ping 10.42.0.2`, `ssh clover-usb uname -a`. На экране:
  `dwc3_ecm(info)host set alt 1: link up`, `random(info)qcom prng`.

### Шаг 5. Круг без человека

`port/clover/cycle.sh`: собрать `boot.img` → на хаб → `ssh` «в fastboot» →
`fastboot boot` с хаба → ждать ssh → прогон → журнал. Пробные образы — с
`bootwd`: зависший вернётся к образу из `boot`. Прошивка `boot` — только
с явного согласия.

**Половина есть с шага 1** [T, 2026-10-16]: fastboot ABL через некоторое
время гасит планшет (вернуть — только кнопками), поэтому на хабе работает
`hub-keeper.sh` (ограниченное число часов, `touch stop`): как только
планшет показывается в fastboot, ставит образ из `queue/` или парковочный
`meecho-clover-park.img` (`nextboot=bootloader bootwd=600`). В fastboot
планшет бывает секунды, образ из очереди ждёт не больше 10 минут. Пока
вручную вводить планшет в fastboot нельзя: хранитель тут же поставит
образ.

**Шаг 5 сделан, 2026-10-17** [T]: `port/clover/cycle.sh`, из Git Bash
(адреса и ключ — `port/board.conf`, сборка — в WSL файлом, как у
`board-cc.sh`):

```
KERNEL_ELF=… BOOT_MBA=… port/clover/cycle.sh [-i boot.img] [-c cmd | -s script] [-l log] [-t s]
```

Сборка `mkboot.sh` + `checkboot.py`, к `CMDLINE` (по умолчанию корень на
eMMC с сетью) дописывается `cycle=<id>`; выкладка в `queue/` под
скрытым именем, сверка sha256, переименование; хранитель — запустить,
если стоит; планшет на ssh — `sysctl -w minix.pmic.nextboot=bootloader`
и перезагрузка, нет — ждать хранителя; «образ поднялся» — только когда
`sysenv cycle` отвечает своим `<id>`; прогон и `kmsg` — в журнал
`~/work-clover-cycle/cycle-<id>.log`. Код выхода — команды; 101 сборка,
102 хаб или выкладка, 103 хранитель не загрузил вовремя (образ снимается
с очереди, иначе загрузится позже, когда его никто не ждёт), 104
загружен, но ssh нет (в журнал — хвост шины хаба).

Проверено на планшете: круг с планшета на ssh — 51 с (сборка из готового
архива 2 с, выкладка 11 с, перезагрузка и fastboot 25 с, ssh через 6–15 с
после `fastboot boot`); с планшета без ssh — хранитель взял образ, когда
сторож прежнего вернул планшет в fastboot; 104 — образ на ramdisk без
sshd: хвост шины показывает `MEECHO USB` и `usb0`, а ssh нет; 103 — образ
снят, хранитель поставил парковку. 102 нарочно не вызывался.

Две ловушки, обе стоили прогона. `pgrep -f hub-keeper.sh` на хабе ловит
и ту команду ssh, в строке которой это имя стоит, — ждёт само себя;
`cycle.sh` спрашивает `pgrep -fx 'bash hub-keeper.sh'`. И сессия ssh с
`/sbin/reboot` не кончается, пока перезагрузка не заберёт сеть (25 с):
ожидание очереди начиналось, когда образ уже загружен. Теперь у неё 12 с.

Открыто: `LWIP: warning, no TCP ISN secret has been set` в `kmsg`, хотя
`random` с `qprng` поднят раньше `lwip`. Почему пул в этот момент не
отдаёт байты — не разобрано [I].

## Что остаётся человеку

Шаги 0–3 (фото экрана, вход в fastboot кнопками). С шага 4в — только
зависание раньше `bootwd` (сторожа SoC для clover нет): долгое нажатие
питания. Образ, после которого `ssh` не поднялся, виден только на экране;
`cycle.sh` говорит об этом кодом 104 и хвостом шины хаба. Запись
разделов планшета — по-прежнему только с согласия.

## Объём (оценка) [I]

| Часть | Строк | Где проверяется |
|---|---|---|
| обвязка + PHY + GCC (+ перенос `qcom_soc`) | ~500 | планшет |
| DWC3 устройства + ep0 | ~1300 | стенд + планшет |
| ECM + `libnetdriver` | ~600 | стенд + планшет |
| PRNG, `pmicctl`, `rc.clover`, сценарии | ~300 | QEMU + планшет |
| стенд DWC3/ECM | ~800 | хост |

Для сравнения: `xhci` — 5.5 тыс. строк, `dwmac` — 2.8 тыс.
