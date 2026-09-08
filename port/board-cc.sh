#!/bin/sh
# Собрать программу userland кросс-компилятором и положить её в работающую
# MEECHO на плате. Одна команда вместо пересборки образа корня: круг
# "поправил - увидел на плате" измерен и занимает около десяти секунд против
# пятнадцати минут.
#
#   board-cc.sh [-r] исходник.c [имя-на-плате]
#
#     -r  сразу запустить на плате и показать вывод
#
# Собирается статически против DESTDIR - как и весь userland порта: динамики
# пока нет (ld.elf_so компонуется, но exec не знает пути PT_INTERP).
#
# Результат кладётся в $OUTDIR (по умолчанию рядом с объектниками), а НЕ в
# DESTDIR - намеренно: файл в DESTDIR молча попадает в каждый следующий
# образ корня, хотя ни один Makefile его не собирает, и потом непонятно,
# откуда он там.
#
# Переменные окружения: XCC, XCFLAGS, DESTDIR, OUTDIR. Если сборка идёт в
# WSL, а скрипт запускается из Windows, задайте WSL_DISTRO и WSL_USER - тогда
# компилятор зовётся через wsl.exe, а готовый файл читается по пути UNC.
set -u

. "$(dirname "$0")/board-ssh.sh"

WSL_DISTRO=${WSL_DISTRO:-}
WSL_USER=${WSL_USER:-}

# Пути к тулчейну и DESTDIR - те, что видит СБОРКА. Когда она идёт в WSL, а
# скрипт запущен из Git Bash, $HOME здесь виндовый, и брать умолчания из
# него нельзя: mkdir уедет в профиль Windows. Поэтому для случая с WSL
# домашний каталог берётся оттуда.
if [ -n "$WSL_DISTRO" ]; then
	BHOME=/home/${WSL_USER:-$USER}
else
	BHOME=$HOME
fi
DEST=${DESTDIR:-$BHOME/dest-evbarm64}
XCC=${XCC:-$BHOME/xtools-aarch64/bin/aarch64-elf64-minix-gcc}
XCFLAGS=${XCFLAGS:--O -static -Wall -Wextra}
OUTDIR=${OUTDIR:-$BHOME/obj-evbarm64/work/cc}

run=0
while [ $# -gt 0 ]; do
	case "$1" in
	-r) run=1; shift ;;
	--) shift; break ;;
	-*) echo "usage: board-cc.sh [-r] исходник.c [имя]" >&2; exit 2 ;;
	*) break ;;
	esac
done

SRC=${1:-}
[ -n "$SRC" ] && [ -f "$SRC" ] || {
	echo "usage: board-cc.sh [-r] исходник.c [имя]" >&2
	exit 2
}
NAME=${2:-$(basename "$SRC" .c)}

if [ -n "$WSL_DISTRO" ]; then
	# Сборка в WSL, вызов из Windows. Исходник передаётся по содержимому, а
	# не по пути: так работает файл откуда угодно и не надо угадывать, виден
	# ли он через /mnt. Заодно снимается CRLF - на NTFS он приходит.
	#
	# Рабочие файлы кладём НЕ в /tmp: Git Bash переписывает "/tmp/..." в
	# строке для wsl.exe в виндовый временный каталог, и файл уезжает не
	# туда. У /home/... такого перевода нет.
	WTMP=/home/${WSL_USER:-$USER}/.board-cc
	wsl -d "$WSL_DISTRO" ${WSL_USER:+-u "$WSL_USER"} -- \
		bash -c "mkdir -p $WTMP && tr -d '\\r' > $WTMP/src.c" < "$SRC" || exit 1
	# Сборка тоже уходит файлом, а не строкой: кавычки через wsl.exe
	# теряются молча, а $? внутри такой строки всегда читается нулём - то
	# есть встроенная проверка статуса тихо утверждает, что всё хорошо.
	{
		echo "set -e"
		echo "mkdir -p '$OUTDIR'"
		echo "rm -f '$OUTDIR/$NAME'"
		echo "$XCC $XCFLAGS --sysroot='$DEST' -o '$OUTDIR/$NAME' $WTMP/src.c"
		echo "ls -l '$OUTDIR/$NAME'"
	} | wsl -d "$WSL_DISTRO" ${WSL_USER:+-u "$WSL_USER"} -- \
		bash -c "cat > $WTMP/build.sh" || exit 1
	wsl -d "$WSL_DISTRO" ${WSL_USER:+-u "$WSL_USER"} -- \
		bash "$WTMP/build.sh" 2>&1 | tr -d '\0'
	BIN=${UNC:-//wsl.localhost/$WSL_DISTRO$OUTDIR}/$NAME
else
	mkdir -p "$OUTDIR"
	rm -f "$OUTDIR/$NAME"
	"$XCC" $XCFLAGS --sysroot="$DEST" -o "$OUTDIR/$NAME" "$SRC" || exit 1
	ls -l "$OUTDIR/$NAME"
	BIN=$OUTDIR/$NAME
fi

[ -f "$BIN" ] || {
	echo "сборка не дала $BIN" >&2
	exit 1
}

sh "$(dirname "$0")/board-put.sh" -m 755 "$BIN" "/usr/bin/$NAME" || exit 1

if [ "$run" = 1 ]; then
	echo "--- /usr/bin/$NAME на плате ---"
	board_ssh "/usr/bin/$NAME" 2>&1 | tr -d '\r'
fi
