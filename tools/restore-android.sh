#!/usr/bin/env bash
# Restore Lineage after mainline test (dtbo was erased).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DTBO="$ROOT/backup/ab-20260726-023543/active/dtbo.img"
BOOT="$ROOT/backup/ab-20260726-023543/active/boot.img"

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
fastboot flash dtbo "$DTBO"
fastboot flash boot "$BOOT"
fastboot reboot
echo "Waiting for adb..."
adb wait-for-device
sleep 8
adb devices -l
adb shell getprop ro.product.device
adb shell getprop ro.build.display.id
echo "Android restore OK"
