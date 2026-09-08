#!/bin/bash
# На плате /etc/rc уронил шелл сразу после строки "Asking for an address",
# по адресу 0x4e00000000efffff — 0x4e это 'N', первая буква строки
# "Network on ...", то есть строка пишется поверх указателя.
#
# В QEMU та же подстановка работает, и разница между двумя случаями одна:
# в работающем варианте после закрывающей обратной кавычки есть ещё текст
# (" (leased)"), а в упавшем строка кавычкой кончается.
#
# Скрипт кладёт в гостя файл с четырьмя формами и запускает его — именно
# файлом, а не набором в интерактивном шелле: чтение скрипта у ash идёт
# другим путём, и на плате упало чтение файла.
set -u
sleep 30
echo root
sleep 3

echo "cat > /tmp/t.sh <<'EOF'"
echo 'netif=vio0'
echo 'echo T1-START'
echo 'echo "N:`ifconfig $netif | sed -n s/inet/X/p` tail"'
echo 'echo T1-DONE'
echo 'echo T2-START'
echo 'echo "N:`ifconfig $netif | sed -n s/inet/X/p`"'
echo 'echo T2-DONE'
echo 'echo T3-START'
echo 'echo "Network on $netif:`ifconfig $netif |'
echo '    sed -n s/inet/X/p`"'
echo 'echo T3-DONE'
echo 'echo T4-START'
echo 'echo "Network on $netif:`ifconfig $netif |'
echo '    sed -n s/inet/X/p` (leased)"'
echo 'echo T4-DONE'
echo EOF
sleep 3
echo 'sh /tmp/t.sh; echo "RC=$?"'
sleep 8
echo /sbin/poweroff
sleep 8
