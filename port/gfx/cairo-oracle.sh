#!/bin/bash
# cairo's own meson build for aarch64-elf64-minix - the oracle the
# reachover in external/mit/cairo is read from, as Mesa's is (but by hand:
# a hundred and twenty files are a list one can keep).
#
#	port/gfx/cairo-oracle.sh [DIST]
#
# DIST is a clone of the release tag (~/gfx-src/cairo, the source of
# import-stack.sh).  Meson is configured as external/mit/cairo/Makefile
# builds - PNG, fontconfig and FreeType on; zlib (PostScript, PDF, the
# script surface), X11, tee, glib and the tests off - against DESTDIR,
# every object compiled, and then compared with the tree:
#   config.h, cairo-features.h  what the checks wrote, against the tree's
#                               (which carry a comment of their own)
#   sources                     what libcairo is made of, against the
#                               Makefile's SRCS
#   read set                    the files of DIST the objects read (ninja's
#                               deps), against the import in dist/
# Nothing in the tree is written: a difference is for whoever imports.
#
# Meson's view of MEECHO, as in mesa-import.sh: the system is 'netbsd'
# (meson does not know minix; the compiler still says __minix), the
# compiler wrapper drops -pthread and links libgfxcompat's pthreads over
# libmthread, and the libraries the tree's reachovers install without a
# .pc get one here.
set -euo pipefail
PORT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$(cd "$PORT/.." && pwd)
DIST=$(cd "${1:-$HOME/gfx-src/cairo}" && pwd)
WORK=${WORK:-$HOME/cairo-oracle}
DESTDIR=${DESTDIR:-$HOME/dest-evbarm64}
XBIN=${XBIN:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix}
CROSS=$WORK/cross
ORACLE=$WORK/build-minix
TREE=$SRC/external/mit/cairo

mkdir -p "$CROSS/pc"
cat > "$CROSS/minix-gcc" <<EOF
#!/bin/bash
args=()
link=1
for a in "\$@"; do
	case "\$a" in
	-pthread) continue ;;
	-c|-S|-E|-M|-MM) link=0 ;;
	esac
	args+=("\$a")
done
inc=(--sysroot=$DESTDIR -isystem $DESTDIR/usr/include/gfxcompat)
if [ \$link = 0 ]; then
	exec $XBIN-gcc "\${inc[@]}" "\${args[@]}"
else
	exec $XBIN-gcc "\${inc[@]}" "\${args[@]}" -lgfxcompat -lmthread
fi
EOF
chmod +x "$CROSS/minix-gcc"

cat > "$CROSS/minix.ini" <<EOF
[binaries]
c = '$CROSS/minix-gcc'
ar = '$XBIN-ar'
strip = '$XBIN-strip'
nm = '$XBIN-nm'
pkg-config = '/usr/bin/pkg-config'

[properties]
sys_root = '$DESTDIR'
pkg_config_libdir = '$CROSS/pc'

[host_machine]
system = 'netbsd'
cpu_family = 'aarch64'
cpu = 'cortex-a55'
endian = 'little'
EOF

pc() {	# NAME VERSION CFLAGS LIBS
	cat > "$CROSS/pc/$1.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $1
Description: $1, as the tree's reachover installs it
Version: $2
Libs: -L\${libdir} $4
Cflags: $3
EOF
}
cp "$DESTDIR/usr/lib/pkgconfig/zlib.pc" "$CROSS/pc/"
pc pixman-1 0.46.4 '-I${includedir}/pixman-1' -lpixman-1
pc libpng 1.6.59 '-I${includedir}' '-lpng -lz -lm'
# FreeType 2.15.0's libtool version (what meson compares with).
pc freetype2 26.4.20 '-I${includedir}/freetype2' '-lfreetype -lz'
pc fontconfig 2.18.3 '-I${includedir}' \
    '-lfontconfig -lfreetype -lexpat -lz -lgfxcompat -lmthread'

rm -rf "$ORACLE"
meson setup "$ORACLE" "$DIST" --cross-file "$CROSS/minix.ini" \
    --buildtype=release --prefix=/usr --default-library=static \
    -Dpng=enabled -Dfontconfig=enabled -Dfreetype=enabled \
    -Dzlib=disabled -Dtee=disabled -Dxcb=disabled -Dxlib=disabled \
    -Dquartz=disabled -Ddwrite=disabled -Dtests=disabled -Dlzo=disabled \
    -Dglib=disabled -Dspectre=disabled -Dsymbol-lookup=disabled \
    -Dgtk2-utils=disabled > "$WORK/oracle-setup.log"
if ! ninja -C "$ORACLE" src/libcairo.a > "$WORK/oracle-build.log" 2>&1; then
	grep -E '^FAILED|error:' "$WORK/oracle-build.log" | head
	echo "cairo-oracle: the build failed: $WORK/oracle-build.log" >&2
	exit 1
fi

defines() {	# the #defines of a header, one per line, sorted
	grep -E '^#define ' "$1" | grep -v -E '_H$|_H_$' |
	    sed 's/[[:space:]]\{1,\}/ /g; s/ $//' | sort
}
rc=0
for h in config.h src/cairo-features.h; do
	if diff <(defines "$ORACLE/$h") <(defines "$TREE/${h#src/}") \
	    > "$WORK/diff-${h##*/}"; then
		echo "same    ${h##*/}"
	else
		echo "DIFFERS ${h##*/}: $WORK/diff-${h##*/}"; rc=1
	fi
done

python3 - "$ORACLE" "$TREE/Makefile" <<'EOF' || rc=1
import json, os, re, sys
cc = json.load(open(os.path.join(sys.argv[1], "compile_commands.json")))
oracle = {os.path.basename(c["file"]) for c in cc
          if "libcairo.a.p" in c["output"]}
mk = open(sys.argv[2]).read().replace("\\\n", " ")
tree = set()
for m in re.finditer(r"^SRCS\+?=(.*)$", mk, re.M):
    tree |= set(m.group(1).split())
if oracle == tree:
    print("same    sources (%d)" % len(tree))
else:
    print("DIFFERS sources: meson only %s, Makefile only %s" %
          (sorted(oracle - tree), sorted(tree - oracle)))
    sys.exit(1)
EOF

# ninja names what an object read relative to the build directory.
( cd "$ORACLE" && ninja -t deps ) | python3 -c '
import os, sys
b, d = sys.argv[1], sys.argv[2] + "/"
on, seen = False, set()
for line in sys.stdin:
    if not line.startswith(" "):
        on = line.startswith("src/libcairo.a.p/")
    elif on and line.strip():
        p = os.path.normpath(os.path.join(b, line.strip()))
        if p.startswith(d):
            seen.add(p[len(d):])
print("\n".join(sorted(seen)))
' "$ORACLE" "$DIST" > "$WORK/readset.txt"
if diff <(cd "$TREE/dist" && find src -type f | sort) "$WORK/readset.txt" \
    > "$WORK/diff-readset"; then
	echo "same    read set ($(wc -l < "$WORK/readset.txt") files)"
else
	echo "DIFFERS read set: $WORK/diff-readset"; rc=1
fi
exit $rc
