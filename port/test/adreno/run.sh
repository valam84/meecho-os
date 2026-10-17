#!/bin/sh
# Хостовый стенд службы adreno (port/clover/hw-gpu.md, шаги 1-3): настоящие
# файлы службы и qcom_soc.c, настоящая читалка дерева libsys, настоящее
# дерево планшета - и модель SoC вокруг GPU (model.c), у которой обращение
# к блоку без такта вешает шину.  Служба под стенд не правится.
#
#	sh run.sh [-v]
#
# Дерево clover - GPL-2.0-only, в дереве MEECHO его нет: CLOVER_DTB= или то,
# что оставил port/clover/mkboot.sh.  Второе дерево - машина без Adreno
# (CB2, лежит в port/cb2-sd).
set -e
HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
PORT=$(cd "$HERE/../.." && pwd)
TREE=${TREE:-$(cd "$PORT/.." && pwd)}
SRC=${SRC:-$TREE/minix/drivers/video/adreno}
QCOM=${QCOM:-$TREE/minix/drivers/hid/qcom_touch}
INC=$TREE/minix/include
OUT=${OUT:-/tmp}
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
OTHER=${OTHER_DTB:-$PORT/cb2-sd/live.dtb}

if [ ! -f "$CLOVER" ]; then
	echo "adreno: no $CLOVER (CLOVER_DTB=, or port/clover/mkboot.sh)" >&2
	exit 2
fi

CC=${CC:-gcc}
$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g \
	-I "$HERE/stubs" -I "$HERE" -I "$SRC" -I "$QCOM" -idirafter "$INC" \
	-include "$HERE/shim/regs-hook.h" \
	-D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	"$HERE/adrtest.c" "$HERE/model.c" "$HERE/smmumodel.c" \
	"$SRC/adreno_find.c" "$SRC/adreno_run.c" "$SRC/adreno_cc.c" \
	"$SRC/adreno_hw.c" "$SRC/adreno_smmu.c" "$SRC/adreno_pt.c" \
	"$SRC/adreno_tz.c" "$QCOM/qcom_soc.c" \
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/adreno-test"
"$OUT/adreno-test" "$@" "$CLOVER" "$OTHER"
