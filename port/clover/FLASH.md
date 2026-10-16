# MEECHO на Mi Pad 4: прошивка раздела `boot`

Как поставить MEECHO на планшет постоянно — так, чтобы она стартовала при
включении, — и как вернуть всё назад. Основано на исходниках LineageOS 22.2
для clover (`kyasu/android_device_xiaomi_clover` и `…_sdm660-common`,
ветка `lineage-22.2`) и на `deviceinfo` postmarketOS (`device-xiaomi-clover`).

## Что прошивается и что нет

- Прошивается **только раздел `boot`** (64 МиБ,
  `BOARD_BOOTIMAGE_PARTITION_SIZE := 0x04000000`). Образ MEECHO — ~15 МБ:
  ядро `Image.gz` с деревом `sdm660-xiaomi-clover` и `boot.mba` в поле
  ramdisk; заголовок v0, страница 4096, база 0 — ровно формат LineageOS
  (`BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb`, без `BOARD_BOOT_HEADER_VERSION`).
- **AVB/`vbmeta` и `dtbo` у clover нет** (в BoardConfig ни одной такой
  переменной): прошивать больше ничего не нужно, и ABL обработает
  прошитый образ так же, как `fastboot boot`, который уже проверен на этом
  планшете.
- **Не трогаются**: загрузчик (ABL, в нём fastboot), `recovery`, `system`,
  `vendor`, `userdata`. Данные LineageOS остаются на месте, она просто не
  загружается, пока в `boot` стоит MEECHO.
- Режим fastboot живёт в ABL, не в `boot`: **громкость вниз + питание**
  работает всегда, что бы ни было в `boot`.

## Что MEECHO на планшете умеет сейчас — и чего нет

- Есть: сообщения ядра и консоль на экране, приглашение `login:`.
- Нет ввода: ни USB-клавиатуры, ни сенсорного экрана — войти пока нельзя.
- Нет хранилища: корень — ramdisk, ничего не сохраняется между
  загрузками (eMMC-драйвера для `sdhci-msm` ещё нет).
- Одно ядро (`no_smp=1`): SMP на big.LITTLE SDM660 не проверялся.
- Нет управления питанием: экран горит всё время, заряд аккумулятором
  MEECHO не управляет. Не оставлять планшет на MEECHO надолго без зарядки.

То есть прошивка даёт одно — старт без компьютера. Пока нет ввода, разница
с `fastboot boot` невелика; зато прошивка полностью обратима.

## Перед прошивкой (обязательно)

1. **Образ, которым откатываться.** `boot.img` той LineageOS, что стоит на
   планшете (22.2, UNOFFICIAL, 2025-05-09): он лежит внутри её ZIP
   (`unzip lineage-22.2-…-clover.zip boot.img`). Без него откат —
   только переустановкой системы через recovery.
2. **Recovery жив**: громкость вверх + питание открывает recovery (TWRP или
   LineageOS recovery) — запасной путь, если `boot.img` не найдётся
   (`adb sideload` ZIP-а LineageOS).
3. Заряд > 50 %.
4. **Тот же файл сначала через `fastboot boot`** и только потом `flash`:
   `fastboot boot` ничего не пишет; если на экране `login:`, тот же файл
   можно прошивать.
5. Контрольная сумма совпадает с `meecho-clover-boot.img.sha256`.

## Прошивка

Планшет в fastboot (громкость вниз + питание), подключён к компьютеру:

```
fastboot devices
fastboot getvar product          # должно быть: clover
fastboot boot meecho-clover-boot.img     # проба из RAM, ничего не пишет
```

Если проба показала `login:` — перевести планшет снова в fastboot и:

```
fastboot flash boot meecho-clover-boot.img
fastboot reboot
```

После перезагрузки — предупреждение Xiaomi о разблокированном загрузчике
(«orange state», несколько секунд), затем экран MEECHO.

## Откат на LineageOS

```
fastboot flash boot lineage-boot.img     # boot.img из ZIP LineageOS
fastboot reboot
```

Или recovery (громкость вверх + питание) → `adb sideload lineage-…zip`.

## Как собран образ

`port/clover/mkboot.sh` в отдельном `WORK_DIR`, командная строка ядра
`bootramdisk=1 console=console no_smp=1` — **без `bootwd`**: сторож
загрузки перезагружает через N секунд, и с прошитым `boot` это значило бы
бесконечный круг MEECHO → сброс → MEECHO, а не возврат в LineageOS, как
после `fastboot boot`. Проверка образа — `port/clover/checkboot.py`:
заголовок, адреса, SHA-1, ядро после gzip, приложенное дерево, ramdisk,
размер раздела — каждый кусок сверяется с исходным файлом.
