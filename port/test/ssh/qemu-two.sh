#!/bin/bash
# Второе соединение к одному и тому же sshd: воспроизведение на QEMU.
#
# Прогоны на QEMU до сих пор проверяли слушателя, поднятого РУКАМИ с
#   -E файл -o LogLevel=DEBUG3
# а на плате отказывает тот, что поднят /etc/rc: без -E, то есть пишущий
# в syslog, и с уровнем по умолчанию.  rc корня на QEMU sshd не запускает
# вовсе, так что эта настройка на эмуляторе не проверялась ни разу.
# Скрипт поднимает её и входит снаружи (через virtio_net и lwip, не через
# loopback) столько раз, сколько сказано.
#
#   bash qemu-two.sh [-E] [-e] [-L уровень] [-n N]
#
#     -E     поднять слушателя с -E файлом и DEBUG3 (как на плате работал)
#     -e     только -E файл, уровень журнала не трогать
#     -L X   только -o LogLevel=X, журнал по-прежнему в syslog
#     -n N   сколько раз входить (по умолчанию 3)
#
# Четыре сочетания и нужны: у слушателя, который на плате отказывает, и у
# того, который работает, отличаются ОБА - и куда пишется журнал, и его
# уровень.  По одному за прогон, иначе снова получится бисекция, которая
# ничего не разделяет.
#
# Переменные: PORT (проброшенный порт хоста, 2222), QEMU_TIMEOUT.
set -u

PORT=${PORT:-2222}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}
TRIES=3
ELOG=""
EONLY=""
LEVEL=""

while [ $# -gt 0 ]; do
	case "$1" in
	-E) ELOG=1; shift ;;
	-e) EONLY=1; shift ;;
	-L) LEVEL=$2; shift 2 ;;
	-n) TRIES=$2; shift 2 ;;
	*) break ;;
	esac
done

WORK=/tmp/sshq
rm -rf "$WORK"; mkdir -p "$WORK"
LOG=$WORK/console.log
IN=$WORK/in
mkfifo "$IN"

# Ключ клиента переживает прогоны: образ диска тоже переживает, и
# authorized_keys в нём должен оставаться годным.
KEY=${KEY:-$HOME/.ssh/qemu_meecho}
if [ ! -f "$KEY" ]; then
	mkdir -p "$(dirname "$KEY")"
	ssh-keygen -q -t ed25519 -N '' -C meecho-qemu -f "$KEY" || exit 1
fi
PUB=$(cat "$KEY.pub")

# Пережившая прошлый прогон QEMU держит проброшенный порт, и новая молча
# отказывается его занять - "Could not set up host forwarding rule".
pkill -9 -f qemu-system-aarch64 2>/dev/null && sleep 2

echo "=== запуск QEMU (порт хоста $PORT -> 22 гостя) ==="
# stdbuf: вывод QEMU в файл идёт блоками по 4 КБ, и ожидание метки в журнале
# срабатывает через минуты после того, как метка напечатана - то есть
# расписание разъезжается с гостем.  Гнать построчно.
QEMU_HOSTFWD=$PORT QEMU_TIMEOUT=$QEMU_TIMEOUT \
	QEMU="stdbuf -o0 -e0 qemu-system-aarch64" \
	bash /home/minix/bin/ramimage.sh -d < "$IN" > "$LOG" 2>&1 &
QPID=$!
exec 9> "$IN"

# Строки идут в последовательный порт гостя, и входной буфер tty невелик:
# три длинные команды подряд без паузы он режет посередине (проверено -
# ssh-keygen получил обрубленный путь и напечатал usage).  Пауза после
# каждой строки, и строки короткие.
say() { printf '%s\n' "$*" >&9; sleep 1; }

# Ждать строку в журнале консоли.  Метки конца команды пишутся не echo, а
# printf: терминал отражает набранное, и echo нашёлся бы в журнале раньше,
# чем команда выполнилась.
wait_for() {
	local pat="$1" lim="${2:-120}" i=0
	while [ "$i" -lt "$lim" ]; do
		if grep -q -- "$pat" "$LOG" 2>/dev/null; then return 0; fi
		if ! kill -0 "$QPID" 2>/dev/null; then
			echo "!!! QEMU вышел, не дождавшись '$pat'"; return 1
		fi
		sleep 1; i=$((i + 1))
	done
	echo "!!! не дождались '$pat' за ${lim}s"
	return 1
}

# Метка ищется без якорей вовсе.  Слева терминал печатает приглашение "# ",
# справа консоль ставит CR перед LF - так что и "^d0", и "d0$" не находят
# ничего, а расписание молча ждёт таймаута на каждой метке.  Ложного
# совпадения не будет: сама команда содержит "d%s", а не "d0".
mark() { say "printf 'd%s\\n' $1"; wait_for "d$1" "${2:-90}"; }

wait_for "login:" 180 || { kill $QPID 2>/dev/null; exit 1; }
say root
sleep 3
say "export TERM=vt100 PAGER=cat"
mark 0 || true

say "ifconfig | grep 'inet '"
mark 1 || true

# Ключ хоста и ключ доступа.  Ключ хоста делается в госте, как на плате.
say "cd /etc/ssh"
say "echo '$PUB' > authorized_keys"
say "chmod 600 authorized_keys"
say "ssh-keygen -A"
mark 2 240 || true

say "cd /; ls -l /etc/ssh/"
mark 3 || true

# Без ключей - ровно так, как это делает /etc/rc на плате: без -E, в syslog,
# с уровнем по умолчанию.
ARGS=""
[ -n "$ELOG" ] && ARGS="-E /var/log/sshd.log -o LogLevel=DEBUG3"
[ -n "$EONLY" ] && ARGS="-E /var/log/sshd.log"
[ -n "$LEVEL" ] && ARGS="$ARGS -o LogLevel=$LEVEL"
say "/usr/sbin/sshd $ARGS && echo SSHD-UP"
mark 4 || true

echo "=== вход снаружи, $TRIES раз ==="
for i in $(seq 1 "$TRIES"); do
	echo "--- попытка $i ---"
	timeout 45 ssh -p "$PORT" -i "$KEY" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o BatchMode=yes -o ConnectTimeout=10 -vv \
		root@127.0.0.1 'uname -s; cat /proc/uptime; echo MARK-OK' \
		> "$WORK/try$i.out" 2> "$WORK/try$i.err"
	rc=$?
	echo "rc=$rc"
	cat "$WORK/try$i.out"
	grep -E "banner|timed out|Connection|remote software|Authenticated|debug1: Local version" \
		"$WORK/try$i.err" | tail -8
done

echo "=== состояние гостя после попыток ==="
say "ps ax | grep -v grep | grep sshd"
mark 5 || true
say "ls -l /var/run/ 2>/dev/null | head -20"
mark 6 || true

say "/sbin/poweroff"
sleep 8
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== хвост консоли ==="
tail -60 "$LOG"
echo
echo "журналы: $WORK"
