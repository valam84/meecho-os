#!/bin/bash
# Стенд monpair до и после первого входа по ssh.
#
# monpair повторяет фигуру «монитор и его ребёнок» без sshd: socketpair +
# труба, fork, ребёнок через exec пишет запрос в сокет, родитель ждёт
# poll(-1) на сокете и трубе.  Если после одного входа по ssh стенд начнёт
# зависать, значит ломается система, а не sshd, и дальше искать надо в
# VFS/uds, а не в OpenSSH.
#
# Образ диска пересобирается (DISK_FRESH=1): monpair кладётся в DESTDIR и
# попадает в корень только так.
set -u

QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}
PORT=${PORT:-2222}

WORK=/tmp/monp
rm -rf "$WORK"; mkdir -p "$WORK"
LOG=$WORK/console.log
IN=$WORK/in
mkfifo "$IN"

KEY=${KEY:-$HOME/.ssh/qemu_meecho}
[ -f "$KEY" ] || { echo "нет ключа $KEY"; exit 1; }
PUB=$(cat "$KEY.pub")

pkill -9 -f qemu-system-aarch64 2>/dev/null && sleep 2

DISK_FRESH=${DISK_FRESH:-1} QEMU_HOSTFWD=$PORT QEMU_TIMEOUT=$QEMU_TIMEOUT \
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
mark() { say "printf 'd%s\\n' $1"; wait_for "d$1" "${2:-120}"; }

wait_for "login:" 300 || { kill $QPID 2>/dev/null; exit 1; }
say root
sleep 3
say "export TERM=vt100 PAGER=cat"
mark 0 || true

# До всякого ssh.
say "monpair 4"
say "monpair -one 4"
say "monpair -pipe"
say "monpair -loop 4"
mark 1 180 || true

say "cd /etc/ssh"
say "echo '$PUB' > authorized_keys"
say "chmod 600 authorized_keys"
say "ssh-keygen -A"
say "cd /"
mark 2 300 || true

say "/usr/sbin/sshd && echo UP"
mark 3 || true

echo "--- вход 1 ---"
timeout 45 ssh -p "$PORT" -i "$KEY" -o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null -o BatchMode=yes \
	root@127.0.0.1 'uname -s; echo ATTEMPT-OK' > "$WORK/a1.out" 2>&1
echo "rc=$?"; tail -2 "$WORK/a1.out"

# После одного входа.
say "monpair 4"
say "monpair -one 4"
say "monpair -pipe"
say "monpair -loop 4"
mark 4 180 || true

echo "--- вход 2 ---"
timeout 45 ssh -p "$PORT" -i "$KEY" -o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null -o BatchMode=yes \
	root@127.0.0.1 'uname -s; echo ATTEMPT-OK' > "$WORK/a2.out" 2>&1
echo "rc=$?"; tail -2 "$WORK/a2.out"

say "monpair 4"
say "monpair -one 4"
say "monpair -pipe"
say "monpair -loop 4"
mark 5 180 || true

say "/sbin/poweroff"
sleep 8
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== консоль от первого monpair ==="
sed -n '/monpair 4/,$p' "$LOG"
