# MEECHO на Mi Pad 4: продолжение на Mac (Apple Silicon)

Для человека и для Claude на новой машине. Всё нужное приходит одним
`git clone`: дерево, история, скрипты, эталон с планшета; тулчейн и
система собираются из дерева (`port/build-all.sh`). Двоичных файлов
переносить не нужно.

## Где остановились (2026-10-16)

- `fastboot boot` образа MEECHO доходит на Mi Pad 4 до **`login:` на
  экране планшета**. ABL принимает образ v0, reloc-ядро пишет в переданный
  `simple-framebuffer`, загружает двенадцать процессов, монтирует imgrd и
  `/proc`, запускает rc и getty.
- Первая паника была `device tree describes no GIC this kernel can drive`:
  сканер принимал GIC только непосредственно под корнем, а у Clover
  `arm,gic-v3` лежит под `/soc`. Теперь GIC находится на любой глубине и
  его `reg` разбирается с шириной ячеек родительской шины.
- Временный маркер подтвердил первый аппаратный тик: `timer IRQ 18 on CPU
  0`. После этого остановка за `Started VFS` оказалась консолью, а не
  таймером: `stdout-path` указывает на `qcom,msm-uartdm`, которого TTY пока
  не поддерживает, поэтому `console=tty00` давал `ENXIO` при открытии
  `/dev/console` из init.
- Для Clover `mkboot.sh` теперь передаёт `console=console`. AArch64 ttyc0
  выводит пользовательскую консоль через `SYS_DIAGCTL` в уже работающий
  kernel framebuffer; обратную доставку kernel messages он поглощает, чтобы
  не создать эхо-цикл. Тот же путь проверен framebuffer-стендом QEMU.
- Следующая работа — ввод на планшете: приглашение видно, но USB-клавиатура
  и touchscreen ещё не проверены. UART остаётся на тестовых точках под
  крышкой и для этого рубежа не нужен.

## Планшет

- Xiaomi Mi Pad 4, **LTE-модель** (`fastboot getvar variant` → `SDM EMMC`)
  → дерево **`sdm660-xiaomi-clover`**; у Wi-Fi-модели было бы `sda660-…`.
- Загрузчик разблокирован; на планшете LineageOS 22.2 (сборка `user`,
  root через adb невозможен и не нужен).
- Режим fastboot: выключить, зажать **громкость вниз + питание**.
- `fastboot boot` ничего не прошивает: следующая перезагрузка — снова
  LineageOS. Аргумент ядра `bootwd=600` (по умолчанию в `mkboot.sh`)
  сам перезагружает через 10 минут; иначе — долгое нажатие питания.
- Прежний хост (Linux на ТВ-приставке Amlogic) работал с fastboot только
  без USB LPM; с Mac этого быть не должно. Признак беды: `fastboot`
  отвечает `Protocol error`, на планшете чёрный экран «press any key to
  shutdown».

## Mac: подготовка (один раз)

Сборка идёт в Linux-машине **OrbStack** (родной arm64, без Rosetta);
планшет подключается к самому macOS.

1. На macOS:
   ```
   brew install --cask orbstack android-platform-tools
   ```
2. Linux-машина Ubuntu **26.04** (нужен пакет `gcc-15-source`; в 24.04 его
   нет):
   ```
   orb create ubuntu:26.04 meecho
   orb -m meecho -u root bash -c 'useradd -m -s /bin/bash minix &&
       echo "minix ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/minix'
   ```
   Если тег `26.04` не найден — `orb images` / документация OrbStack;
   проверить потом `lsb_release -a` и `apt-cache policy gcc-15-source`.
   Пользователь именно **`minix`**: `CLAUDE.md` и часть скриптов пишут
   пути `/home/minix/...`.
3. Внутри машины (`orb -m meecho -u minix`):
   ```
   sudo apt update && sudo apt install -y build-essential git python3 \
       device-tree-compiler qemu-system-arm socat xz-utils zlib1g-dev \
       gcc-15-source binutils-source libgmp-dev libmpfr-dev libmpc-dev \
       libisl-dev flex bison libzstd-dev texinfo
   git clone -b aarch64 https://github.com/valam84/meecho-os.git ~/minix-src
   cd ~/minix-src && git config user.name valam && git config user.email valam84@gmail.com
   mkdir -p ~/bin && for f in build-minix ramimage tree-includes tree-libs \
       build-xtools build-dirs hello-aarch64; do cp port/$f.sh ~/bin/; done
   ```
   Если репозиторий закрыт — сначала вход в GitHub в машине
   (`sudo apt install gh && gh auth login`).
   Дерево — **только внутри Linux-машины** (ext4), не в папке macOS:
   APFS не различает регистр, а в NetBSD-части есть такие имена.
