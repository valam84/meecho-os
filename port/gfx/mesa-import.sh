#!/bin/bash
# Import Mesa into external/mit/mesa: dist/ (the files the build reads,
# unchanged), gen/ (what Mesa's generators make) and lib/*/srcs.mk (sources
# and flags) - all from meson's own build of Mesa for MEECHO.
#
#	port/gfx/mesa-import.sh [fetch|tools|setup|build|reachover|all]
#
# fetch      the release tarball from gitlab (archive.mesa3d.org does not
#            answer from here), checked against MESA_SHA256
# tools      Mesa's build tools for the host: mesa_clc and vtn_bindgen2
#            (OpenCL C -> SPIR-V -> C, LLVM 21 and libclc) and
#            panfrost_compile (SPIR-V -> Mali binaries).  Panfrost's
#            internal shaders (libpan) are OpenCL C; what these tools make
#            of them does not depend on the host
# setup      meson configured for aarch64-elf64-minix (the "oracle"); see
#            the notes at mkcross below
# build      ninja: every object compiled (and the shared libraries linked
#            as far as they go - not needed), so that ninja knows what each
#            object read
# reachover  port/gfx/mesa-reachover.py into the tree
#
# Host packages (Ubuntu 26.04): meson ninja-build python3-mako bison flex
# llvm-21-dev clang-21 libclang-21-dev libclang-cpp21-dev libclc-21-dev
# llvm-spirv-21 libllvmspirvlib-21-dev spirv-tools spirv-tools-dev
# spirv-headers libdrm-dev; the toolchain with C++ (build-xtools.sh cxx).
set -euo pipefail
PORT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$(cd "$PORT/.." && pwd)

MESA_VER=${MESA_VER:-26.2.4}
MESA_SHA256=${MESA_SHA256:-89d4c3bf09fac8968f4198d00a6328e5e0df02f18d4b17b8eedc6b1a5773ae6b}
WORK=${WORK:-$HOME/mesa-import}
DESTDIR=${DESTDIR:-$HOME/dest-evbarm64}
XCXX=${XCXX:-$HOME/xtools-aarch64-cxx/bin/aarch64-elf64-minix}
XBIN=${XBIN:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix}
JOBS=${JOBS:-24}

DIST=$WORK/mesa-mesa-$MESA_VER
TOOLS=$WORK/host-tools
ORACLE=$WORK/build-minix
CROSS=$WORK/cross
OUT=$SRC/external/mit/mesa

export PATH=$TOOLS/bin:/usr/lib/llvm-21/bin:$PATH
export LD_LIBRARY_PATH=$TOOLS/lib:$TOOLS/lib/x86_64-linux-gnu

fetch() {
	mkdir -p "$WORK"
	local tar=$WORK/mesa-$MESA_VER.tar.gz
	[ -f "$tar" ] || curl -sf -m 900 -o "$tar" \
	    "https://gitlab.freedesktop.org/mesa/mesa/-/archive/mesa-$MESA_VER/mesa-mesa-$MESA_VER.tar.gz"
	echo "$MESA_SHA256  $tar" | sha256sum -c -
	rm -rf "$DIST"
	tar -C "$WORK" -xzf "$tar"
}

tools() {
	rm -rf "$WORK/build-tools"
	meson setup "$WORK/build-tools" "$DIST" --prefix="$TOOLS" \
	    --buildtype=release -Dgallium-drivers=panfrost -Dvulkan-drivers= \
	    -Dplatforms= -Degl=disabled -Dgles1=disabled -Dgles2=disabled \
	    -Dglx=disabled -Dgbm=disabled -Dllvm=enabled -Dshared-llvm=enabled \
	    -Dmesa-clc=enabled -Dinstall-mesa-clc=true \
	    -Dprecomp-compiler=enabled -Dinstall-precomp-compiler=true \
	    -Dbuild-tests=false -Dvalgrind=disabled -Dlibunwind=disabled \
	    -Dlmsensors=disabled -Dzstd=disabled -Dexpat=disabled \
	    -Dxmlconfig=disabled -Dtools=
	ninja -C "$WORK/build-tools" -j"$JOBS"
	ninja -C "$WORK/build-tools" install
	ls "$TOOLS/bin"
}

