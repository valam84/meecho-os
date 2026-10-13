#!/bin/sh
# A picture of the board's screen as a PNG, while MEECHO runs on it.  From
# Git Bash on Windows, as the other board-*.sh.
#
# fbart says the frame's geometry (its first line: "FBART 1280x720 stride
# 5120, ..."), dd copies that many rows of /dev/fb0 - the buffer the
# display controller shows - through gzip over ssh, and fbpng.py in WSL
# writes the PNG.  The same picture qemu-shot.py takes on QEMU, of the real
# scanout this time.
#
#   board-shot.sh [out.png]          default: ./meecho-board.png
set -u
__d=$(dirname "$0")
for c in "$__d/board.conf" "$__d/../board.conf" "$__d/../../board.conf"; do
	[ -f "$c" ] && . "$c"
done
BOARD=${BOARD:-cb2-lan}
BOARD_PORT=${BOARD_PORT:-22}
SSH_BIN=${SSH_BIN:-ssh}
WSL_DISTRO=${WSL_DISTRO:-Ubuntu}
WSL_USER=${WSL_USER:-minix}
MSYS_NO_PATHCONV=1
export MSYS_NO_PATHCONV
# The host key of MEECHO is made on its first boot, and the address is
# Armbian's too: never let one of them into known_hosts.
OPTS="-o BatchMode=yes -o ConnectTimeout=15 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=ERROR"
OUT=${1:-meecho-board.png}
RAW=$OUT.raw.gz

geo=$($SSH_BIN $OPTS -p "$BOARD_PORT" "$BOARD" \
    'uname -s; /usr/tests/gfx/fbart -w 1 -h 1 | head -1')
case "$geo" in
Linux*|*"not found"*|"")
	echo "board-shot: not MEECHO with fbart on $BOARD: $geo" >&2; exit 1 ;;
esac
set -- $(echo "$geo" | sed -n 's/^FBART \([0-9]*\)x\([0-9]*\) stride \([0-9]*\).*/\1 \2 \3/p')
[ $# -eq 3 ] || { echo "board-shot: no geometry in: $geo" >&2; exit 1; }
W=$1 H=$2 STRIDE=$3

$SSH_BIN $OPTS -p "$BOARD_PORT" "$BOARD" \
    "dd if=/dev/fb0 bs=$STRIDE count=$H 2>/dev/null | gzip -1" > "$RAW" || exit 1

# A Git Bash path (/d/minix/...) as WSL sees it (/mnt/d/minix/...).
wslpath() {
	p=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
	echo "$p" | sed 's|^/\([a-zA-Z]\)/|/mnt/\L\1/|'
}
wsl.exe -d "$WSL_DISTRO" -u "$WSL_USER" -- python3 "$(wslpath "$__d/fbpng.py")" \
    "$(wslpath "$RAW")" "$W" "$H" "$STRIDE" "$(wslpath "$OUT")" &&
	rm -f "$RAW"
