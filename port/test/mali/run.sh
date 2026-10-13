#!/bin/sh
# Хостовый стенд службы mali: настоящие mali_find.c и mali_rk.c, настоящая
# читалка дерева libsys, настоящие деревья (плата, плата без оверлеев,
# Allwinner H313) и регистры CRU/PMU, снятые с платы под вендорским Linux;
# настоящие MMU, буферы, задания и устройство службы на модели ускорителя
# (model.c).  Служба под стенд не правится; отказ проверки - отказ службы.
#
#	sh run.sh       проверки
#	sh run.sh -v    и журнал службы
set -e
HERE=${HERE:-$(cd "$(dirname "$0")" && pwd)}
PORT=$(cd "$HERE/../.." && pwd)
TREE=${TREE:-$(cd "$PORT/.." && pwd)}
SRC=${SRC:-$TREE/minix/drivers/video/mali}
OUT=${OUT:-/tmp}

CC=${CC:-gcc}
$CC -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	-I "$HERE/shim" -I "$SRC" -D__unused="__attribute__((unused))" \
	-DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"" \
	"$HERE/malitest.c" "$SRC/mali_find.c" "$SRC/mali_rk.c" \
	"$TREE/minix/lib/libsys/fdt.c" -o "$OUT/mali-test"

# Остальное - на модели: регистры через неё (regs-hook.h), mmap и
# vm_remap - её, кодировка ioctl и drm.h - машины, не хоста.
MODEL_CFLAGS="-std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g \
	-I $HERE/shim -I $SRC \
	-I $TREE/external/mit/libdrm/lib/libdrm/drm \
	-include $HERE/shim/regs-hook.h \
	-DREAL_DRM_H=\"$TREE/external/mit/libdrm/dist/include/drm/drm.h\" \
	-DREAL_IOC_MMAP_H=\"$TREE/minix/include/sys/ioc_mmap.h\" \
	-DMAP_PREALLOC=0x80000 -Dmmap=model_mmap -Dmunmap=model_munmap \
	-Dvm_remap=model_vm_remap -Dvm_remap_ro=model_vm_remap_ro"
CORE="$SRC/mali_mmu.c $SRC/mali_bo.c $SRC/mali_sched.c"

# Самопроверка: MMU ускорителя, цепочка заданий, отказ (jobtest.c).
$CC $MODEL_CFLAGS "$HERE/jobtest.c" "$HERE/model.c" "$SRC/mali_job.c" \
	$CORE -o "$OUT/mali-jobtest"

# Клиенты: устройство, буферы, задания, ожидания, два клиента, отказ
# одного (drmtest.c).
$CC $MODEL_CFLAGS "$HERE/drmtest.c" "$HERE/model.c" "$SRC/mali_drm.c" \
	$CORE -o "$OUT/mali-drmtest"

rc=0
"$OUT/mali-test" "$PORT/cb2-display/live.dtb" \
	"$PORT/cb2-sd/rk3566-bigtreetech-cb2.dtb" \
	"$PORT/h313/h313-tanix-tx1.dtb" "$@" || rc=1
"$OUT/mali-jobtest" "$@" || rc=1
"$OUT/mali-drmtest" "$@" || rc=1
exit $rc
