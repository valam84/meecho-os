#!/bin/sh
#
# On the board, over ssh: log in at the CONSOLE by keyboard and run a
# command there - with no keyboard and nobody at the board.
#
# Two usb_kbd instances type through inject= (the same report path, input
# server and tty as a real keyboard): the first "root" at getty's login:,
# the second a command once the shell is up, and then "exit", so that the
# console is back at login: for whoever comes next.  The command writes
# `uname -s` to a file, which is what this script checks; it also prints
# K%sB with OK, so that the screen (fbpeek + fbocr.py) shows KOKB if the
# text reached the monitor.
#
#   sh board-inject.sh          prints "typed: ok" or "typed: FAIL"
#
# Requires the console to be at login: - nobody logged in on it.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

rm -f /tmp/kbd-typed
minix-service up /service/usb_kbd -label kbdi1 \
    -args 'inject=root%0a injectwait=2' || exit 1
minix-service up /service/usb_kbd -label kbdi2 \
    -args 'inject=uname%20-s%20%3e%20/tmp/kbd-typed%3b%20printf%20%27K%25sB%5cn%27%20OK%0aexit%0a injectwait=9' ||
    exit 1
sleep 16
minix-service down kbdi1
minix-service down kbdi2

if [ "`cat /tmp/kbd-typed 2>/dev/null`" = MEECHO ]
then	echo "typed: ok"
else	echo "typed: FAIL (`cat /tmp/kbd-typed 2>&1`)"
fi
