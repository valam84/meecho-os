# Соединение с РАБОТАЮЩЕЙ MEECHO на плате. Не запускается сам - его
# подключают через "." скрипты board-put.sh и board-cc.sh.
#
# Своя установка описывается в port/board.conf - его нет в репозитории,
# потому что адрес платы и ключ у каждого свои. Образец рядом:
# board.conf.example.
#
# В отличие от board-cycle.sh, который говорит с вендорским Linux на плате,
# здесь собеседник - сама MEECHO. Адрес у них общий, поэтому обе стороны
# спрашивают, кто отвечает, прежде чем что-то делать.

# Git Bash переписывает аргументы, похожие на пути Unix, в виндовые, когда
# зовёт не-MSYS программу - а ssh.exe и wsl.exe именно такие. Команда
# "/usr/bin/monpair" уехала бы на плату как "C:/Program Files/Git/usr/bin/
# monpair", и это уже стоило одного прогона.
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV

__d=$(dirname "$0")
[ -f "$__d/board.conf" ] && . "$__d/board.conf"
[ -f "$__d/../board.conf" ] && . "$__d/../board.conf"
BOARD=${BOARD:-cb2-lan}
BOARD_PORT=${BOARD_PORT:-22}
KEY=${KEY:?port/board.conf: KEY=путь к ssh-ключу (см. board.conf.example)}

# Ключ хоста не проверяется, и это не небрежность: ключ MEECHO делается на
# первой загрузке и живёт до следующей записи корня, так что known_hosts на
# этом адресе означает лишь "чей ключ записался последним" - один прогон, и
# ssh откажется ходить в вендорский Linux, через который ставится следующая
# сборка.
board_ssh() {
	ssh -o BatchMode=yes -o ConnectTimeout=15 \
	    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	    -o IdentitiesOnly=yes -i "$KEY" -p "$BOARD_PORT" \
	    "$BOARD" "$@"
}

# Отказаться громко, если на том конце не MEECHO.
#
# Зеркальная ловушка: board-cycle.sh перед тем, как трогать /boot, требует
# ответа Linux - адрес у платы один на две системы, и промахнуться уже
# случалось. Здесь наоборот: писать и запускать надо только в MEECHO.
board_require_meecho() {
	sys=$(board_ssh 'uname -s' 2>/dev/null | tr -d '\r')
	[ "$sys" = MEECHO ] && return 0
	echo "на $BOARD отвечает '$sys', а не MEECHO: плата ещё в вендорском Linux?" >&2
	return 1
}
