#!/bin/bash
# Подать на консоль QEMU проверку окружения, которое exec передаёт дальше.
#
# Повод: на плате шелл, которого dhcpcd запускает для хуков, падает в getenv
# (strncmp из __findenvvar) — обходя environ по указателю, который не
# указатель, а первые байты строки. В QEMU не воспроизводится: slirp отдаёт
# скудную аренду.
#
# Много коротких переменных уже проверено и проходит (400 штук по 64 байта).
# Здесь вторая половина вопроса: одна очень длинная переменная. Именно такую
# делает print_string в dhcpcd, когда сервер присылает длинную опцию.
#
# Строки уходят в гостя как есть — никаких подстановок на стороне хоста.
set -u
sleep 45
echo root
sleep 4
echo 'echo START'
sleep 2
echo 'v=aaaaaaaaaaaaaaaa'
echo 'i=0; while [ $i -lt 6 ]; do v="$v$v"; i=$((i+1)); done'
echo 'export BIG="$v"; echo LEN=${#BIG}'
echo 'sh -c "echo K1-OK"'
sleep 6
echo 'i=0; while [ $i -lt 4 ]; do v="$v$v"; i=$((i+1)); done'
echo 'export BIG="$v"; echo LEN=${#BIG}'
echo 'sh -c "echo K16-OK"'
sleep 8
echo 'i=0; while [ $i -lt 2 ]; do v="$v$v"; i=$((i+1)); done'
echo 'export BIG="$v"; echo LEN=${#BIG}'
echo 'sh -c "echo K64-OK"'
sleep 10
echo 'echo DONE'
sleep 3
echo /sbin/poweroff
sleep 10
