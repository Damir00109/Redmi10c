#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Pack a boot.img for TEMPORARY test: fastboot boot (NOT flash).
# Uses Lineage boot_b header layout as template where possible.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG_OUT="${1:-$ROOT/out/boot-mainline-TEST.img}"
KERNEL="${2:-$ROOT/out/mainline/arch/arm64/boot/Image.gz}"
# Minimal ramdisk placeholder — replace with real initramfs later
RAMDISK="${3:-$ROOT/out/ramdisk-empty.cpio}"

mkdir -p "$(dirname "$IMG_OUT")" "$ROOT/out"
if [ ! -f "$KERNEL" ]; then
  echo "Build kernel first: $ROOT/tools/build-mainline.sh"
  exit 1
fi

if [ ! -f "$RAMDISK" ]; then
  # empty cpio
  ( cd "$ROOT/out" && echo | cpio -o -H newc 2>/dev/null | gzip -n > ramdisk-empty.cpio.gz )
  RAMDISK="$ROOT/out/ramdisk-empty.cpio.gz"
fi

if command -v mkbootimg >/dev/null; then
  mkbootimg --header_version 3 \
    --kernel "$KERNEL" \
    --ramdisk "$RAMDISK" \
    --os_version 16.0.0 \
    --os_patch_level 2026-06 \
    --output "$IMG_OUT"
else
  echo "mkbootimg not installed. Install android-sdk or clone AOSP mkbootimg."
  echo "Kernel ready at: $KERNEL"
  exit 1
fi

echo "Wrote $IMG_OUT"
echo "TEST ONLY: fastboot boot $IMG_OUT"
echo "DO NOT: fastboot flash ..."
