#!/bin/bash
# Что именно ломается после первого соединения по ssh.
#
# Известно (qemu-two.sh, qemu-fresh.sh): после одного успешного входа
# следующий не получает баннера, и это касается ЛЮБОГО слушателя, включая
# только что поднятый.  То есть испорчено состояние системы, а не sshd.
#
# Этот прогон снимает три вещи об уже испорченной системе:
#   1. журнал самого слушателя (-E файл, уровень по умолчанию: при DEBUG3
#      отказ не воспроизводится, так что журнал приходится брать на том
#      уровне, на котором он и происходит);
#   2. работает ли select на сокетах ДРУГОГО драйвера - ping ходит через
#      lwip, а не через uds;
#   3. таблицу процессов и застрявшие пары.
set -u

QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}
PORT=${PORT:-2222}
TRIES=${TRIES:-3}
LEVEL=${LEVEL:-}

WORK=/tmp/sshd-diag
rm -rf "$WORK"; mkdir -p "$WORK"
LOG=$WORK/console.log
IN=$WORK/in
mkfifo "$IN"

KEY=${KEY:-$HOME/.ssh/qemu_meecho}
[ -f "$KEY" ] || { echo "нет ключа $KEY - сначала qemu-two.sh"; exit 1; }
PUB=$(cat "$KEY.pub")

pkill -9 -f qemu-system-aarch64 2>/dev/null && sleep 2

QEMU_HOSTFWD=$PORT QEMU_TIMEOUT=$QEMU_TIMEOUT \
	bash /home/minix/bin/ramimage.sh -d < "$IN" > "$LOG" 2>&1 &
QPID=$!
exec 9> "$IN"

say() { printf '%s\n' "$*" >&9; sleep 1; }
wait_for() {
	local pat="$1" lim="${2:-120}" i=0
	while [ "$i" -lt "$lim" ]; do
		grep -q -- "$pat" "$LOG" 2>/dev/null && return 0
		kill -0 "$QPID" 2>/dev/null || { echo "!!! QEMU вышел"; return 1; }
		sleep 1; i=$((i + 1))
	done
	echo "!!! не дождались '$pat'"; return 1
}
mark() { say "printf 'd%s\\n' $1"; wait_for "d$1" "${2:-90}"; }

wait_for "login:" 240 || { kill $QPID 2>/dev/null; exit 1; }
say root
sleep 3
say "export TERM=vt100 PAGER=cat"
mark 0 || true

say "cd /etc/ssh"
say "echo '$PUB' > authorized_keys"
say "chmod 600 authorized_keys"
say "cd /; rm -f /var/log/s.log"
mark 1 || true

say "/usr/sbin/sshd -E /var/log/s.log ${LEVEL:+-o LogLevel=$LEVEL} && echo UP"
mark 2 || true

# Пинг ДО соединений: чтобы было с чем сравнивать.
say "ping -c 2 10.0.2.2"
mark 3 120 || true

n=0
while [ "$n" -lt "$TRIES" ]; do
	n=$((n + 1))
	echo "--- вход $n ---"
	timeout 45 ssh -p "$PORT" -i "$KEY" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o BatchMode=yes -o ConnectTimeout=10 \
		root@127.0.0.1 'uname -s; cat /proc/uptime; echo ATTEMPT-OK' \
		> "$WORK/a$n.out" 2> "$WORK/a$n.err"
	echo "rc=$?"; cat "$WORK/a$n.out"
	tail -2 "$WORK/a$n.err"
done

# Тот же пинг ПОСЛЕ: если он всё ещё ходит, значит select на сокетах lwip
# жив, и сломано что-то, что касается только локальных сокетов.
say "ping -c 2 10.0.2.2"
mark 4 120 || true
say "ps ax | grep -v grep | grep sshd"
mark 5 || true
say "cat /var/log/s.log"
mark 6 || true

say "/sbin/poweroff"
sleep 8
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== консоль ==="
sed -n '/d2/,$p' "$LOG"
