#!/bin/bash
# Native macOS workflow for the standalone AArch64 kernel and host benches.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KERNEL_DIR="$ROOT/minix/kernel/arch/aarch64/bringup"
if command -v brew >/dev/null 2>&1; then
    BREW_PREFIX=$(brew --prefix)
    export PATH="$BREW_PREFIX/bin:$BREW_PREFIX/opt/coreutils/libexec/gnubin:$PATH"
fi
ACTION=${1:-build}
if [ "$#" -gt 0 ]; then shift; fi
case "$ACTION" in
    build|run|run-el2|debug|debug-el2|disasm|clean)
        TARGET=$ACTION
        [ "$TARGET" != build ] || TARGET=all
        exec make -C "$KERNEL_DIR" -f Makefile.bringup CROSS=aarch64-elf- "$TARGET" "$@"
        ;;
    test)
        OUT=$(mktemp -d "${TMPDIR:-/tmp}/meecho-tests.XXXXXX")
        trap 'rm -rf "$OUT"' EXIT
        export OUT
        for TEST in cachectl dwmac sdmmc; do
            CC=clang sh "$ROOT/port/test/$TEST/run.sh"
        done
        CC=aarch64-elf-gcc sh "$ROOT/port/test/fpu-signal/run.sh"
        ;;
    *)
        echo "Usage: bash port/macos-dev.sh {build|run|run-el2|debug|debug-el2|disasm|clean|test}" >&2
        exit 2
        ;;
esac
