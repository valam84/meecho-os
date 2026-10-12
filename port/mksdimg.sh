#!/bin/sh
# Полный образ SD-карты MEECHO для BIGTREETECH CB2: записать на карту, вставить
# в плату, включить - и всё, от загрузчика до корня, на этой карте.
#
#   mksdimg.sh [выход.img]       по умолчанию ~/obj-evbarm64/work/meecho-cb2-sd.img
#
# Раскладка, в секторах по 512 байт:
#
#   0               MBR: p1 FAT32 LBA (0x0c, активный) с 32768, 256 МиБ;
#                   p2 MINIX (0x81) сразу за ним, во весь корень
#   64 .. 32767     загрузчик вендорской карты BTT: idbloader с 64 (DDR, SPL)
#                   и u-boot.itb с 16384 (TF-A, U-Boot), как его кладёт Armbian
#   p1 (FAT)        boot.scr (port/cb2-sdcard.cmd), meecho/kernel.bin,
#                   meecho/boot.mba, dtb/rockchip/rk3566-bigtreetech-cb2.dtb
#   p2 (MFS V4)     корень, тот же, что mkroot.sh делает для eMMC
#
# Корень на плате - /dev/c1d0p1: гнездо SD - второй экземпляр sdmmc после
# распаянной eMMC, второй раздел MBR у MINIX - p1. Это и пишет boot.scr.
#
# Свой загрузчик не собирается (rkbin, блобы DDR) - берётся проверенный на
# этом железе, с вендорской карты: port/cb2-sd/uboot-area.bin - сектора
# 64..32767 этой карты, и вендорское дерево устройств. В git их нет (GPL и
# двоичные блобы Rockchip); снять заново - port/cb2-sd/README.md.
#
# Нужны: собранные kernel.bin и boot.mba (ramimage.sh -b), корень
# (mkroot.sh - его зовёт этот скрипт, если ROOT не задан), mkimage, mtools,
# dosfstools, sfdisk, xz.
set -eu
PORT=$(cd "$(dirname "$0")" && pwd)
OBJ=${OBJ:-$HOME/obj-evbarm64}
WORK=${WORK:-$OBJ/work}
OUT=${1:-$WORK/meecho-cb2-sd.img}
UBOOT=${UBOOT:-$PORT/cb2-sd/uboot-area.bin}
DTB=${DTB:-$PORT/cb2-sd/rk3566-bigtreetech-cb2.dtb}
ROOT=${ROOT:-}
FAT_START=32768
FAT_SECTORS=524288		# 256 МиБ, как у вендорской карты

for f in "$WORK/kernel.bin" "$WORK/boot.mba" "$UBOOT" "$DTB"; do
	[ -f "$f" ] || { echo "$0: нет $f" >&2; exit 1; }
done
# Сектора 64..32767: начало - заголовок idbloader, на 16384 - FIT u-boot.itb.
[ "$(stat -c %s "$UBOOT")" -eq $(((32768 - 64) * 512)) ] || {
	echo "$0: $UBOOT не 16 МиБ без 32 КиБ - не та область" >&2; exit 1; }
[ "$(head -c 4 "$UBOOT")" = RKNS ] || {
	echo "$0: в начале $UBOOT нет RKNS - это не idbloader" >&2; exit 1; }

if [ -z "$ROOT" ]; then
	sh "$PORT/mkroot.sh"
	ROOT=$WORK/root-emmc.img
fi
[ -f "$ROOT" ] || { echo "$0: нет корня $ROOT" >&2; exit 1; }
ROOT_SECTORS=$(($(stat -c %s "$ROOT") / 512))
ROOT_START=$((FAT_START + FAT_SECTORS))

T=$WORK/sdimg
rm -rf "$T"
mkdir -p "$T"

# Первый раздел: FAT32, его U-Boot (bootstd) читает первым и находит boot.scr.
mkimage -C none -A arm64 -T script -d "$PORT/cb2-sdcard.cmd" "$T/boot.scr" \
	> /dev/null
mkfs.vfat -F 32 -n MEECHO -C "$T/fat.img" $((FAT_SECTORS / 2)) > /dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i "$T/fat.img" ::meecho ::dtb ::dtb/rockchip
mcopy -i "$T/fat.img" "$T/boot.scr" ::boot.scr
mcopy -i "$T/fat.img" "$PORT/cb2-sdcard.cmd" ::boot.cmd
mcopy -i "$T/fat.img" "$WORK/kernel.bin" "$WORK/boot.mba" ::meecho/
mcopy -i "$T/fat.img" "$DTB" ::dtb/rockchip/rk3566-bigtreetech-cb2.dtb
cat > "$T/README.txt" <<'END'
MEECHO for the BIGTREETECH CB2 - boot partition of a self-contained card.

boot.scr loads meecho/kernel.bin and meecho/boot.mba and starts MEECHO with
its root on the second partition of this card (/dev/c1d0p1). boot.cmd is
its source. Switches - create an empty file in meecho/:

  root_ramdisk   root in memory (rescue: the card's root is not touched)
  root_emmc      root on the board's eMMC (c0d0) instead of this card
  no_smp         one CPU instead of four
  verbose        the kernel says what it is initialising
  bootwd         reset after 15 minutes (for unattended test runs)
  slow_console   console at 115200 instead of 1500000 (U-Boot then waits
                 for Enter on the console after switching)

The console is uart2 at 1500000 8N1. root has no password on the console;
ssh accepts the keys in /etc/ssh/authorized_keys of the root partition.
END
mcopy -i "$T/fat.img" "$T/README.txt" ::README.txt

# Карта целиком.
rm -f "$OUT"
truncate -s $(((ROOT_START + ROOT_SECTORS) * 512)) "$OUT"
sfdisk -q "$OUT" <<END
label: dos
label-id: 0x4d454543
unit: sectors

start=$FAT_START, size=$FAT_SECTORS, type=c, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=81
END
dd if="$UBOOT" of="$OUT" bs=512 seek=64 conv=notrunc status=none
dd if="$T/fat.img" of="$OUT" bs=512 seek=$FAT_START conv=notrunc status=none
dd if="$ROOT" of="$OUT" bs=512 seek=$ROOT_START conv=notrunc status=none

# Проверки того, что ошибиться здесь можно молча.
#
# MINIX читает внутри раздела 0x81 вложенную таблицу, если в конце первого
# сектора стоит 0x55aa; у корня MFS там должны быть нули, иначе c1d0p1
# разлетится на мусорные подразделы.
sig=$(dd if="$OUT" bs=1 skip=$((ROOT_START * 512 + 510)) count=2 \
	status=none | od -An -tx1 | tr -d ' ')
[ "$sig" != 55aa ] || { echo "$0: в начале корня сигнатура MBR" >&2; exit 1; }
sfdisk -d "$OUT"
mdir -i "$OUT@@$((FAT_START * 512))" -/ :: | grep -v "^$"

echo "сжимаю..."
rm -f "$OUT.xz"
xz -T0 -6 -k "$OUT"
( cd "$(dirname "$OUT")" && sha256sum "$(basename "$OUT")" \
	"$(basename "$OUT").xz" > "$(basename "$OUT").sha256" )
ls -l "$OUT" "$OUT.xz"
cat "$OUT.sha256"
