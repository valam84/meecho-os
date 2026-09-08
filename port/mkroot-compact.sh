#!/bin/bash
# Компактный корень для прогонов на плате: сервисы, базовый userland и то,
# что нужно сети, 256 МБ. Полный корень (512 МБ, 276 программ) для проверки
# одной службы не нужен и в пять раз дольше едет по ssh.
#
# Скрипт корня переустанавливается из дерева на D: каждый раз. Это стоило
# одного прогона: mkroot.sh на D: был поправлен, а собирала его копия в
# ~/bin, и на плату уехал корень со старым /etc/rc - то есть правка,
# которой в системе не было, выглядела как правка, которая не работает.
set -euo pipefail
PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

sed 's/\r$//' $PORT/mkroot.sh > "$PORT/mkroot.sh"
# Список каталогов и размер можно перекрыть снаружи: для ssh нужны ещё
# usr/sbin (сам sshd) и usr/libexec (sshd-session и sshd-auth - в
# OpenSSH 10 демон разделён на три программы, и без двух других он
# запускается и умирает на первом же соединении).
export SUBDIRS="${SUBDIRS:-service etc/system.conf.d sbin bin usr/bin libexec}"
bash "$PORT/mkroot.sh" "${ROOT_MB:-256}"
W=$HOME/obj-evbarm64/work
cp "$W/root-emmc.img.gz" $PORT/cb2-card/
ls -l $PORT/cb2-card/root-emmc.img.gz
