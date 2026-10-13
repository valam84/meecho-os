#!/bin/sh
#
# libepoll-shim on the host: the library's own sources and shimtest.c,
# built by the host's gcc.  The shim's headers come first on the include
# path, so they shadow Linux's own <sys/epoll.h> and friends - what is
# tested is the shim over poll(2), not Linux's epoll.
#
#   sh run.sh
#
# SRC is the library's directory and can be overridden.

set -e

HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
SRC=${SRC:-$HOME/minix-src/minix/lib/libepoll-shim}
OBJ=${OBJ:-/tmp/shimtest}
CC=${CC:-gcc}

mkdir -p "$OBJ"
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-result \
    -D_GNU_SOURCE -I"$SRC/include" -I"$SRC" \
    "$SRC"/fdtab.c "$SRC"/epoll.c "$SRC"/eventfd.c "$SRC"/timerfd.c \
    "$SRC"/signalfd.c "$HERE/shimtest.c" -o "$OBJ/shimtest"
"$OBJ/shimtest"