4. Собрать всё с нуля:
   ```
   bash ~/minix-src/port/build-all.sh
   ```
   Этапы и времена — в начале скрипта (на 24 ядрах около двух часов, из
   них тулчейн ~40 мин). Каждый этап можно перезапускать по имени
   (`build-all.sh xtools`, `… dirs`). **На хосте arm64 тулчейн ещё не
   собирали ни разу**: `port/build-xtools.sh` берёт исходники из
   `/usr/src/{binutils,gcc-15}` (пакеты выше, архитектурно-независимые) и
   накладывает `port/toolchain/*.patch`; привязки к x86 в нём нет, но если
   что-то упадёт — это первое место.
5. Деревья SDM660 (mainline для clover):
   ```
   cd ~ && git clone --depth 1 --filter=blob:none --sparse \
       https://github.com/sdm660-mainline/linux.git sdm660-linux
   cd sdm660-linux && git sparse-checkout set --no-cone \
       '/arch/arm64/boot/dts/qcom/sdm6*' '/arch/arm64/boot/dts/qcom/sda6*' \
       '/arch/arm64/boot/dts/qcom/pm660*' '/arch/arm64/boot/dts/qcom/pm8005*' \
       '/include/dt-bindings/' '/include/uapi/linux/input-event-codes.h'
   ```
   (`uapi` нужен: `dt-bindings/input/linux-event-codes.h` — ссылка туда.)
   Загружалось дерево с коммита `72bc4bb`.

## Цикл: правка → планшет

Внутри машины:
```
bash ~/minix-src/port/kbuild-variant.sh reloc "-O2"
KERNEL_ELF=~/obj-kvar/reloc/minix/kernel/kernel \
    bash ~/minix-src/port/clover/mkboot.sh /Users/<имя>/meecho/boot.img
```
`kbuild-variant.sh` собирает ядро в свой obj-каталог; `mkboot.sh` делает
`kernel.bin` и `boot.mba` (`ramimage.sh`), дерево `sdm660-xiaomi-clover`,
`Image.gz`+дерево и `boot.img` v0. Командная строка ядра — `CMDLINE=`
(по умолчанию `bootramdisk=1 console=console no_smp=1 verbose=3 bootwd=600`).
macOS видна из машины как `/Users/<имя>`; машина из macOS — как
`~/OrbStack/meecho/`.

На macOS, планшет в fastboot:
```
fastboot devices
fastboot boot ~/meecho/boot.img
```
(Из самой Linux-машины до macOS-команд дотягивается OrbStack-команда
`mac …`, например `mac fastboot boot /Users/<имя>/meecho/boot.img`.)

Проверить то же ядро на QEMU, сдвинутым и с консолью на «экране»:
```
KERNEL_ELF=~/obj-kvar/reloc/minix/kernel/kernel KERNEL_TEXT_OFFSET=0x8000 \
    bash ~/minix-src/port/test/fbcon/qemu-fbcon.sh /Users/<имя>/meecho/fbcon.png
```
(снимок буфера экрана монитором QEMU в PNG; `KERNEL_TEXT_OFFSET` у
`releasetools/evbarm64_ramimage.sh` кладёт тот же образ в другое место).

## Для Claude на Mac

- **`CLAUDE.md` в корне дерева описывает прежнее окружение**: Windows-хост,
  WSL, хаб, плата CB2, PowerShell. Его разделы о проекте, решениях и
  уроках в силе; команды вида `wsl -d Ubuntu -u minix -- bash -c '…'`
  здесь — `orb -m meecho -u minix bash -lc '…'` (или команда прямо в
  машине, если Claude запущен в ней), пути `\\wsl.localhost\…` —
  `~/OrbStack/meecho/home/minix/…`; разделы про хаб, COM-порты, ssh к CB2
  на Mac неприменимы.
- Договорённости, которые были в памяти прежней сессии:
  - **отвечать по-русски**; сообщения коммитов — по-английски, с «почему»
    и сортом правки (`[age]`, `[gap]`, `[docs]`, …), обоснование
    дублировать в `port/PORTING-LOG.md`;
  - **эталон — с этого железа**: прежде чем писать драйвер, снять, что
    делает работающая на нём система (здесь — LineageOS/postmarketOS), а
    не вспоминать по похожему;
  - **«не воспроизводится на эмуляторе»** — вывод, только если на
    эмуляторе запущено ровно то же самое;
  - **кэши, TLB, атрибуты памяти QEMU не проверяет** — такое считать
    непроверенным до железа;
  - коммитить по ходу, файлы — поимённо (дерево могут делить сессии);
    публикация — GitHub `valam84/meecho-os`.
