# MEECHO на Mi Pad 4: образы, проба и прошивка

Как запустить MEECHO на планшете, поставить её постоянно и вернуть всё
назад. Основано на исходниках LineageOS 22.2 для clover
(`kyasu/android_device_xiaomi_clover` и `…_sdm660-common`, ветка
`lineage-22.2`) и на `deviceinfo` postmarketOS (`device-xiaomi-clover`).
Что проверять на экране и в каком порядке — [CHECKLIST.md](CHECKLIST.md).

## Комплект

| Файл | Что это | Командная строка ядра |
|---|---|---|
| `meecho-clover-safe.img` | запасной: корень в памяти, одно ядро, без драйвера eMMC | `bootramdisk=1 console=console osk=1 no_smp=1` |
| `meecho-clover-boot.img` | основной: корень в памяти, все ядра, eMMC **только на чтение** | `bootramdisk=1 console=console osk=1 blkdrv=sdmmc` |
| `meecho-clover-emmc.img` | корень на eMMC, в разделе `userdata` | `rootdevname=c0d0 blkdrv=sdmmc gptpart0=userdata console=console osk=1` |
| `meecho-clover-root.img.gz` | файловая система для `userdata` (1 ГиБ после `gunzip`), полный userland | — |
| `SHA256SUMS` | контрольные суммы | — |

Все три образа содержат одно и то же ядро и один и тот же загрузочный
архив; различаются только командной строкой. Формат — заголовок v0,
страница 4096, `Image.gz` с приложенным деревом `sdm660-xiaomi-clover`,
ровно как у LineageOS (`BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb`).
**AVB/`vbmeta` и `dtbo` у clover нет**, раздел `boot` — 64 МиБ.

## Что умеет и чего нет

Собрано и проверено без планшета (QEMU и стенды на настоящем дереве
clover); на железе не проверено ничего из нового — см. CHECKLIST.

- Экран: консоль крупным шрифтом, внизу экранная клавиатура (`osk=1`).
- Ввод: касания по клавиатуре; громкость — стрелки вверх/вниз; кнопка
  питания гасит и зажигает подсветку.
- Аккумулятор: уровень, напряжение и зарядка — в журнале (`pmic(…)`).
- Ядра: все восемь (кроме `safe`).
- Хранилище: eMMC берётся как оставил загрузчик; без окна `gptpart0=` —
  только чтение; с окном — один раздел и ни сектора вне его.
- **Нет**: выключения по `poweroff` (может не сработать), автогашения
  экрана, USB, Wi-Fi, звука.

## Защита загрузчиков

На той же eMMC лежат xbl и abl — испорченный сектор там стоит
планшета. Поэтому драйвер eMMC:

- не трогает контроллер, пока GCC не показывает оба его такта
  включёнными (иначе на этой SoC чтение регистра вешает шину);
- не сбрасывает контроллер и не меняет такт, ширину и режим шины;
- без `gptpart0=` отказывает в любой записи (`EROFS`);
- с `gptpart0=userdata` пишет только внутри раздела `userdata`, найденного
  по GPT с проверкой обеих CRC и единственности имени; запрос за его
  границей отклоняется до карты;
- гнездо SD не трогает вовсе.

## Перед любой прошивкой

1. **Образ для отката.** `boot.img` той LineageOS, что стоит на планшете
   (22.2, UNOFFICIAL, 2025-05-09), из её ZIP
   (`unzip lineage-22.2-…-clover.zip boot.img`).
2. **Recovery жив**: громкость вверх + питание.
3. Заряд > 50 %.
4. `sha256sum -c SHA256SUMS` (на Mac — `shasum -a 256 -c SHA256SUMS`;
   строку про распакованный образ проверять после `gunzip`).
5. **Каждый образ сначала через `fastboot boot`.** Он ничего не пишет;
   следующая перезагрузка — снова то, что в `boot`.

## Проба (ничего не пишет)

Планшет в fastboot (громкость вниз + питание):

```
fastboot devices
fastboot getvar product                  # clover
fastboot boot meecho-clover-safe.img
```

Затем так же `meecho-clover-boot.img` (шаги — CHECKLIST, 1 и 2).

## Корень на eMMC (стирает данные Android)

Только если `meecho-clover-boot.img` показал `eMMC adopted` и `EFI PART`
(CHECKLIST, шаг 2). Раздел `userdata` перезаписывается файловой
системой MEECHO: данные LineageOS пропадут, сама LineageOS после этого
не загрузится, пока `userdata` не отформатировать заново
(`fastboot -w` или recovery).

```
gunzip meecho-clover-root.img.gz
fastboot flash userdata meecho-clover-root.img
fastboot boot meecho-clover-emmc.img
```

`fastboot` сам разобьёт гигабайтный образ на части (предел приёма у
этого ABL — 512 МиБ). Записывается первый гигабайт раздела; остальное не
трогается и MEECHO не видно.

## Постоянная установка

Когда проба нужного образа прошла:

```
fastboot flash boot meecho-clover-emmc.img     # или -boot / -safe
fastboot reboot
```

После перезагрузки — предупреждение Xiaomi о разблокированном
загрузчике, затем MEECHO. Режим fastboot живёт в ABL, а не в `boot`:
**громкость вниз + питание** работает всегда.

## Откат на LineageOS

```
fastboot flash boot lineage-boot.img     # boot.img из ZIP LineageOS
fastboot -w                              # только если писали userdata
fastboot reboot
```

Или recovery (громкость вверх + питание) → `adb sideload lineage-…zip`.

## Как собрано

`port/clover/mkboot.sh` с `BOOT_MBA=` (один архив на три образа) и
`KERNEL_ELF=` (вариант ядра `-O2` без отладочных секций), корень —
`port/mkroot.sh` с `RC_FILE=port/clover/rc.clover ROOT_NAME=root-clover`.
**Без `bootwd`**: с прошитым `boot` сторож загрузки дал бы бесконечный круг
сбросов. Каждый образ проверен `port/clover/checkboot.py` (заголовок,
адреса, SHA-1, ядро после gzip, дерево, ramdisk, размер раздела — каждый
кусок против исходного файла), а те же `kernel.bin` и `boot.mba` —
загрузкой на QEMU.
