#!/bin/sh
# Хостовый стенд службы qcom_cpufreq (port/clover/hw-cpu.md, шаг 2): её
# настоящие файлы (cf_find.c, cf_volt.c, cf_rail.c, cf_clk.c, cf_run.c),
# настоящая читалка дерева libsys, настоящее дерево планшета с
# sdm660-xiaomi-clover-cpu.dtsi - и модель вокруг (model.c): PLL обоих
# кластеров (захват только после MODE 2 -> 6 с L в диапазоне), их
# мультиплексоры (UPDATE), aux в GCC, фьюзы, рельсы PM660 за арбитром SPMI.
# Служба под стенд не правится.
#
#	sh run.sh [-v[v[v]]]
#
# Что рельс держит, модель знает не от службы, а от port/clover/cpu-volt.py:
# voltref.py пишет его углы для фьюзов планшета, нулевых, двух краевых
# наборов и трёхсот случайных (volt-ref.txt).
#
# Дерево clover - GPL-2.0-only, в дереве MEECHO его нет: CLOVER_DTB= или то,
# что оставил port/clover/mkboot.sh; узел meecho,sdm660-cpu-dvfs стенд
# дописывает к нему сам, из того же .dtsi, что берёт cpu-probe.sh -b.
set -e
HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
PORT=$(cd "$HERE/../.." && pwd)
TREE=${TREE:-$(cd "$PORT/.." && pwd)}
SRC=${SRC:-$TREE/minix/drivers/power/qcom_cpufreq}
PMIC=$TREE/minix/drivers/power/qcom_pmic
INC=$TREE/minix/include
OUT=${OUT:-/tmp}
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
DTSI=$PORT/clover/sdm660-xiaomi-clover-cpu.dtsi
PROBE_LOG=$PORT/clover/cpu-probe-20261018.txt

if [ ! -f "$CLOVER" ]; then
	echo "qcom_cpufreq: no $CLOVER (CLOVER_DTB=, or port/clover/mkboot.sh)" >&2
	exit 2
fi

# The tree with the CPU blocks: the decompiled tree has no labels, so the
# .dtsi's "&soc" becomes the node's path.
dtc -q -I dtb -O dts "$CLOVER" > "$OUT/cf-tree.dts"
sed 's|^&soc {|\&{/soc@0} {|' "$DTSI" >> "$OUT/cf-tree.dts"
dtc -q -I dts -O dtb -o "$OUT/cf-tree.dtb" "$OUT/cf-tree.dts"

python3 "$HERE/voltref.py" "$PROBE_LOG" > "$OUT/cf-volt-ref.txt"

CC=${CC:-gcc}
$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g \
	-I "$HERE/stubs" -I "$HERE" -I "$SRC" -I "$PMIC" -idirafter "$INC" \
	-include "$HERE/shim/regs-hook.h" \
	-D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	"$HERE/cftest.c" "$HERE/model.c" \
	"$SRC/cf_find.c" "$SRC/cf_volt.c" "$SRC/cf_rail.c" "$SRC/cf_clk.c" \
	"$SRC/cf_run.c" "$TREE/minix/lib/libsys/fdt.c" \
	-o "$OUT/cf-test"

"$OUT/cf-test" "$@" "$OUT/cf-tree.dtb" "$OUT/cf-volt-ref.txt"
