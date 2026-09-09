#!/bin/sh
# Verify the user/kernel ABI of the AArch64 FP state in a signal frame.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
CC=${CC:-aarch64-elf-gcc}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/meecho-fpu-signal.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

mkdir "$TMP/machine"
ln -s "$ROOT/sys/arch/arm/include" "$TMP/arm"
ln -s "$ROOT/sys/arch/aarch64/include" "$TMP/aarch64"
for header in "$ROOT"/sys/arch/aarch64/include/*; do
	ln -s "$header" "$TMP/machine/$(basename "$header")"
done
for header in "$ROOT"/minix/include/arch/aarch64/include/*; do
	name=$(basename "$header")
	[ -e "$TMP/machine/$name" ] || ln -s "$header" "$TMP/machine/$name"
done

GCC_INCLUDE=$($CC -print-file-name=include)
INCLUDES="-isystem $GCC_INCLUDE -I$TMP -I$ROOT/include -I$ROOT/sys \
	-I$ROOT/minix/include -I$ROOT/minix \
	-I$ROOT/minix/kernel/arch/aarch64 \
	-I$ROOT/minix/kernel/arch/aarch64/include \
	-I$ROOT/minix/kernel/arch/aarch64/bsp/include"

$CC -std=gnu99 -ffreestanding -nostdinc -D__minix -D_NETBSD_SOURCE $INCLUDES \
	-c "$HERE/abi.c" -o "$TMP/abi.o"

# Parse the production paths too. This catches disagreement between the ABI
# and the kernel code without requiring a complete MINIX cross-toolchain.
$CC -std=gnu99 -ffreestanding -nostdinc -mgeneral-regs-only \
	-D__minix -D__kernel__ -D_MINIX_SYSTEM $INCLUDES -fsyntax-only \
	"$ROOT/minix/kernel/arch/aarch64/fpu.c" \
	"$ROOT/minix/kernel/system/do_sigsend.c" \
	"$ROOT/minix/kernel/system/do_sigreturn.c"

$CC -std=gnu99 -ffreestanding -nostdinc -D__minix -D_NETBSD_SOURCE \
	$INCLUDES -fsyntax-only "$ROOT/minix/tests/test62.c"

echo "AArch64 signal-frame FP/SIMD ABI and kernel paths: all checks passed"
