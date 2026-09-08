#!/bin/bash
# Assemble the archives a release ships.
#
#	bash port/mkrelease.sh [version]        default: 0.1.0
#
# Three of them, because they are wanted separately:
#
#   meecho-qemu-VER.tar.gz    kernel, boot archive, a root image, and a run
#                             script.  Needs qemu-system-aarch64 and nothing
#                             else.
#   meecho-cb2-VER.tar.gz     what goes on the SD card of a BIGTREETECH CB2:
#                             the kernel, the boot archive, and the boot
#                             scripts that start them once.
#   meecho-cb2-root-VER.img.gz  the eMMC root: 1 GB with the full userland,
#                             written to the board with dd.  Kept out of the
#                             card archive because it is sixty megabytes and
#                             most runs do not need a fresh one.
#
# Everything is built first by build-release paths; this script only packs
# what is already in $WORK, and refuses to pack anything missing.
set -uo pipefail

PORT=$(cd "$(dirname "$0")" && pwd)
SRCDIR=${SRCDIR:-$(cd "$PORT/.." && pwd)}

VER=${1:-0.1.0}
WORK=${WORK:-$HOME/obj-evbarm64/work}
OUT=${OUT:-$HOME/release-$VER}

need() { [ -f "$1" ] || { echo "$0: missing $1 -- run port/build-all.sh and port/mkcard.sh first" >&2; exit 1; }; }
need "$WORK/kernel.bin"
need "$WORK/boot.mba"
need "$WORK/disk.img"
need "$WORK/card/boot.scr"

rm -rf "$OUT"
mkdir -p "$OUT"

# ---------------------------------------------------------------------------
# QEMU
q=$OUT/meecho-qemu-$VER
mkdir -p "$q"
cp "$WORK/kernel.bin" "$WORK/boot.mba" "$q/"
echo "compressing the disk image..."
gzip -9 -c "$WORK/disk.img" > "$q/disk.img.gz"

cat > "$q/run.sh" <<'RUNSH'
#!/bin/sh
# Boot MEECHO on QEMU.  Needs qemu-system-aarch64 and nothing else.
#
#	./run.sh              root on the disk image
#	./run.sh -r           root on the ramdisk; no disk at all
#	./run.sh -2           enter at EL2, as U-Boot leaves a kernel on a board
#	./run.sh -3           give the machine a GICv3 instead of a GICv2
#	./run.sh -s 4         four cores
#	SSH=2222 ./run.sh     forward host port 2222 to the guest's sshd
#
# Log in as root, no password.  Type poweroff to shut down; QEMU exits by
# itself.  Ctrl-A X kills it if something goes wrong.
set -eu
cd "$(dirname "$0")"

ram=0; el2=0; gic=2; smp=1
while [ $# -gt 0 ]; do
	case "$1" in
	-r) ram=1; shift ;;
	-2) el2=1; shift ;;
	-3) gic=3; shift ;;
	-s) smp=$2; shift 2 ;;
	*)  echo "usage: $0 [-r] [-2] [-3] [-s cores]" >&2; exit 1 ;;
	esac
done

command -v qemu-system-aarch64 >/dev/null ||
	{ echo "no qemu-system-aarch64: apt install qemu-system-arm" >&2; exit 1; }

# The disk image ships compressed and is unpacked once.  What you write to
# it stays there -- booting twice and finding your file is how you tell a
# disk from a ramdisk.
if [ $ram -eq 0 ] && [ ! -f disk.img ]; then
	echo "unpacking the disk image (512 MB, once)..."
	gunzip -c disk.img.gz > disk.img
fi

machine="virt"
[ "$gic" = 3 ] && machine="$machine,gic-version=3"
[ $el2 -eq 1 ] && machine="$machine,virtualization=on"

net="-netdev user,id=net0"
[ -n "${SSH:-}" ] && net="$net,hostfwd=tcp::${SSH}-:22"

if [ $ram -eq 1 ]; then
	disk=""
	args="bootramdisk=1 console=tty00"
else
	disk="-drive if=none,id=hd0,file=disk.img,format=raw,discard=unmap
	      -device virtio-blk-device,drive=hd0"
	args="rootdevname=c0d0 console=tty00"
fi

echo "MEECHO on qemu virt: EL$( [ $el2 -eq 1 ] && echo 2 || echo 1 ), GICv$gic, $smp core(s)"
echo "log in as root; type poweroff to leave"
echo
exec qemu-system-aarch64 -M "$machine" -cpu cortex-a72 -m 512 -smp "$smp" \
	-display none -serial stdio \
	$net -device virtio-net-device,netdev=net0 \
	$disk \
	-kernel kernel.bin -initrd boot.mba \
	-append "$args"
RUNSH
chmod +x "$q/run.sh"

cat > "$q/README.md" <<QREADME
# MEECHO $VER for QEMU

    ./run.sh

Log in as \`root\`, no password. \`poweroff\` shuts it down and QEMU exits.
\`Ctrl-A X\` kills it if something goes wrong.

Needs \`qemu-system-aarch64\` (Debian/Ubuntu: \`apt install qemu-system-arm\`).

| File | What it is |
|---|---|
| \`kernel.bin\` | the kernel, a flat image with an arm64 header |
| \`boot.mba\` | the boot archive: twelve boot images the kernel unpacks |
| \`disk.img.gz\` | a 512 MB MFS V4 root with the full userland |
| \`run.sh\` | the QEMU command line, with the options that matter |

The disk image is unpacked on first use and then kept. What you write to it
stays there, which is the point.

Other combinations, all worth trying, and each exercising different code:

    ./run.sh -2          enter at EL2, as U-Boot leaves a kernel on a board
    ./run.sh -3          GICv3, which is what the target board has
    ./run.sh -2 -3 -s 4  both, on four cores
    ./run.sh -r          the ramdisk root; no disk at all
    SSH=2222 ./run.sh    then: ssh -p 2222 root@127.0.0.1

Some things to try once you are in:

    uname -a
    ls /bin /sbin /usr/bin | wc -l      # 276 programs
    cat /proc/cpuinfo /proc/uptime
    mount                               # mfs on /, procfs on /proc
    ps ; mtop
    vi /root/hello                      # yes, really
    ifconfig -a ; ping -c 3 10.0.2.2    # 10.0.2.2 is your host
    fsck_mfs -s /dev/c0d0

Source, documentation and the porting log: see the project page.
QREADME

# ---------------------------------------------------------------------------
# CB2 card kit
c=$OUT/meecho-cb2-$VER
mkdir -p "$c"
cp -r "$WORK/card/." "$c/"
rm -f "$c/root-emmc.img.gz"          # shipped separately
cp "$SRCDIR/docs/meecho/RUNNING-CB2.md" "$c/RUNNING-CB2.md" 2>/dev/null || true

# ---------------------------------------------------------------------------
# CB2 eMMC root
if [ -f "$WORK/root-emmc.img.gz" ]; then
	cp "$WORK/root-emmc.img.gz" "$OUT/meecho-cb2-root-$VER.img.gz"
fi

# ---------------------------------------------------------------------------
cd "$OUT"
tar czf "meecho-qemu-$VER.tar.gz" "meecho-qemu-$VER"
tar czf "meecho-cb2-$VER.tar.gz"  "meecho-cb2-$VER"
rm -rf "meecho-qemu-$VER" "meecho-cb2-$VER"
sha256sum ./*.tar.gz ./*.img.gz > SHA256SUMS 2>/dev/null

echo
echo "release $VER in $OUT:"
ls -lh "$OUT"
echo
cat SHA256SUMS
