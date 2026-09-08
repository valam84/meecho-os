#!/bin/sh
# Собрать комплект файлов для SD-карты BIGTREETECH CB2.
#
# Своего загрузчика мы не строим: U-Boot для RK3566 требует проприетарных
# блобов rkbin (инициализация DDR и BL31), а вендорский уже проверен на этом
# железе. Поэтому карта готовится так: на неё пишется штатный образ BTT
# (Armbian), после чего на её FAT-раздел кладётся то, что собрано здесь.
# Вендорский boot.scr при этом заменяется нашим - Linux с этой карты больше не
# загрузится, для того и вторая карта.
#
#   mkcard.sh            собрать в ~/obj-evbarm64/work/card
#   mkcard.sh /mnt/d/... собрать и скопировать туда же
set -e
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}


W=${W:-$HOME/obj-evbarm64/work}
OUT=$W/card
SRC=${SRC:-$PORT/}

command -v mkimage >/dev/null || { echo "нет mkimage: sudo apt install u-boot-tools" >&2; exit 1; }
[ -f "$W/kernel.bin" ] || { echo "нет $W/kernel.bin — сначала ramimage.sh" >&2; exit 1; }
[ -f "$W/boot.mba" ]   || { echo "нет $W/boot.mba — сначала ramimage.sh" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/meecho"

cp "$W/kernel.bin" "$W/boot.mba" "$OUT/meecho/"

# Скрипт U-Boot: текст в boot.cmd, на карту идёт скомпилированный boot.scr.
sed 's/\r$//' "$SRC/cb2-boot.cmd" > "$OUT/boot.cmd"
mkimage -C none -A arm64 -T script -d "$OUT/boot.cmd" "$OUT/boot.scr" >/dev/null

# И второй скрипт — для вендорской карты, а не для этой. Вендорский boot.scr
# сам исполняет fixup.scr перед своим booti, так что одноразовую загрузку
# MEECHO можно повесить туда, ничего вендорского не трогая. Кладётся по ssh
# скриптом board-deploy.ps1; на карту MEECHO он не нужен и не мешает.
sed 's/\r$//' "$SRC/cb2-fixup.cmd" > "$OUT/fixup.cmd"
mkimage -C none -A arm64 -T script -d "$OUT/fixup.cmd" "$OUT/fixup.scr" >/dev/null

cat > "$OUT/README.txt" <<'EOF'
MEECHO на BIGTREETECH CB2 — что делать с этими файлами
======================================================

1. Записать на вторую (чистую) карту штатный образ BTT/Armbian для CB2 —
   тем же способом, каким записана рабочая карта. Нужен только его
   загрузчик; систему с неё мы не грузим.

2. Вставить карту в компьютер. Смонтируется FAT-раздел (тот, что на плате
   виден как /boot) — на нём лежат armbianEnv.txt, boot.scr, Image, dtb/.

3. Скопировать на этот раздел ровно две вещи:
     meecho/          (каталог целиком: kernel.bin и boot.mba)
     boot.scr         (заменив вендорский — можно сначала переименовать его
                       в boot.scr.vendor, чтобы вернуть загрузку Linux)

   boot.cmd и этот README на карту НЕ нужны: U-Boot читает только
   скомпилированный boot.scr. boot.cmd — его исходный текст, он здесь для
   чтения и правки (пересобрать: mkimage -C none -A arm64 -T script
   -d boot.cmd boot.scr).

   Каталог dtb/ трогать не надо: скрипт берёт оттуда
   dtb/rockchip/rk3566-bigtreetech-cb2.dtb, он уже там.

4. Вставить карту в плату, включить питание, смотреть консоль на 1500000 8N1.

Что должно появиться:

   MEECHO: loading from mmc 1:1
   MEECHO: booting, archive NNNNNNNN bytes
   Starting kernel ...
   MEECHO/aarch64: 4 cores, memory ...

Строка про память — это уже наше ядро: она означает, что консоль найдена
через device tree, DesignWare 8250 заработал и карта памяти разобрана.

Дальше должны пойти серверы, а за ними приглашение:

   Started VFS: 9 worker thread(s)
   Root device name is imgrd
   ...
   login:

Драйвер tty ищет консоль там же, где ядро, — /chosen/stdout-path через
/aliases, — и знает обе микросхемы: PL011 у QEMU и DesignWare у RK3566.
Вход в систему: root, без пароля.

Вернуть вендорскую загрузку: восстановить исходный boot.scr.
EOF

ls -l "$OUT" "$OUT/meecho"

if [ -n "$1" ]; then
	# Каталог наполняется, а не пересоздаётся: на /mnt/d удалить его самого
	# WSL не может ("Permission denied" на NTFS), и rm -rf уронил бы скрипт
	# уже после того, как вынес содержимое.
	mkdir -p "$1"
	cp -r "$OUT/." "$1/"
	echo "скопировано в $1"
fi
