# Mi Pad 4: эталон с postmarketOS (2026-10-19)

Снято 2026-10-19 с работающего postmarketOS v26.06 на самом планшете
(`ssh user@172.16.42.1` через хаб, USB-ECM; root — временный `sudo` без
пароля, снят после снятия). Ядро `6.19.10-sdm660-ipa`
(`linux-postmarketos-qcom-sdm660` 6.19.10-r0, собрано `pmos@nura-build`
2026-10-17, clang 22). Корень — `userdata` (тот же раздел, что у MEECHO —
`clover-usb-access`); загрузка — прошитый `boot`.

**В отличие от LineageOS (`reference.md`) здесь работает mainline-путь
целиком, с root: модем, Wi-Fi, BT, звук, датчики, батарея.** Это эталон того,
что делает с этим железом система, у которой всё это работает.

- Тексты — `pmos-20261019/` (`pass1` — общий снимок, `pass2` — журналы,
  QMI, прошивки, `pass3` — sysfs классов, gpio, pinmux), там же скрипты
  снятия (`capture*.sh`) и **дерево устройства, по которому живёт это ядро**
  (`sdm660-xiaomi-clover-pmos.dts`, декомпилировано из `/sys/firmware/fdt`).
  **Убрано:** IMEI, номер, IMSI, ICCID, серийный номер, идентификаторы
  соты, имена соседних сетей Wi-Fi, файл SIM — вместо них `<…>`.
