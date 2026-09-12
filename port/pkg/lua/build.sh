#!/bin/sh
# Lua 5.4 - первый пакет репозитория. Выбран как проверка пути, а не за
# нужность: настоящая программа не из базы, MIT, один Makefile без
# configure, и результат проверяется одной строкой на машине.
. "$(dirname "$0")/../env.sh"

V=5.4.7
pkg_fetch "https://www.lua.org/ftp/lua-$V.tar.gz"
pkg_extract "lua-$V.tar.gz"

# posix: -DLUA_USE_POSIX и ничего сверх libc и libm. Переменные Makefile
# задаются в командной строке, потому что src/Makefile присваивает их сам.
make -j"$JOBS" posix \
    CC="$CC -std=gnu99" AR="$AR rcu" RANLIB="$RANLIB" \
    MYCFLAGS="$CFLAGS" MYLDFLAGS="$LDFLAGS"

stage=$(pkg_stage "lua-$V")
make install INSTALL_TOP="$stage" INSTALL="install -p"

pkg_finish "lua-$V" "$stage" \
    -c "Powerful, efficient, lightweight, embeddable scripting language" \
    -d "$RECIPE/DESCR" -l mit -P lang/lua
