#!/bin/bash
# Host test of the SPMI PMIC arbiter client (qcom_pmic/spmi.c).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/power/qcom_pmic
OUT=${TMPDIR:-/tmp}/spmitest
cc -std=c99 -Wall -Wextra -Werror -O1 -g -I"$SRC" -o "$OUT" \
    "$HERE/spmitest.c" "$SRC/spmi.c"
"$OUT"