# Meson's view of MEECHO.  For measuring the build, not for building it:
#  - host_machine.system is 'netbsd'.  Mesa's tables of systems with DRM
#    (system_has_kms_drm) do not know minix, and with it the whole EGL/DRI
#    path would be left out.  The compiler still defines __minix, not
#    __NetBSD__: the code sees MINIX.
#  - the compiler wrappers drop -pthread (meson's threads dependency adds
#    it, and MINIX's GCC refuses it: no libpthread), give libmthread's
#    threads under their POSIX names (libgfxcompat), and add the flags the
#    reachover's Makefiles add (external/mit/mesa/Makefile.inc,
#    lib/libgallium/Makefile) - the same decisions, so that the oracle
#    compiles what the tree will.
#  - static libraries of the system are linked as their _pic.a into the
#    shared libraries meson makes; libstdc++ is not PIC, so libgallium's
#    .so does not link - the reachover is static, and needs it not.
mkcross() {
	mkdir -p "$CROSS/pc"
	local t
	for t in gcc g++; do
		cat > "$CROSS/minix-$t" <<EOF
#!/bin/bash
args=()
link=1
shared=0
extra=()
for a in "\$@"; do
	case "\$a" in
	-pthread) continue ;;
	-c|-S|-E|-M|-MM) link=0 ;;
	-shared) shared=1 ;;
	*hud/hud_context.c) extra+=(-DSA_SIGINFO=0) ;;
	*panfrost/lib/kmod/panfrost_kmod.c)
		extra+=(-isystem $DESTDIR/usr/include/libepoll-shim) ;;
	*gallium/drivers/panfrost/*.c) extra+=(-include pthread.h) ;;
	*util/texcompress_astc_luts*.cpp)
		extra+=(-include $OUT/lib/libgallium/onethread-mutex.h) ;;
	esac
	args+=("\$a")
done
inc=(-DDETECT_OS_POSIX=1 -DDETECT_OS_BSD=1 -DDETECT_OS_NETBSD=1
     --sysroot=$DESTDIR -isystem $DESTDIR/usr/include/gfxcompat)
if [ \$link = 0 ]; then
	exec $XCXX-$t "\${inc[@]}" "\${extra[@]}" "\${args[@]}"
elif [ \$shared = 1 ]; then
	exec $XCXX-$t "\${inc[@]}" "\${args[@]}" -l:libepoll-shim_pic.a \
		-l:libgfxcompat_pic.a -l:libmthread_pic.a
else
	exec $XCXX-$t "\${inc[@]}" "\${args[@]}" -lepoll-shim -lgfxcompat \
		-lmthread
fi
EOF
		chmod +x "$CROSS/minix-$t"
	done
	cat > "$CROSS/minix.ini" <<EOF
[binaries]
c = '$CROSS/minix-gcc'
cpp = '$CROSS/minix-g++'
ar = '$XBIN-ar'
strip = '$XBIN-strip'
nm = '$XBIN-nm'
objcopy = '$XBIN-objcopy'
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
	cp "$DESTDIR/usr/lib/pkgconfig/zlib.pc" "$CROSS/pc/"
	# libdrm's reachover installs no .pc.
	cat > "$CROSS/pc/libdrm.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libdrm
Description: Userspace interface to kernel DRM services
Version: 2.4.134
Libs: -L\${libdir} -l:libdrm_pic.a
Cflags: -I\${includedir} -I\${includedir}/libdrm
EOF
}

setup() {
	mkcross
	rm -rf "$ORACLE"
	meson setup "$ORACLE" "$DIST" --cross-file "$CROSS/minix.ini" \
	    --buildtype=release --prefix=/usr \
	    -Dgallium-drivers=panfrost -Dvulkan-drivers= -Dplatforms= \
	    -Degl=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=false \
	    -Dglx=disabled -Dgbm=enabled -Dllvm=disabled \
	    -Dmesa-clc=system -Dprecomp-compiler=system \
	    -Dshader-cache=disabled -Dbuild-tests=false -Dvalgrind=disabled \
	    -Dlibunwind=disabled -Dlmsensors=disabled -Dzstd=disabled \
	    -Dexpat=disabled -Dxmlconfig=disabled -Dtools= -Dvideo-codecs=
}

build() {
	ninja -C "$ORACLE" -k 0 -j"$JOBS" > "$WORK/oracle-build.log" 2>&1 || true
	local failed
	failed=$(grep '^FAILED' "$WORK/oracle-build.log" | grep -v '\.so' || true)
	if [ -n "$failed" ]; then
		echo "$failed"
		echo "objects failed: see $WORK/oracle-build.log" >&2
		exit 1
	fi
	echo "every object compiled"
}

reachover() {
	DESTDIR=$DESTDIR python3 "$PORT/gfx/mesa-reachover.py" "$ORACLE" \
	    "$DIST" "$OUT"
}

case "${1:-all}" in
fetch)		fetch ;;
tools)		tools ;;
setup)		setup ;;
build)		build ;;
reachover)	reachover ;;
all)		fetch; tools; setup; build; reachover ;;
*)		echo "usage: $0 [fetch|tools|setup|build|reachover|all]" >&2
		exit 1 ;;
esac
