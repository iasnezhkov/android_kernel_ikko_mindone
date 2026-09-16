#!/usr/bin/env bash
# One-command build wrapper for the iKKO MindOne 6.1 kernel (MT6789/MT8781V-CA).
# Wraps the exact steps documented in README.md — does not replace them.
#
# Usage:
#   TOOLCHAIN=/opt/toolchains/llvm-19.1.4-aarch64 ./build.sh [OUT_DIR]
#
# Requires: LLVM/clang 19.1.4 for aarch64 (see README.md — the toolchain version is
# not cosmetic, it changes module CRCs / module_layout that out-of-tree modules
# must match). Host: Linux aarch64 or x86_64 with a cross-capable clang/lld.
set -euo pipefail
cd "$(dirname "$0")"

TOOLCHAIN="${TOOLCHAIN:-/opt/toolchains/llvm-19.1.4-aarch64}"
OUT="${1:-$(pwd)/../k61-out}"
JOBS="${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu 2>/dev/null || echo 8)}"

if [ ! -x "$TOOLCHAIN/bin/clang" ]; then
    echo "!! no clang at $TOOLCHAIN/bin/clang — set TOOLCHAIN=/path/to/llvm-19.1.4-aarch64" >&2
    exit 1
fi

export PATH="$TOOLCHAIN/bin:$PATH"
export ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu-
mkdir -p "$OUT"

echo "== toolchain: $("$TOOLCHAIN/bin/clang" --version | head -1)"
echo "== source:    $(pwd) @ $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "== output:    $OUT"

make O="$OUT" mindone_defconfig
make O="$OUT" -j"$JOBS" Image modules dtbs

REL=$(cat "$OUT/include/config/kernel.release" 2>/dev/null || echo unknown)
ML=$(grep -m1 -P '\tmodule_layout\t' "$OUT/Module.symvers" 2>/dev/null | cut -f1 || echo unknown)
echo "== done =="
echo "kernel.release  $REL"
echo "module_layout   $ML"
echo "Image           $OUT/arch/arm64/boot/Image"
echo "DTB             $OUT/arch/arm64/boot/dts/mediatek/mindone.dtb"
echo "Module.symvers  $OUT/Module.symvers   (ABI reference for building the out-of-tree modules against this kernel)"
