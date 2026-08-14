#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Safer mainline test: erase dtbo -> fastboot boot -> ALWAYS restore dtbo+boot -> reboot Android
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:-$ROOT/out/boot-mainline-fog-TEST.img}"
DTBO="$ROOT/backup/ab-20260726-023543/active/dtbo.img"
BOOT="$ROOT/backup/ab-20260726-023543/active/boot.img"
LOG="$ROOT/logs/test-boot-$(date +%Y%m%d-%H%M%S).log"
WAIT_SECS="${WAIT_SECS:-40}"

exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) IMG=$IMG WAIT=$WAIT_SECS ==="

restore() {
  echo "=== RESTORE $(date -Is) ==="
  # get to fastboot from wherever
  if adb devices 2>/dev/null | grep -q $'\tdevice'; then
    adb reboot bootloader || true
  fi
  for i in $(seq 1 60); do
    fastboot devices | grep -q . && break
    sleep 1
  done
  if ! fastboot devices | grep -q .; then
    echo "NEED_KEYS: hold Vol- + Power for fastboot, then re-run restore-android.sh"
    return 1
  fi
  fastboot flash dtbo "$DTBO"
  fastboot flash boot "$BOOT"
  fastboot reboot
  adb wait-for-device
  sleep 6
  adb shell getprop ro.product.device
  echo "RESTORE_OK"
}

adb reboot bootloader
for i in $(seq 1 40); do fastboot devices | grep -q . && break; sleep 1; done
fastboot devices
fastboot erase dtbo
echo "Booting mainline (temporary)..."
fastboot boot "$IMG" || true
echo "Observing ${WAIT_SECS}s (screen should show alive; watch lsusb for 18d1 ACM)..."
for i in $(seq 1 "$WAIT_SECS"); do
  if ls /dev/ttyACM* >/dev/null 2>&1; then
    echo "ACM_UP: $(ls /dev/ttyACM*)"
    break
  fi
  sleep 1
done
lsusb | grep -iE '18d1|Google|Xiaomi|Gadget|ACM' || true

echo "Forcing return via long wait then reboot to bootloader from host if possible..."
# hard: phone may ignore USB; try adb (won't work) then ask for power cycle path
# After WAIT, user may still be on alive screen — reboot to bootloader by... we can't.
# So: instruct Power 10s then Vol-+Power, OR if they left us a way:
sleep 2
# Attempt: some gadgets respond to nothing. Restore path requires fastboot.
echo "If still on alive screen: Power 10-20s off, then Vol- + Power to fastboot."
echo "Auto-waiting up to 3 min for fastboot..."
for i in $(seq 1 90); do
  if fastboot devices | grep -q .; then
    restore
    exit 0
  fi
  sleep 2
done
echo "TIMEOUT — run: $ROOT/tools/restore-android.sh when in fastboot"
exit 1
