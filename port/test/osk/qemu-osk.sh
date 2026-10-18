#!/bin/bash
# The console's on-screen keyboard end to end on QEMU: fb draws it on a
# "simple-framebuffer" (the Mi Pad 4's 1200x1920, as qemu-fbcon.sh makes
# one), qcom_touch owns it - with inject= standing in for the touch screen
# QEMU does not have - and the two talk through DS.  Three frames are saved
# and made PNGs to look at:
#
#   1-upper.png   "echo Hi" typed and run; then Ctrl tapped twice (locked),
#                 Shift once (armed: the upper case shown), Alt held down
#                 (armed, and pressed)
#   2-digits.png  a new owner (down and up again): digits, '5' held
#   3-back.png    after a program held /dev/fb0: the console hidden, the
#                 owner let '5' go; the screen given back, nothing pressed
#
# and the serial log is checked for what the keys typed and for the owner
# hearing the console go and come back.
#
#	qemu-osk.sh			# WORK_DIR=~/work-osk
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../.." && pwd)
: ${WORK_DIR:=$HOME/work-osk}
export WORK_DIR
FB=0x5e000000
W=1200 H=1920
SIZE=$((W * H * 4))
SOCK=$WORK_DIR/monitor.sock
mkdir -p "$WORK_DIR"
rm -f "$WORK_DIR"/*.png "$WORK_DIR"/*.raw

# 1. The machine's own tree plus a frame buffer (as qemu-fbcon.sh).
QEMU_EXTRA="-machine dumpdtb=$WORK_DIR/virt.dtb" QEMU_TIMEOUT=20 \
    bash "$SRC/port/ramimage.sh" -r </dev/null >/dev/null 2>&1
[ -s "$WORK_DIR/virt.dtb" ] || { echo "FAIL: no dumped tree"; exit 1; }
dtc -q -I dtb -O dts -o "$WORK_DIR/virt.dts" "$WORK_DIR/virt.dtb" || exit 1
python3 - "$WORK_DIR/virt.dts" "$WORK_DIR/fb.dts" $FB $SIZE $W $H <<'EOF'
import sys
src, dst, fb, size, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), \
    int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
t = open(src).read()
reg = "<0x0 0x%x 0x0 0x%x>" % (fb, size)
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

# The monitor saves the frame buffer; its output must not reach the guest.
cat > "$WORK_DIR/save.py" <<'EOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
s.settimeout(1)
s.sendall(('pmemsave %s %s "%s"\n' % (sys.argv[3], sys.argv[4],
    sys.argv[2])).encode())
end = time.time() + 8
while time.time() < end:
	try:
		s.recv(4096)
	except socket.timeout:
		pass
s.close()
EOF
save() {
	python3 "$WORK_DIR/save.py" "$SOCK" "$WORK_DIR/$1.raw" $FB $SIZE \
	    >>"$WORK_DIR/monitor.log" 2>&1
}

UP="minix-service up /service/qcom_touch -args"
INJ1="log=3 inject=~25,e,c,h,o,Space,Shift,H,i,Enter,~50,Ctrl,Ctrl,Shift,+Alt"
INJ2="log=3 inject=~25,123,+5"

# 2. Boot with the keyboard drawn, log in, type, look.
rm -f "$SOCK" "$WORK_DIR/monitor.log"
{
	sleep 35; echo root; sleep 4
	echo "$UP \"$INJ1\""; sleep 25
	save 1-upper
	echo "minix-service down qcom_touch"; sleep 4
	echo "$UP \"$INJ2\""; sleep 15
	save 2-digits
	# The '5' held down is on the command line already: DEL it first.
	printf '\177sleep 6 < /dev/fb0\n'; sleep 14
	save 3-back
	echo /sbin/poweroff; sleep 6
} | BOOTARGS="bootramdisk=1 console=tty00 osk=1" \
    QEMU_EXTRA="-dtb $WORK_DIR/fb.dtb -monitor unix:$SOCK,server,nowait" \
    QEMU_TIMEOUT=150 bash "$SRC/port/ramimage.sh" -r \
    > "$WORK_DIR/console.log" 2>&1

fail=0
check() {	# check WHAT PATTERN: the pattern is in the serial log
	if grep -aqE "$2" "$WORK_DIR/console.log"; then
		echo "ok   $1"
	else
		echo "FAIL $1"; fail=1
	fi
}
check "fb draws the keyboard" "on-screen keyboard"
check "the owner hears the console" "console 0x3: the keyboard takes touches"
check "keys typed and run" "^Hi"
check "the console hidden under a program" "console 0x0: the keyboard takes no touches"
# Upper case shown, Shift armed, Ctrl locked, Alt armed - and a key down.
check "the owner holds Alt" 'inject: hold "\+Alt", word 0x00[0-9a-f]{2}0191'
for f in 1-upper 2-digits 3-back; do
	if [ -s "$WORK_DIR/$f.raw" ]; then
		python3 "$SRC/port/test/fbcon/fbpng.py" "$WORK_DIR/$f.raw" $W $H \
		    "$WORK_DIR/$f.png" || fail=1
		echo "     $WORK_DIR/$f.png"
	else
		echo "FAIL $f: frame buffer not saved"; fail=1
	fi
done
exit $fail
