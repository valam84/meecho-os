#!/bin/sh
#
# On the board, over ssh: the GPU at each operating point (stage 11.8).
# For every rate named, the mali service is brought up again with
# "mhz=RATE" (0 - the fastest the supply's boot voltage allows), and then:
#
#   - what the service says about the point and the clock register, and
#     whether its self-test chain came out clean (the kernel's buffer);
#   - board-glesbench.sh gles2: the desktop on the GPU, its frames by the
#     compositor's own count - drawing and waiting for the GPU;
#   - with -c, glcube offscreen for N frames: every frame checked.
#
#   sh board-opp.sh [-c N] MALI-BINARY RATE...
#
# The binary is any file - not necessarily /service/mali - so that a build
# under test does not replace the one the root boots with.  Expects
# board-glesbench.sh and kmsg in /root, glcube in /root or $PATH.
PATH=/root:/bin:/sbin:/usr/bin:/usr/sbin
export PATH

CUBE=0
if [ "$1" = -c ]; then CUBE=$2; shift 2; fi
# minix-service finds the system.conf entry by the file's name, not by
# -label: the binary under test goes under the name "mali".
mkdir -p /tmp/opp
cp "${1:?the mali binary to run}" /tmp/opp/mali || exit 1
BIN=/tmp/opp/mali
shift

for mhz in "$@"; do
	minix-service down mali > /dev/null 2>&1
	sleep 2
	kmsg > /tmp/opp-before.txt 2>/dev/null
	if ! minix-service up "$BIN" -label mali \
	    -config /etc/system.conf.d/mali -dev /dev/dri/renderD128 \
	    -args "mhz=$mhz"; then
		echo "== mhz=$mhz: the service did not come up"
		continue
	fi
	sleep 5
	echo "== mhz=$mhz"
	kmsg 2>/dev/null | grep -F -v -x -f /tmp/opp-before.txt |
	    grep -E 'operating point|CLKSEL_CON6|setting gpll|already|DIFFERS|wrong|chain|warn' |
	    sed 's/^/   /'
	sh /root/board-glesbench.sh gles2 2>&1 | grep -E '^(==|overview|cursor|idle|xray)|render_ms' |
	    sed 's/^/   /'
	if [ "$CUBE" -gt 0 ]; then
		glcube -o -q -n "$CUBE" 2>&1 | tail -3 | sed 's/^/   cube: /'
	fi
done
