#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Temporary mainline boot test over USB. Always restores dtbo + reboots to Android.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:-$ROOT/out/boot-mainline-fog-TEST.img}"
DTBO_BAK="$ROOT/backup/ab-20260726-023543/active/dtbo.img"
BOOT_BAK="$ROOT/backup/ab-20260726-023543/active/boot.img"
LOG="$ROOT/logs/test-boot-$(date +%Y%m%d-%H%M%S).log"

exec > >(tee -a "$LOG") 2>&1
echo "=== test start $(date -Is) ==="
echo "IMG=$IMG"
test -f "$IMG"
test -f "$DTBO_BAK"
test -f "$BOOT_BAK"

restore_android() {
  echo "=== RESTORE Android $(date -Is) ==="
  # ensure fastboot
  if adb devices 2>/dev/null | grep -q 'device$'; then
    adb reboot bootloader || true
    sleep 2
  fi
  for i in $(seq 1 30); do
    fastboot devices | grep -q . && break
    sleep 1
  done
  if ! fastboot devices | grep -q .; then
    echo "ERROR: no fastboot for restore — hold Vol- + Power"
    return 1
  fi
  fastboot flash dtbo "$DTBO_BAK"
  # boot partition untouched if we only used 'fastboot boot', but ensure Lineage boot is there
  fastboot flash boot "$BOOT_BAK"
  fastboot reboot
  echo "Waiting for adb..."
  for i in $(seq 1 60); do
    adb wait-for-device 2>/dev/null && break
    sleep 2
  done
  sleep 5
  adb devices -l
  adb shell getprop ro.product.device || true
  echo "=== restore done ==="
}

trap 'echo TRAP; restore_android' ERR

echo "Reboot to fastboot..."
adb reboot bootloader
for i in $(seq 1 40); do
  fastboot devices | grep -q . && break
  sleep 1
done
fastboot devices
fastboot getvar current-slot || true
fastboot getvar product || true

echo "Erase dtbo (temp) so Android overlays do not fight mainline..."
fastboot erase dtbo

echo "Temporary boot (NOT flash): $IMG"
fastboot boot "$IMG" || true

echo "Waiting 25s for USB ACM / any sign..."
sleep 25
echo "USB devices:"
lsusb | grep -iE 'Google|Xiaomi|Qualcomm|Linux|ACM|Gadget' || lsusb | head
dmesg 2>/dev/null | tail -30 | grep -iE 'ttyGS|usb|gadget|acm' || true
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "no host ACM yet"

echo "Force back to fastboot via key timing not available — reboot command:"
# If still in fastboot after failed boot, good; if hanging, user needs keys.
# Try: some devices return to fastboot after crash; else wait and restore when reachable.
for i in $(seq 1 20); do
  if fastboot devices | grep -q .; then
    echo "fastboot available again"
    break
  fi
  if adb devices | grep -q 'device$'; then
    echo "unexpected adb device"
    break
  fi
  sleep 2
done

restore_android
echo "=== test end $(date -Is) ==="
