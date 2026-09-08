#!/bin/bash
# Свежий слушатель против того, который уже обслужил соединение.
#
# qemu-two.sh показал, что второе соединение к одному и тому же sshd не
# получает баннера и на QEMU - если слушатель поднят так, как его поднимает
# /etc/rc платы (уровень журнала по умолчанию).  Следующий вопрос ровно
# один: испорчено состояние ЭТОГО слушателя или всей системы?
#
# Поэтому здесь ДВА слушателя, оба подняты до первого соединения и оба без
# ключей: порт 22 и порт 2223 гостя.  Порядок входов - 22, 22, 2223, 2223,
# 22.  Если третий вход (первый к свежему слушателю) пройдёт, испорчен
# слушатель; если не пройдёт - испорчена система.
#
#   bash qemu-fresh.sh
#
# Переменные: QEMU_TIMEOUT.  Порты хоста 2222 -> 22 гостя и 2223 -> 2223.
set -u
PORT=$(cd "$(dirname "$0")/../.." && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}


QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}
SEQ=${SEQ:-"2222 2222 2223 2223 2222"}

WORK=/tmp/sshf
rm -rf "$WORK"; mkdir -p "$WORK"
LOG=$WORK/console.log
IN=$WORK/in
mkfifo "$IN"

KEY=${KEY:-$HOME/.ssh/qemu_meecho}
[ -f "$KEY" ] || { echo "нет ключа $KEY - сначала qemu-two.sh"; exit 1; }
PUB=$(cat "$KEY.pub")

pkill -9 -f qemu-system-aarch64 2>/dev/null && sleep 2

echo "=== запуск QEMU (2222 -> 22, 2223 -> 2223) ==="
QEMU_HOSTFWD="2222,2223:2223" QEMU_TIMEOUT=$QEMU_TIMEOUT \
	bash "$PORT/ramimage.sh" -d < "$IN" > "$LOG" 2>&1 &
QPID=$!
exec 9> "$IN"

say() { printf '%s\n' "$*" >&9; sleep 1; }

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

mark() { say "printf 'd%s\\n' $1"; wait_for "d$1" "${2:-90}"; }

wait_for "login:" 240 || { kill $QPID 2>/dev/null; exit 1; }
say root
sleep 3
say "export TERM=vt100 PAGER=cat"
mark 0 || true

say "cd /etc/ssh"
say "echo '$PUB' > authorized_keys"
say "chmod 600 authorized_keys"
say "cd /"
mark 1 || true

# Оба слушателя - до первого соединения.  Иначе получится бисекция,
# загрязнённая состоянием, как в прогоне 3 на плате.
say "/usr/sbin/sshd && echo UP-22"
say "/usr/sbin/sshd -p 2223 && echo UP-2223"
mark 2 || true

echo "=== входы: $SEQ ==="
n=0
for p in $SEQ; do
	n=$((n + 1))
	echo "--- вход $n, порт хоста $p ---"
	timeout 45 ssh -p "$p" -i "$KEY" \
		-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o BatchMode=yes -o ConnectTimeout=10 \
		root@127.0.0.1 'uname -s; cat /proc/uptime; echo ATTEMPT-OK' \
		> "$WORK/a$n.out" 2> "$WORK/a$n.err"
	echo "rc=$?"
	cat "$WORK/a$n.out"
	grep -E "banner|timed out|Connection" "$WORK/a$n.err" | tail -3
done

say "ps ax | grep -v grep | grep sshd"
mark 3 || true

say "/sbin/poweroff"
sleep 8
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

echo "=== хвост консоли ==="
tail -25 "$LOG"
