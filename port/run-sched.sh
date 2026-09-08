#!/bin/sh
# Запустить расписание на консоли платы фоном и оставить метку по окончании.
# Отдельным файлом, потому что через ssh эта строка обрастает кавычками
# быстрее, чем читается.
TTY=${1:-/dev/ttyUSB0}
BAUD=${2:-1500000}
# Порт держит кто-то, кроме нашей консоли (например, screen человека):
# не отбирать, а отказать громко - два читателя одного tty рвут поток обоим.
holder=$(fuser "$TTY" 2>/dev/null | tr -d " ")
if [ -n "$holder" ] && ! pgrep -f "hub-conso[l]e" >/dev/null; then
	echo "console-BUSY: $TTY занят pid $holder ($(ps -o comm= -p $holder 2>/dev/null))"
	exit 1
fi
rm -f /tmp/run.done /tmp/run.log /tmp/run.err
pkill -f 'hub-conso[l]e' >/dev/null 2>&1
sleep 1
setsid sh -c "python3 /root/hub-console.py $TTY $BAUD /tmp/run.log /tmp/sched.txt >/tmp/run.out 2>/tmp/run.err; touch /tmp/run.done" </dev/null >/dev/null 2>&1 &
sleep 1
pgrep -f 'hub-conso[l]e' >/dev/null && echo console-up || echo console-FAILED
exit 0
