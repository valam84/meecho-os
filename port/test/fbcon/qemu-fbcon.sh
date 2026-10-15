#!/bin/bash
# The kernel's frame buffer console (minix/kernel/arch/aarch64/fbcon.c) on
# QEMU, which has no display a "simple-framebuffer" node could describe: the
# machine's own device tree is dumped, given a frame buffer in RAM the way the
# Mi Pad 4's tree describes its splash screen (a no-map reservation and a node
# in /chosen, 1200x1920 a8r8g8b8), and booted with it. Nothing scans that
# memory out, so the monitor saves it after boot and fbpng.py turns it into
# a PNG - the screen as the panel would have shown it.
#
#	qemu-fbcon.sh [OUT.png]
#
# KERNEL_ELF picks the kernel (default: the shared one), KERNEL_TEXT_OFFSET
# moves it (evbarm64_ramimage.sh), WORK_DIR keeps this run's files apart.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../.." && pwd)
: ${WORK_DIR:=$HOME/work-fbcon}
export WORK_DIR
OUT=${1:-$WORK_DIR/fbcon.png}
FB=0x5e000000
W=1200 H=1920
SIZE=$((W * H * 4))
SOCK=$WORK_DIR/monitor.sock
mkdir -p "$WORK_DIR"

# 1. The tree QEMU would have generated for exactly this machine.
QEMU_EXTRA="-machine dumpdtb=$WORK_DIR/virt.dtb" QEMU_TIMEOUT=20 \
    bash "$SRC/port/ramimage.sh" -r </dev/null >/dev/null 2>&1
[ -s "$WORK_DIR/virt.dtb" ] || { echo "FAIL: no dumped tree"; exit 1; }
dtc -q -I dtb -O dts -o "$WORK_DIR/virt.dts" "$WORK_DIR/virt.dtb" || exit 1

# 2. Plus a frame buffer, described as the clover tree describes its own.
python3 - "$WORK_DIR/virt.dts" "$WORK_DIR/fb.dts" $FB $SIZE $W $H <<'EOF'
import sys
src, dst, fb, size, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), \
    int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
t = open(src).read()
reg = "<0x0 0x%x 0x0 0x%x>" % (fb, size)
# A second root block: dtc merges it into the first, which keeps the
# properties-before-subnodes rule without editing the generated text.
t += """
/ {
	reserved-memory {
		#address-cells = <0x02>;
		#size-cells = <0x02>;
		ranges;
		framebuffer_memory@%x {
			reg = %s;
			no-map;
		};
	};
	chosen {
		#address-cells = <0x02>;
		#size-cells = <0x02>;
		ranges;
		framebuffer@%x {
			compatible = "simple-framebuffer";
			reg = %s;
			width = <%d>;
			height = <%d>;
			stride = <%d>;
			format = "a8r8g8b8";
		};
	};
};
""" % (fb, reg, fb, reg, w, h, w * 4)
open(dst, "w").write(t)
EOF
dtc -q -I dts -O dtb -o "$WORK_DIR/fb.dtb" "$WORK_DIR/fb.dts" || exit 1

# 3. Boot with it, log in, save the frame buffer, stop.
rm -f "$SOCK" "$WORK_DIR/fb.raw"
{
	sleep 30; echo root; sleep 3; echo "uname -a"; sleep 3
	# Its stdout is QEMU's stdin, the guest's console: say nothing there.
	python3 - "$SOCK" "$WORK_DIR/fb.raw" $FB $SIZE \
	    >"$WORK_DIR/monitor.log" 2>&1 <<'EOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
s.settimeout(1)
cmd = 'pmemsave %s %s "%s"\n' % (sys.argv[3], sys.argv[4], sys.argv[2])
s.sendall(cmd.encode())
end, got = time.time() + 10, b""
while time.time() < end:
	try:
		got += s.recv(4096)
	except socket.timeout:
		pass
print(got.decode(errors="replace"))
s.close()
EOF
	echo /sbin/poweroff; sleep 5
} | QEMU_EXTRA="-dtb $WORK_DIR/fb.dtb -monitor unix:$SOCK,server,nowait" \
    QEMU_TIMEOUT=70 bash "$SRC/port/ramimage.sh" -r > "$WORK_DIR/console.log" 2>&1
grep -aq "login:" "$WORK_DIR/console.log" || echo "WARN: no login: on the UART"
[ -s "$WORK_DIR/fb.raw" ] || { echo "FAIL: frame buffer not saved"; exit 1; }

python3 "$HERE/fbpng.py" "$WORK_DIR/fb.raw" $W $H "$OUT" || exit 1
echo "ok $OUT"
