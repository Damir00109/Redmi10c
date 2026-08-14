#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Full-ish partition dump for Redmi 10C (fog/rain/wind)
# Requires: unlocked BL, rooted Lineage (su), adb on PC, USB debugging
# Goal: soft-brick recovery without EDL. Does NOT magically bypass Xiaomi EDL auth.

set -euo pipefail

OUT="${1:-$HOME/Desktop/Redmi10c/backup-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
cd "$OUT"

need() { command -v "$1" >/dev/null || { echo "Install $1 first"; exit 1; }; }
need adb

echo "==> Waiting for adb device..."
adb wait-for-device
# User/Magisk builds often hang on `adb root`; Magisk su is enough.
timeout 3 adb root >/dev/null 2>&1 || true
adb wait-for-device

if ! adb shell su -c id | grep -q uid=0; then
  echo "ERROR: no Magisk/su root. Grant root to shell and enable Magisk."
  exit 1
fi

echo "==> Device info"
{
  echo "date: $(date -Is)"
  adb shell getprop ro.product.device
  adb shell getprop ro.product.name
  adb shell getprop ro.build.display.id
  adb shell getprop ro.boot.slot_suffix
  adb shell getprop ro.boot.hardware
} | tee device-info.txt

echo "==> Partition map"
adb shell su -c 'ls -l /dev/block/by-name' | tee by-name.txt
adb shell su -c 'cat /proc/partitions' | tee proc-partitions.txt

# Critical for return-to-Lineage / bootloader life
CRITICAL=(
  boot
  dtbo
  vendor_boot
  recovery
  vbmeta
  vbmeta_system
  misc
)

# Keep phone "alive" as Xiaomi/Qualcomm device — dump but DO NOT flash casually
FIRMWARE=(
  abl
  xbl
  xbl_config
  imagefv
  keymaster
  cmnlib
  cmnlib64
  uefisecapp
  devcfg
  qupfw
  hyp
  tz
  aop
  modem
  bluetooth
  dsp
  featenabler
)

# Identity / calibration — backup only, restore carefully
SENSITIVE=(
  persist
  modemst1
  modemst2
  fsg
  fsc
)

# Big Android payload (slow / huge)
BIG=(
  super
)

dump_one() {
  local name="$1"
  local dest="$2/${name}.img"
  if adb shell su -c "test -e /dev/block/by-name/${name}"; then
    echo "  dumping ${name} ..."
    # stream through adb to avoid filling phone storage
    adb shell su -c "dd if=/dev/block/by-name/${name} bs=4M 2>/dev/null" > "$dest"
    # empty/failed?
    if [[ ! -s "$dest" ]]; then
      echo "  WARN: ${name} empty/failed"
      rm -f "$dest"
      return
    fi
    sha256sum "$dest" | tee -a SHA256SUMS.txt >/dev/null
  else
    echo "  skip ${name} (no such partition)"
  fi
}

mkdir -p critical firmware sensitive big

echo "==> CRITICAL partitions"
for p in "${CRITICAL[@]}"; do dump_one "$p" critical; done

echo "==> FIRMWARE partitions (bootloader/modem stack)"
for p in "${FIRMWARE[@]}"; do dump_one "$p" firmware; done

echo "==> SENSITIVE partitions (IMEI/calib — backup only)"
for p in "${SENSITIVE[@]}"; do dump_one "$p" sensitive; done

echo "==> BIG partitions (this can take a while, several GB)"
for p in "${BIG[@]}"; do dump_one "$p" big; done

echo "==> GPT / disk metadata if available"
adb shell su -c 'ls -l /dev/block/sda /dev/block/sde /dev/block/by-name/userdata 2>/dev/null' | tee block-nodes.txt || true
# Dump first 34 LBA of primary disk candidates (GPT header+entries). Harmless read.
for disk in sda sde; do
  if adb shell su -c "test -b /dev/block/${disk}"; then
    echo "  dumping ${disk} GPT head..."
    adb shell su -c "dd if=/dev/block/${disk} bs=512 count=34 2>/dev/null" > "gpt-${disk}-head.bin" || true
  fi
done

cat > RESTORE-NOTES.txt <<'EOF'
SOFT BRICK (fastboot works):
  fastboot flash boot critical/boot.img
  fastboot flash dtbo critical/dtbo.img
  fastboot flash vendor_boot critical/vendor_boot.img   # if present
  fastboot flash recovery critical/recovery.img         # if present
  fastboot reboot

DO NOT casually flash firmware/ (abl,xbl,modem,...) unless you know why.
DO NOT flash sensitive/ unless IMEI/NV destroyed AND you understand the risk.

HARD BRICK (only EDL 9008):
  This dump helps AFTER you can write via EDL/auth tool.
  It does NOT bypass Xiaomi EDL authentication by itself.
  Keep an official fastboot ROM for your exact variant (rain) as second parachute.
EOF

echo
echo "DONE: $OUT"
du -h -d1 "$OUT" | sort -h
echo "Keep this folder on PC + USB stick. Test restore path once with fastboot boot."
