#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Restore Lineage after mainline test (dtbo was erased).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DTBO="$ROOT/backup/lineage-23.2-a/dtbo_a.img"
BOOT="$ROOT/backup/lineage-23.2-a/boot_a.img"
VBMETA="$ROOT/backup/lineage-23.2-a/vbmeta_a.img"

# Prefer: Android already up → adb reboot bootloader (reliable on this ABL).
# Fallback: wait for manual Vol- + Power fastboot.
if adb devices 2>/dev/null | awk 'NR>1&&$2=="device"{ok=1} END{exit !ok}'; then
  echo "Android adb up → adb reboot bootloader"
  adb reboot bootloader
fi

echo "Waiting for fastboot..."
for i in $(seq 1 120); do
  fastboot devices | grep -q . && break
  sleep 1
done
fastboot devices | grep -q . || { echo "no fastboot device"; exit 1; }
test -f "$DTBO"
test -f "$BOOT"
test -f "$VBMETA"
fastboot set_active a
fastboot flash dtbo_a "$DTBO"
fastboot flash boot_a "$BOOT"
fastboot flash vbmeta_a "$VBMETA"
fastboot reboot
echo "Waiting for adb..."
adb wait-for-device
sleep 8
adb devices -l
adb shell getprop ro.product.device
adb shell getprop ro.build.display.id
echo "Android restore OK"
