#!/bin/sh
# Положить файл в РАБОТАЮЩУЮ MEECHO на плате, по ssh.
#
#   board-put.sh [-m режим] локальный-файл [путь-на-плате]
#
# Путь по умолчанию - /usr/bin/<имя файла>. Режим по умолчанию берётся у
# локального файла: 755, если он исполняемый, иначе 644. Своя установка - в
# port/board.conf (образец: board.conf.example).
#
# Зачем это, если есть scp. Затем, что scp здесь работает не всегда и не у
# всех, и понять это можно только замером:
#
#   - без строки "Subsystem sftp" в sshd_config клиент OpenSSH 9.0 и новее
#     получает "subsystem request failed on channel 0": другого протокола он
#     не знает. Строка теперь есть, но в СТАРОМ образе корня её нет;
#   - клиент до 9.0 ходит старым протоколом scp и обходится одним
#     /usr/bin/scp на той стороне, так что "у меня scp работает" может
#     означать всего лишь, что у вас старый ssh.
#
# Этот путь - обычный ssh и cat - работает при любом ответе на тот вопрос.
# И делает две вещи, которых scp не делает: сверяет md5 (у "cat >" нет
# способа сообщить об усечении или полном диске) и ставит файл через
# временное имя и mv, потому что запись поверх работающей программы портит
# её на середине.
set -u

. "$(dirname "$0")/board-ssh.sh"

usage() {
	echo "usage: board-put.sh [-m режим] файл [путь-на-плате]" >&2
	exit 2
}

mode=""
while [ $# -gt 0 ]; do
	case "$1" in
	-m) mode=${2:-}; [ -n "$mode" ] || usage; shift 2 ;;
	--) shift; break ;;
	-*) usage ;;
	*) break ;;
	esac
done

LOCAL=${1:-}
[ -n "$LOCAL" ] && [ -f "$LOCAL" ] || usage
REMOTE=${2:-/usr/bin/$(basename "$LOCAL")}

if [ -z "$mode" ]; then
	if [ -x "$LOCAL" ]; then mode=755; else mode=644; fi
fi

board_require_meecho || exit 1

TMP="$REMOTE.put.$$"
board_ssh "cat > '$TMP'" < "$LOCAL" || {
	echo "не удалось передать $LOCAL" >&2
	exit 1
}

want=$(md5sum < "$LOCAL" | cut -d' ' -f1)
# NetBSD'шный md5 со стандартного ввода печатает только сумму, но берём
# последнее слово - на случай, если печатает не только её.
got=$(board_ssh "md5 < '$TMP'" 2>/dev/null | tr -d '\r' | awk '{print $NF}')

if [ "$want" != "$got" ]; then
	echo "суммы не сошлись: здесь $want, на плате $got" >&2
	board_ssh "rm -f '$TMP'" 2>/dev/null
	exit 1
fi

board_ssh "chmod $mode '$TMP' && mv '$TMP' '$REMOTE'" || {
	echo "не удалось поставить $REMOTE" >&2
	exit 1
}

echo "$LOCAL -> $BOARD:$REMOTE (режим $mode, md5 $want)"
