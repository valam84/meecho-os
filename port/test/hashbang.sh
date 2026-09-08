#!/bin/bash
# Подать на консоль QEMU воспроизведение падения шелла хуков dhcpcd.
#
# Гипотеза: exec скрипта с #! портит ps_strings. insert_arg в VFS считает,
# что ps_strings лежит в самом конце кадра, а minix_stack_fill на aarch64
# оставляет после него 8 байт зазора в половине случаев - когда округление
# строк на 8 и кадра на 16 расходятся. Тогда правится не та структура, и
# ps_envstr (откуда crt0 берёт environ) сдвигается на байт.
#
# Значит: скрипт с #! должен падать в зависимости от ДЛИНЫ окружения, через
# одно. Здесь окружение удлиняется по байту, и каждый раз запускается
# двухстрочный скрипт. Ожидание при верной гипотезе: чередование OK и
# падений с периодом, связанным с 8 и 16 байтами.
set -u
sleep 45
echo root
sleep 4
echo 'printf "#!/bin/sh\necho OK-\$PAD\n" > /tmp/t.sh; chmod +x /tmp/t.sh; cat /tmp/t.sh'
sleep 3
echo 'echo START'
sleep 1
echo 'P=; i=0; while [ $i -lt 24 ]; do export PAD="$P"; echo "len=$i"; /tmp/t.sh; P="${P}x"; i=$((i+1)); done'
sleep 40
echo 'echo DONE'
sleep 3
echo /sbin/poweroff
sleep 10
