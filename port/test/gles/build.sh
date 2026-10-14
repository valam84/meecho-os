#!/bin/bash
# glcube, static, against Mesa (external/mit/mesa) in DESTDIR, into
# DESTDIR/usr/tests/gles.  Linked by the toolchain's second pass: Mesa is
# partly C++ (port/build-xtools.sh cxx).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
D=${DESTDIR:-$HOME/dest-evbarm64}
X=${XCXX:-$HOME/xtools-aarch64-cxx/bin/aarch64-elf64-minix}
mkdir -p "$D/usr/tests/gles"
$X-gcc -O2 -Wall --sysroot="$D" -isystem "$D/usr/include/gfxcompat" \
	-c -o /tmp/glcube.$$.o "$HERE/glcube.c"
$X-g++ -static --sysroot="$D" -o "$D/usr/tests/gles/glcube" /tmp/glcube.$$.o \
	-Wl,--start-group -lEGL -lGLESv2 -lgbm -lgallium -Wl,--end-group \
	-ldrm -lz -lepoll-shim -lgfxcompat -lmthread -lm
rm -f /tmp/glcube.$$.o
ls -l "$D/usr/tests/gles/glcube"