- **Прошивки — вне дерева**: `D:\minix\clover-flash\pmos\fw\` — разделы
  `modem` (`image/`, 100 МБ, 490 файлов), `dsp`, `bluetooth`,
  `vendor/firmware`; дайджесты всех совпали с `pass2/fw/*-ls.txt`, снятыми
  на планшете. Лицензия у них Xiaomi/Qualcomm — в дерево BSD не идут (как
  `fetch-gpu-fw.sh`). Сырые снимки без чистки — там же, `root*/`.
- **Разделы `modemst1/2`, `fsg`, `fsc`, `persist` не копировались**: в них
  NV модема (IMEI, калибровка) этого экземпляра. `rmtfs` должен отдавать
  модему их самих, а не копию (`pass2/sys/partitions.txt` — смещения).

## Как поднимается модем (`pass1/modem/dmesg.txt`, `pass2/journal/`)

| t, с | Что |
|---|---|
| 15 | `msm-firmware-loader.sh` монтирует `modem` (vfat), `dsp`, `bluetooth`, `persist`, `vendor` в `/run/msm-firmware-loader/mnt/*` **только на чтение** и строит каталог ссылок `target/` — путь поиска прошивок ядра (`firmware_class.path`). В `/lib/firmware` только ath10k, a530 и конфиг тача |
| 22.6 | IPA (`qcom,sdm660-ipa`, `qcom,ipa-lite-v2.6`, `0x14780000`, драйвер **`ipa2_lite`** — не mainline): `IPA version 0x1030300 (hw 0x20060001)`, 8 КБ SRAM |
| 22.6–23.2 | ADSP (`15700000`, `adsp.mdt`) и CDSP (`1a300000`, `cdsp.mdt`) — `qcom_q6v5_pas`, без участия пользовательских служб |
| 30.2 | systemd поднимает `tqftpserv` и `rmtfs -r -P -s` (`/dev/qcom_rmtfs_mem1`, память `0x85e00000` 2 МБ, client-id 1, vmid 15; `-P` — разделы по именам) |
| 30.3 | MSS (`4080000`, `qcom,sdm660-mss-pil`, `qcom_q6v5_mss`): `mba.mbn` 238 КБ → `MBA booted without debug policy, loading mpss` (`modem.mdt`) |
| 31.8 | `remote processor 4080000.remoteproc is now up`; rmtfs: модем просит `/boot/modem_fsg_oem_1/2` — отказ, модему это не мешает |
| 32.0 | tqftpserv: модем пишет `/readwrite/server_check.txt`, ищет `ota_firewall/ruleset`, `mcfg.tmp` (нет — не мешает) |
| 32.04 | `ipa: modem IPA QMI service found (node 0 port 35)` → `INIT_DRIVER` → `data path enabled` |
| 32.8 | ath10k: QMI wlfw модема отвечает (см. ниже) |
| 33.9 | `msm-modem-uim-selection`: выбирает приложение USIM в слоте 1 |
| 39–51 | ModemManager (`qcom-soc`, порт `qrtr0` QMI + `rmnet_ipa0`): searching → **home, attached** (LTE, eutran-3) |

Ревизия mpss — `MPSS.AT.3.1-00729.1-SDM660_GEN_PACK-1.214729.1.215613.1`
(2019-07-30); LTE bands 1, 3, 5, 7, 8, 34, 38–41; только LTE.
QMI отвечает **прямо по QRTR** (`qmicli -d qrtr://0`, `pass1/modem/
qmi-over-qrtr.txt`, `pass2/modem/nas.txt`) — отдельного узла устройства
нет. `WDA get data format` — `InvalidArgument`: формат данных IPA
согласует сам драйвер.

**Наблюдение, не наш дефект:** через ~9 мин после загрузки SIM потеряла
ATR (`Card state: 'error: no-atr-received'`), модем ушёл в
`emergency-only`; к снятию так и осталось. Пять операторов в поиске
(`pass2/modem/mmcli-signal.txt`) — радиочасть жива.

## Wi-Fi (`pass1/wifi/`, `pass2/wifi/`)

WCN3990 через `ath10k_snoc` (`wifi@18800000`, 8 МБ регистров, 12 линий
SPI 413–425 без 419, SMMU `0x1a00/0x1a01` на `apps_smmu`). **Прошивка
Wi-Fi исполняется в модеме** (`wlanmdsp.mbn` в `modem/image`), ath10k
говорит с ней QMI-службой wlfw по QRTR — значит **без MSS, rmtfs и QRTR
нет и Wi-Fi**:

```
qmi chip_id 0x30214 chip_family 0x4001 board_id 0xff soc_id 0x40050000
qmi fw_version 0x101f81f0 ... WLAN.HL.1.0.1.c2-00496-QCAHLSWMTPLZ-1
wcn3990 hw1.0 ... firmware ver api 5 ... crc32 022c9087
htt-ver 3.56 wmi-op 4 htt-op 3 cal file max-sta 32 raw 0 hwcrypto 1
invalid MAC address; choosing random
```

- Хост берёт `ath10k/WCN3990/hw1.0/firmware-5.bin` (crc `022c9087`) и
  `board-2.bin` (crc `ecd45d37`) из `/lib/firmware` (пакет
  `firmware-xiaomi-clover`, github.com/setotau/firmware-xiaomi-clover);
  `bdwlan.*` в `modem/image` — платы под разные board_id, `0xff`
  выбирает из `board-2.bin`.
- В дереве: `qcom,snoc-host-cap-8bit-quirk`, `qcom,no-msa-ready-indicator`,
  память MSA — `wlan-msa-mem@85700000` (1 МБ) плюс guard `@85600000`.
  Питание `vdd-3.3-ch1` не объявлено (dummy) — работает.
- MAC: в OTP/persist нет, `bootmac` ставит локальный `02:00:…`.
- **Радио работает**: поиск видит 21 сеть, 13 на 2.4 ГГц и 8 на 5 ГГц
  (`pass2/wifi/scan-summary.txt`); подключения к сети не было.

## Остальное, что стоит взять

| Что | Факт | Где |
|---|---|---|
| Bluetooth | WCN3990 по UART (`hci_uart`/`btqca`, SOC `0x40010214`, ROM `0x201`); грузит `qca/crbtfw21.tlv`+`qca/crnv21.bin` с раздела `bluetooth` | `pass1/bt/`, `pass2/bt/` |
| Батарея | `qcom-battery` (`pmi8998-fg` на PM660): 6010000 мкА·ч по паспорту, 4.34 В, 99 %; зарядка `qcom-smbx-charger` (`pm660-charger`, USB) | `pass3/power_supply.txt` |
| Датчики | акселерометр, гироскоп, магнитометр, приближение — **через SLPI/ADSP** (`sns_smgr`, `qcom_smgr_*`, IIO), реестр `qcom/sensors/sns.reg`; напрямую по I2C их нет | `pass3/iio.txt`, `pass1/sensors/` |
| Звук | ADSP + APR (`q6afe`, `q6asm`, `q6routing`), кодек PM660 `msm8916-wcd` analog/digital, карта `sdm660-int-sndcard`; прошивка усилителя `tas2557s_uCDSP.bin` на разделе есть, драйвера `tas2557` в этом ядре нет | `pass1/audio/` |
| Тач | `edt-ft5406` на `i2c 0-0038` (`hw-touch.md`) | `pass1/misc/i2c.txt` |
| Кнопки | `pm8941_pwrkey`, `pm8941_resin`, `gpio-keys` (громкость), датчик Холла (`gpio-hall-sensor`) | `pass1/input/devices.txt` |
| Экран | DSI `nt51021` BOE WUXGA video, `msm_dpu`; подсветка `qcom,wled` | `pass3/backlight.txt` |
| Температура | `qcom-tsens`, `spmi-temp-alarm` ×2, `qcom-spmi-adc5` | `pass3/thermal.txt`, `hwmon.txt` |
| RTC | `rtc-pm8xxx` | `pass3/rtc.txt` |
| Регуляторы, такты, шины | `regulator_summary`, `clk_summary`, `interconnect_summary` с работающей системы | `pass1/debug/` |
| Частота ЦП | cpufreq, политики и частоты | `pass2/sys/proc-cpu.txt` |
| Выводы | `pinmux-pins`, `gpio` — кто что занял | `pass3/pinmux.txt`, `gpio.txt` |

## Что отсюда следует для MEECHO

- **Wi-Fi и модем — одна работа, не две.** Порядок: smem/smp2p/GLINK к
  модему → MSS PIL (MBA, затем mpss; `qcom_q6v5_mss` — его логика halt,
  RMB, smp2p) → QRTR поверх GLINK → `rmtfs` (раздаёт `modemst1/2`, `fsg`
  через общую память) и `tqftpserv` → дальше QMI: wlfw для ath10k, IPA и
  NAS/WDS для данных. Ни `pd-mapper`, ни `hexagonrpcd` в пользовательском
  пространстве не запущены: `pd-mapper` — модуль ядра (`qcom_pd_mapper`).
- **IPA — нестандартная часть.** Дерево и драйвер `ipa2_lite`
  (`qcom,ipa-lite-v2.6`) есть только в этой сборке, не в sdm660-mainline
  `72bc4bb`, на котором построено дерево MEECHO. Исходники драйвера надо
  взять у того, кто собрал это ядро: в снимке их нет (`pass2/modem/
  modinfo.txt`, `pass1/sys/config.gz` — только конфигурация).
- Эталон QMI на обмен сообщениями снят не побайтно: следующий шаг, если
  понадобится, — `qrtr` с `QRTR` трассировкой или `qmicli --verbose` на
  том же планшете.
