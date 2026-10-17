#!/bin/sh
# Хостовый стенд службы adreno (port/clover/hw-gpu.md, шаги 1-7): настоящие
# файлы службы и qcom_soc.c, настоящая читалка дерева libsys, настоящее
# дерево планшета - и модель SoC вокруг GPU (model.c), у которой обращение
# к блоку без такта вешает шину, модель SMMU (smmumodel.c), модель
# TrustZone для zap (tzmodel.c) и модель CP (cpmodel.c).  Служба под стенд
# не правится.
#
#	sh run.sh [-v]
#
# Два стенда: adrtest - стадии первых шагов (зонд, питание, SMMU, zap, CP);
# msmtest - узел рендеринга (msm DRM на libdrmdriver, шаг 7), где вместо
# системы модель окружения (osmodel.c: VFS, VM, libchardriver, часы).
#
# Дерево clover - GPL-2.0-only, в дереве MEECHO его нет: CLOVER_DTB= или то,
# что оставил port/clover/mkboot.sh.  Второе дерево - машина без Adreno
# (CB2, лежит в port/cb2-sd).  ZAP_DIR= - каталог с a512_zap.* планшета
# (снят с его раздела vendor, в дерево класть нельзя); по умолчанию -
# D:\minix\clover-flash\gpu\zap, если он есть; без него стенд гоняет только
# придуманный образ.
set -e
HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
PORT=$(cd "$HERE/../.." && pwd)
TREE=${TREE:-$(cd "$PORT/.." && pwd)}
SRC=${SRC:-$TREE/minix/drivers/video/adreno}
QCOM=${QCOM:-$TREE/minix/drivers/hid/qcom_touch}
LIB=${LIB:-$TREE/minix/lib/libdrmdriver}
INC=$TREE/minix/include
OUT=${OUT:-/tmp}
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
OTHER=${OTHER_DTB:-$PORT/cb2-sd/live.dtb}
ZAP_DIR=${ZAP_DIR-/mnt/d/minix/clover-flash/gpu/zap}
if [ -n "$ZAP_DIR" ] && [ -f "$ZAP_DIR/a512_zap.mdt" ]; then
	export ZAP_DIR
else
	unset ZAP_DIR
fi
# linux-firmware's microcode, as port/clover/fetch-gpu-fw.sh leaves it.
FW_DIR=${FW_DIR-$HOME/gpu-fw/qcom}
if [ -n "$FW_DIR" ] && [ -f "$FW_DIR/a530_pfp.fw" ]; then
	export FW_DIR
else
	unset FW_DIR
fi

if [ ! -f "$CLOVER" ]; then
	echo "adreno: no $CLOVER (CLOVER_DTB=, or port/clover/mkboot.sh)" >&2
	exit 2
fi

CC=${CC:-gcc}
SERVICE="$SRC/adreno_find.c $SRC/adreno_run.c $SRC/adreno_cc.c \
	$SRC/adreno_hw.c $SRC/adreno_smmu.c $SRC/adreno_pt.c \
	$SRC/adreno_tz.c $SRC/adreno_fw.c $SRC/adreno_mdt.c \
	$SRC/adreno_zap.c $SRC/adreno_cp.c $QCOM/qcom_soc.c \
	$TREE/minix/lib/libsys/fdt.c"
MODELS="$HERE/model.c $HERE/smmumodel.c $HERE/tzmodel.c $HERE/cpmodel.c"

$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g \
	-I "$HERE/stubs" -I "$HERE" -I "$SRC" -I "$QCOM" -idirafter "$INC" \
	-include "$HERE/shim/regs-hook.h" \
	-D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	"$HERE/adrtest.c" $MODELS $SERVICE -o "$OUT/adreno-test"

# The render node: the service's adreno_gpu.c and adreno_drm.c, libdrmdriver
# beside them; mmap and vm_remap the model's, the encoding of ioctl and
# drm.h the machine's, not the host's; msm_drm.h of the libdrm reachover.
$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -O1 -g \
	-I "$HERE/stubs-drm" -I "$HERE/stubs" -I "$HERE" -I "$SRC" \
	-I "$QCOM" -I "$TREE/external/mit/libdrm/lib/libdrm/drm" \
	-I "$TREE/minix/lib/libgfxcompat" -idirafter "$INC" \
	-include "$HERE/shim/regs-hook.h" \
	-D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$INC/minix/fdt.h\"" \
	-DREAL_DRM_H="\"$TREE/external/mit/libdrm/dist/include/drm/drm.h\"" \
	-DREAL_IOC_MMAP_H="\"$INC/sys/ioc_mmap.h\"" \
	-DMAP_PREALLOC=0x80000 -Dmmap=model_mmap -Dmunmap=model_munmap \
	-Dvm_remap=model_vm_remap -Dvm_remap_ro=model_vm_remap_ro \
	"$HERE/msmtest.c" "$HERE/osmodel.c" $MODELS $SERVICE \
	"$SRC/adreno_gpu.c" "$SRC/adreno_drm.c" \
	"$LIB/drm_dev.c" "$LIB/drm_sync.c" "$LIB/drm_mem.c" \
	-o "$OUT/adreno-msmtest"

rc=0
"$OUT/adreno-test" "$@" "$CLOVER" "$OTHER" || rc=1
"$OUT/adreno-msmtest" "$@" "$CLOVER" || rc=1
exit $rc
