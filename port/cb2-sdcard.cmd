# MEECHO на BIGTREETECH CB2 с самостоятельной SD-карты: загрузчик, ядро и
# корень - всё на ней (port/mksdimg.sh). Компилируется в boot.scr первого
# раздела, как cb2-boot.cmd, и отличается от него в трёх местах:
#
#  - корень по умолчанию - второй раздел этой карты, /dev/c1d0p1. Гнездо SD
#    на CB2 - второй экземпляр sdmmc (распаянная eMMC нумеруется первой,
#    даже без системы на ней), а p1 у MINIX - второй раздел MBR;
#  - переключатели корня - файлы meecho/root_emmc (корень на eMMC, c0d0) и
#    meecho/root_ramdisk (корень в памяти: спасательная загрузка);
#  - сторож загрузки bootwd по умолчанию выключен. На вендорской карте он
#    возвращал плату в Armbian; здесь возвращать некуда, и сброс через
#    пятнадцать минут работы был бы просто перезагрузкой. От зависания
#    остаётся сторож SoC (hwwd, ядро взводит его само). Файл meecho/bootwd
#    включает старое поведение.
#
# Адреса - те же, что в cb2-boot.cmd, и по той же причине (карта памяти
# живой платы): ядро 0x40200000, архив 0x48000000, дерево 0x4a000000.

if test -z "${devnum}"; then setenv devnum 1; fi
if test -z "${distro_bootpart}"; then setenv distro_bootpart 1; fi

if test -e mmc ${devnum}:${distro_bootpart} meecho/slow_console; then
	echo "MEECHO: switching console to 115200"
	setenv baudrate 115200
fi

if test -e mmc ${devnum}:${distro_bootpart} meecho/root_ramdisk; then
	setenv bootargs "bootramdisk=1 console=tty00"
	echo "MEECHO: root in memory (meecho/root_ramdisk present)"
elif test -e mmc ${devnum}:${distro_bootpart} meecho/root_emmc; then
	setenv bootargs "rootdevname=c0d0 blkdrv=sdmmc console=tty00"
	echo "MEECHO: root on eMMC (meecho/root_emmc present)"
else
	setenv bootargs "rootdevname=c1d0p1 blkdrv=sdmmc console=tty00"
	echo "MEECHO: root on this card, c1d0p1"
fi

if test -e mmc ${devnum}:${distro_bootpart} meecho/no_smp; then
	setenv bootargs "${bootargs} no_smp=1"
	echo "MEECHO: single CPU (meecho/no_smp present)"
fi

if test -e mmc ${devnum}:${distro_bootpart} meecho/verbose; then
	setenv bootargs "${bootargs} verbose=3"
	echo "MEECHO: verbose boot (meecho/verbose present)"
fi

if test -e mmc ${devnum}:${distro_bootpart} meecho/bootwd; then
	setenv bootargs "${bootargs} bootwd=900"
	echo "MEECHO: boot watchdog 900s (meecho/bootwd present)"
fi

echo "MEECHO: loading from mmc ${devnum}:${distro_bootpart}"

load mmc ${devnum}:${distro_bootpart} 0x40200000 meecho/kernel.bin
load mmc ${devnum}:${distro_bootpart} 0x48000000 meecho/boot.mba
setenv meecho_mba_size ${filesize}
load mmc ${devnum}:${distro_bootpart} 0x4a000000 dtb/rockchip/rk3566-bigtreetech-cb2.dtb

echo "MEECHO: booting, archive ${meecho_mba_size} bytes"

booti 0x40200000 0x48000000:${meecho_mba_size} 0x4a000000

echo "MEECHO: booti returned - kernel not started"
