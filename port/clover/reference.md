# Xiaomi Mi Pad 4 (clover) — эталон с устройства

Снято 2026-10-15 с работающей LineageOS через `adb` без root (хаб, USB);
сырые ответы — `adb-lineage/`, серийный номер заменён на `<serial>`.

## Первый запуск и первый login (2026-10-15—16)

`port/clover/mkboot.sh` → `fastboot boot` (ничего не прошивается):
`Sending OKAY`, `Booting OKAY`. **На экране планшета появились сообщения
ядра MEECHO** — ABL принял образ v0 с `Image.gz`+дерево, ядро
переместилось (`head.S`), нашло `simple-framebuffer` и пишет туда
(`fbcon.c`). Первая паника была `device tree describes no GIC this kernel
can drive`: узел GICv3 лежит под `/soc`, а сканер принимал только дочерние
узлы корня. После исправления ядро запустило все boot images и VFS.

Следующая остановка после `Started VFS` была не отказом таймера: временная
печать из обработчика показала `timer IRQ 18 on CPU 0`. Init не мог открыть
`/dev/console`, потому что `stdout-path` ведёт к `qcom,msm-uartdm`, а TTY
знает пока только PL011 и 8250/DW. С `console=console` пользовательский
вывод идёт через kernel framebuffer; на планшете подтверждены imgrd,
`/proc`, rc, getty и **`login:`**. Ввод с USB/touch ещё не проверен.

Дерево — **`sdm660-xiaomi-clover`** (LTE: `variant: SDM EMMC`), из
github.com/sdm660-mainline/linux (`72bc4bb`); postmarketOS берёт его же для
`cloverlte` и `sda660-…` для Wi-Fi-модели.

## Что известно точно

| Что | Ответ |
|---|---|
| Модель | `MI PAD 4`, `clover`, плата `sdm660`; RAM ~3.7 ГБ видно Linux (4 ГБ) |
| Система | LineageOS 22.2 (Android 15), ядро downstream 4.19.325 |
| Загрузчик | разблокирован (`verifiedbootstate=orange`) |
| Процессор | 8 ядер, `CPU part` 0x800 ×4 и 0x801 ×4 (Kryo 260 Gold/Silver, A73/A53); ARMv8.0: `Features` без `atomics` — **ни LSE, ни PAN** |
| GIC | `interrupt-controller@17a00000` (GICv3 у SDM660, GICD там) |
| PSCI | узел `psci` есть (метод не прочитан — нужен root) |
| Буфер экрана ABL | `reserved-memory/splash_region@9d400000` |
| RAM | с `0x80000000` (`removed_regions@85800000`, `modem_fw_region@8ac00000`, … `dfps_data_mem@0x9f7ff000`) |
| eMMC / SD | `sdhci@c0c4000` / `sdhci@c084000` (`sdhci-msm`) |
| USB | `ssusb@a800000` (DWC3), PHY `qusb@c012000`; `hsusb@c200000` |
| UART | BLSP: `serial@0c170000`, `serial@0c1b0000`, `uart@c16f000…` — выводы на тестовых точках |
| Сторож | `qcom,wdt@17817000` |
| RNG | `qrng@793000` |
| Таймер MMIO | `timer@17920000` |

## Чего нет и что нужно

- Содержимое свойств дерева, `/proc/iomem`, `/proc/cmdline`, `/sys/class/graphics/fb0`
  SELinux не отдаёт оболочке `shell` — нужен root (`adb root`, «Rooted debugging»
  в параметрах разработчика). Нужны: `reg` у `memory` и `splash_region`,
  `psci/method`, `stdout-path`, формат и шаг буфера экрана.
- Сборка LineageOS — `user` (`ro.debuggable=0`): `adb root` невозможен в принципе,
  пункта «Rooted debugging» нет. Недостающее берётся из mainline-дерева.

## Загрузчик (fastboot, `fastboot-getvar.txt`)

`product:clover`, `variant:SDM EMMC`, `unlocked:yes`, `secure:yes`, `kernel:uefi`
(ABL на UEFI), `max-download-size:536870912` (512 МиБ на `fastboot boot`),
`anti:1`. Строка `token` (токен разблокировки Mi) из файла убрана намеренно.

**fastboot с хаба (Tanix TX9 Pro, xHCI Amlogic) работает только без USB 2.0 LPM.**
С LPM первая же запись — `EPROTO`, а ABL падает в чёрный экран «press any key
to shutdown» (с обычного ПК такого нет). Лечение на хабе:
`echo 18d1:d00d:k,18d1:4e11:k > /sys/module/usbcore/parameters/quirks`
**до** подключения планшета (quirk применяется при перечислении); живёт до
перезагрузки хаба. Autosuspend (`usbcore.autosuspend=-1`) отдельно не помогал.
- Для драйверов брать mainline-дерево postmarketOS (`sdm660-xiaomi-clover`), не
  downstream: его привязки другие, адреса блоков те же.
