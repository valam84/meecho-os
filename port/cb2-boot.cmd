# MEECHO на BIGTREETECH CB2 — скрипт загрузки для U-Boot.
#
# Кладётся на FAT-раздел карты (тот, что вендорская система монтирует как
# /boot) скомпилированным в boot.scr. U-Boot этой сборки ходит через bootstd:
# в журнале загрузки видно «Booting bootflow 'mmc@fe2b0000.bootdev.part_1'
# with script», то есть скрипт с первого раздела первой mmc и есть точка,
# через которую можно взять управление, не входя в приглашение U-Boot. Войти в
# него всё равно нельзя: окружение этой сборки — «Environment from nowhere»,
# autoboot без задержки, и прервать его нечем.
#
# Адреса выбраны по карте памяти живой платы (/proc/iomem вендорской
# системы): RAM с 0x00200000 по 0x7fffffff, первые два мегабайта у TF-A,
# резервы CMA и OP-TEE начинаются с 0x6b600000. Всё, что грузится ниже
# 0x50000000, лежит в свободной середине.
#
#   0x40200000  ядро — ровно _kern_phys_base, под который оно скомпоновано;
#               заголовок образа несёт flags bit 3, поэтому U-Boot оставит
#               его здесь, а не перенесёт в начало RAM
#   0x48000000  загрузочный архив (~11 МБ) как initrd
#   0x4a000000  device tree платы

# Скорость консоли переключается наличием файла, как и однопроцессорный режим.
#
# Плата печатает на 1500000 - это 150 КБ/с сплошным потоком, и проброс USB по
# сети столько не вывозит: в логах видно, как строки обрываются посреди чисел и
# склеиваются со следующими. На 115200 поток в тринадцать раз тоньше.
#
# U-Boot после смены скорости печатает «press ENTER» и ждёт возврата каретки:
# терминал должен слать его сам (board-console.ps1 -Poke). Без этого загрузка
# остановится здесь - поэтому переключатель и сделан файлом, чтобы убрать его
# было так же просто, как поставить.
if test -e mmc ${devnum}:${distro_bootpart} meecho/slow_console; then
	echo "MEECHO: switching console to 115200"
	setenv baudrate 115200
fi

setenv bootargs "bootramdisk=1 console=tty00"

# Однопроцессорный режим переключается наличием файла, а не пересборкой
# скрипта: положить meecho/no_smp — грузиться на одном ядре, убрать — на всех.
# Файл когда-то стоял по умолчанию, и объяснение при нём («ядро встаёт посреди
# баннера с того прогона, где поднялись вторичные ядра») оказалось неверным:
# тот симптом был лавиной прерываний UART, и no_smp его не менял.
#
# С 2026-09-08 SMP на плате проверен и работает: четыре ядра поднимаются и
# берут работу (PORTING-LOG.md, веха 8.0.3). Файла здесь по умолчанию нет, а
# переключатель остаётся — но как инструмент замера, а не предосторожность:
# сравнивать параллельную работу можно только с той же самой машиной на одном
# ядре, и другого способа её получить нет.
if test -e mmc ${devnum}:${distro_bootpart} meecho/no_smp; then
	setenv bootargs "${bootargs} no_smp=1"
	echo "MEECHO: single CPU (meecho/no_smp present)"
fi

# Подробная загрузка: ядро печатает, какой образ оно инициализирует и на каком
# шаге находится. Нужно тогда, когда система замолчала после баннера и
# неизвестно, докуда она дошла.
if test -e mmc ${devnum}:${distro_bootpart} meecho/verbose; then
	setenv bootargs "${bootargs} verbose=3"
	echo "MEECHO: verbose boot (meecho/verbose present)"
fi

# Сторож загрузки: ядро само сбросит машину через это число секунд. Плата
# стоит не здесь, и загрузка, не дошедшая до приглашения, иначе оставляет её
# мёртвой до человека.
if test -e mmc ${devnum}:${distro_bootpart} meecho/no_bootwd; then
	echo "MEECHO: boot watchdog off (meecho/no_bootwd present)"
else
	setenv bootargs "${bootargs} bootwd=900"
fi

# bootstd задаёт эти переменные само; значения на случай, если скрипт
# запустили иначе - в журнале платы это mmc 1:1.
if test -z "${devnum}"; then setenv devnum 1; fi
if test -z "${distro_bootpart}"; then setenv distro_bootpart 1; fi

echo "MEECHO: loading from mmc ${devnum}:${distro_bootpart}"

load mmc ${devnum}:${distro_bootpart} 0x40200000 meecho/kernel.bin
load mmc ${devnum}:${distro_bootpart} 0x48000000 meecho/boot.mba
setenv meecho_mba_size ${filesize}
load mmc ${devnum}:${distro_bootpart} 0x4a000000 dtb/rockchip/rk3566-bigtreetech-cb2.dtb

echo "MEECHO: booting, archive ${meecho_mba_size} bytes"

booti 0x40200000 0x48000000:${meecho_mba_size} 0x4a000000

# Сюда управление попадает, только если booti отказал.
echo "MEECHO: booti returned - kernel not started"
