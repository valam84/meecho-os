#!/bin/bash
# Host tests of the touch driver's pure parts: the QUP I2C master (qup.c)
# and the touch-to-key logic (keys.c).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/hid/qcom_touch
INC=$HERE/../../../minix/include
T=${TMPDIR:-/tmp}
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -o "$T/quptest" \
    "$HERE/quptest.c" "$SRC/qup.c"
"$T/quptest"
if [ -f "$HERE/keystest.c" ]; then
	cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -I"$INC" \
	    -o "$T/keystest" "$HERE/keystest.c" "$SRC/keys.c"
	"$T/keystest"
fi
