#!/bin/sh
# Один прогон MEECHO на плате целиком, из Git Bash на Windows.
#
# Плата и «хаб» - разные машины: USB-TTL воткнут в хаб, а флаг одноразовой
# загрузки и файлы лежат на самой плате, где пока живёт вендорский Linux.
# Значит в прогоне участвуют два ssh, и порядок между ними важен: консоль
# должна слушать ДО того, как плата уйдёт в перезагрузку, иначе начало
# загрузки не попадёт в журнал - а именно там печатается всё интересное про
# драйвер.
#
#   board-cycle.sh [-e|-E] [-s|-S] [-w|-l|-W] [-n] расписание.txt [журнал]
#
#     -e   взвести meecho/root_emmc (корень на eMMC)
#     -E   снять meecho/root_emmc (корень - ramdisk)
#     -s   снять meecho/no_smp (грузиться на всех ядрах)
#     -S   взвести meecho/no_smp (грузиться на одном ядре)
#     -w   сторож загрузки как обычно, 900 с (снять оба флага)
#     -l   сторож на час: под работу руками по ssh, спасение остаётся
#     -W   сторож снят совсем - ОПАСНО, см. ниже
#     -n   не перезагружать: плата уже в MEECHO, просто взять консоль
#
# Без -e и -E переключатель корня не трогается, без -s и -S - переключатель
# ядер, без -w/-l/-W - сторож. Все они сделаны ключами, а не отдельной
# командой по ssh, по одной причине: отдельная команда уходит в ту систему,
# которая сейчас на плате, а флаг живёт в /boot вендорской - промахнуться
# этим способом уже случалось.
#
# Про -W отдельно. Сторож - единственное, что возвращает плату, если MEECHO
# не дошла до приглашения или зависла: одноразовый флаг meecho.go загрузчик
# снимает сам, так что ПЕРЕЗАГРУЗКА вернёт вендорскую систему, но зависшая
# не перезагрузится. Со снятым сторожем возврат - человек у выключателя, а
# плата может стоять не там, где вы. Поэтому для интерактивной работы есть
# -l (час), а -W оставлен на случай, когда часа действительно мало, и
# снимать его надо тем же скриптом с -w сразу после.
#
# Запуск консоли живёт отдельным файлом на хабе (/root/run-sched.sh): та же
# строка, переданная через ssh, обрастает кавычками быстрее, чем читается, и
# отказ в ней виден только тем, что ничего не произошло.
set -u

# Плата и консольная машина адресуются отдельно: у платы может не быть
# прямого маршрута снаружи, и тогда до неё ходят прыжком через ту же
# консольную машину (~/.ssh/config: ProxyJump). Имена задаются в board.conf.

# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес консольной машины и ключ у каждого свои. Образец рядом:
# board.conf.example.
__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
BOARD=${BOARD:-cb2-lan}
BOARD_PORT=${BOARD_PORT:-22}
HUB=${HUB:?port/board.conf: HUB=root@консольная-машина (см. board.conf.example)}
HUB_PORT=${HUB_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}
TTY=${TTY:-/dev/ttyUSB0}
BAUD=${BAUD:-1500000}
WAIT_MAX=${WAIT_MAX:-1800}

SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

# К плате - без проверки ключа хоста, и это не небрежность. Адрес у платы
# один на две системы, а ключ хоста MEECHO делается на первой загрузке и
# живёт до следующей записи корня, так что known_hosts на этом адресе
# означает лишь "чей ключ записался последним": один прогон MEECHO - и ssh
# отказывается ходить в Armbian, через который и ставится следующая сборка.
BOARD_SSH="$SSH -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no"

root_emmc=""
no_smp=""
bootwd=""
reboot=1
while [ $# -gt 0 ]; do
	case "$1" in
	-e) root_emmc=on; shift ;;
	-E) root_emmc=off; shift ;;
	-s) no_smp=off; shift ;;
	-S) no_smp=on; shift ;;
	-w) bootwd=short; shift ;;
	-l) bootwd=long; shift ;;
	-W) bootwd=off; shift ;;
	-n) reboot=0; shift ;;
	*) break ;;
	esac
done

SCHED=${1:-}
LOG=${2:-./board-run.log}
[ -n "$SCHED" ] && [ -f "$SCHED" ] || {
	echo "usage: board-cycle.sh [-e|-E] [-s|-S] [-w|-l|-W] [-n] расписание.txt [журнал]" >&2
	exit 2
}

# Адрес платы занимают обе её системы - вендорский Armbian и MEECHO, - а флаг
# загрузки и файлы лежат в /boot вендорской. Ошибиться здесь дёшево и обидно:
# команда уходит в MEECHO, каталога /boot там нет вовсе, а reboot из неё
# возвращает плату в Linux и стирает прогон, который в этот момент шёл.
# Поэтому сначала спрашиваем, кто отвечает, и только потом трогаем /boot.
if [ "$reboot" = 1 ]; then
	sys=$($BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'uname -s' 2>/dev/null)
	[ "$sys" = Linux ] || {
		echo "на $BOARD отвечает '$sys', а не Linux: плата уже в MEECHO?" >&2
		exit 1
	}
fi

case "$root_emmc" in
on)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'touch /boot/meecho/root_emmc; sync; echo "root_emmc: on"' || exit 1 ;;
off)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'rm -f /boot/meecho/root_emmc; sync; echo "root_emmc: off"' || exit 1 ;;
esac

case "$no_smp" in
on)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'touch /boot/meecho/no_smp; sync; echo "no_smp: on"' || exit 1 ;;
off)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'rm -f /boot/meecho/no_smp; sync; echo "no_smp: off"' || exit 1 ;;
esac

# Сторож - три состояния и два файла, поэтому каждая ветка ставит оба, а не
# только свой: иначе оставленный с прошлого раза no_bootwd переживёт -l и
# тихо отменит его.
case "$bootwd" in
short)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'rm -f /boot/meecho/no_bootwd /boot/meecho/bootwd_long; sync; echo "bootwd: 900s"' || exit 1 ;;
long)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'rm -f /boot/meecho/no_bootwd; touch /boot/meecho/bootwd_long; sync; echo "bootwd: 3600s"' || exit 1 ;;
off)	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'touch /boot/meecho/no_bootwd; sync; echo "bootwd: OFF - вернуть плату сможет только человек у выключателя"' || exit 1 ;;
esac

$SSH "$HUB" -p "$HUB_PORT" 'cat > /tmp/sched.txt' < "$SCHED" || exit 1
$SSH "$HUB" -p "$HUB_PORT" "sh /root/run-sched.sh $TTY $BAUD" || exit 1

if [ "$reboot" = 1 ]; then
	$BOARD_SSH "$BOARD" -p "$BOARD_PORT" 'touch /boot/meecho.go && sync && (nohup sh -c "sleep 1; reboot" >/dev/null 2>&1 &) ; echo REBOOTING' || exit 1
fi

echo "ждём расписание..." >&2
waited=0
while [ $waited -lt $WAIT_MAX ]; do
	if $SSH "$HUB" -p "$HUB_PORT" 'test -f /tmp/run.done' 2>/dev/null; then
		break
	fi
	sleep 10
	waited=$((waited + 10))
done
[ $waited -lt $WAIT_MAX ] || echo "!!! расписание не кончилось за ${WAIT_MAX}s" >&2

$SSH "$HUB" -p "$HUB_PORT" "cat /tmp/run.err; echo '--- журнал ---'; tr -d '\000' < /tmp/run.log" > "$LOG" 2>&1
sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$LOG"
