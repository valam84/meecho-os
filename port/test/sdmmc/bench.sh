#!/bin/sh
# Хранилище платы, измеренное с самой платы: число «до» и число «после».
#
# Веха 9 меняет способ обмена с eMMC (FIFO процессором -> DMA) и способ
# ожидания (опрос -> прерывание). «Стало быстрее» без числа - не результат,
# поэтому мерить надо одним и тем же скриптом до правки и после, на той же
# плате, с тем же корнем.
#
# Запускать по ssh из работающей MEECHO:
#
#	ssh плата 'sh /root/bench.sh'
#
# Меряется три разных пути, и это не избыточность:
#
#   raw	   чтение сырого устройства - блочный драйвер и ничего больше.
#	   Это то число, которое правка меняет напрямую.
#   write  запись файла плюс sync - тот же драйвер в обратную сторону, и
#	   с ним BDEV_FLUSH, то есть CMD6 FLUSH_CACHE у карты.
#   tar	   разворачивание архива - смесь данных и метаданных, то есть
#	   короткие обмены вперемешку с длинными. Файловая система здесь
#	   участвует, и это единственный из трёх, где видно, что даёт
#	   снятый потолок в четыре блока на обычной работе.
#
# Время берётся из /proc/uptime - у MINIX там одно число, секунды с
# загрузки. Часов на плате может не быть вовсе, поэтому date(1) для
# измерения не годится, а uptime годится всегда.
set -u

WORK=${WORK:-/root/bench}
MB=${MB:-32}		# сколько мегабайт читать за прогон
RUNS=${RUNS:-3}
TARSRC=${TARSRC:-/bin}

now() { cat /proc/uptime; }
el() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.2f", b - a }'; }
rate() { awk -v m="$1" -v a="$2" -v b="$3" 'BEGIN {
	d = b - a; if (d <= 0) d = 0.001; printf "%.2f", m / d }'; }

echo "meecho-storage-bench 1"
echo "uname: $(uname -a)"
echo "root: $(sysenv rootdevname 2>/dev/null || echo '?')"
echo "uptime-at-start: $(now)"
echo

# Чтение сырого устройства. Два размера запроса: 64 КБ - то, чем ходит
# файловая система, 1 МБ - то, что показывает потолок пути целиком.
#
# Каждый прогон читает СВОЙ участок устройства, и это не мелочь: первый
# замер этим скриптом дал 6.4 МБ/с на первом прогоне и 145 МБ/с на третьем,
# то есть третий не читал носитель вовсе - блоки уже лежали в кэше. Смещение
# сдвигается на длину прогона, так что ни один участок не читается дважды.
skip=0
for bs in 64k 1m; do
	case $bs in
	64k) cnt=$((MB * 16)); blk=$((1024 * 64)) ;;
	1m)  cnt=$MB;          blk=$((1024 * 1024)) ;;
	esac
	i=1
	while [ $i -le $RUNS ]; do
		off=$((skip * 1024 * 1024 / blk))
		t0=$(now)
		dd if=/dev/c0d0 of=/dev/null bs=$bs count=$cnt skip=$off \
		    >/dev/null 2>&1
		t1=$(now)
		echo "raw-read bs=$bs ${MB}MiB at ${skip}MiB run=$i" \
		    "$(el $t0 $t1)s $(rate $MB $t0 $t1) MiB/s"
		skip=$((skip + MB))
		i=$((i + 1))
	done
done
echo

# Запись файла и sync. sync здесь не украшение: без него измеряется скорость
# кэша файловой системы, а не носителя.
rm -rf "$WORK"
mkdir -p "$WORK" || exit 1
i=1
while [ $i -le $RUNS ]; do
	rm -f "$WORK/w.bin"
	sync
	t0=$(now)
	dd if=/dev/zero of="$WORK/w.bin" bs=64k count=$((MB * 16 / 2)) >/dev/null 2>&1
	sync
	t1=$(now)
	echo "write bs=64k $((MB / 2))MiB run=$i $(el $t0 $t1)s $(rate $((MB / 2)) $t0 $t1) MiB/s"
	i=$((i + 1))
done
rm -f "$WORK/w.bin"
sync
echo

# Архив: сначала собрать, потом развернуть. Собирается один раз - интересно
# разворачивание, у него на каждый файл идут метаданные.
rm -f "$WORK/t.tar"
tar cf "$WORK/t.tar" "$TARSRC" 2>/dev/null
sync
echo "tar-size: $(ls -l "$WORK/t.tar" | awk '{ print $5 }') bytes of $TARSRC"
i=1
while [ $i -le $RUNS ]; do
	rm -rf "$WORK/x"
	mkdir "$WORK/x"
	sync
	t0=$(now)
	(cd "$WORK/x" && tar xf "$WORK/t.tar")
	sync
	t1=$(now)
	echo "tar-extract run=$i $(el $t0 $t1)s"
	i=$((i + 1))
done

rm -rf "$WORK"
sync
echo
echo "uptime-at-end: $(now)"
