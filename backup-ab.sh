#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# A/B-aware dump for Redmi 10C (rain) — slots use *_a / *_b names
set -euo pipefail

OUT="${1:-$HOME/Desktop/Redmi10c/backup/ab-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"/{critical,firmware,extra}
cd "$OUT"

need() { command -v "$1" >/dev/null || { echo "Install $1"; exit 1; }; }
need adb
adb wait-for-device

if ! adb shell su -c id | grep -q uid=0; then
  echo "ERROR: Magisk su required"
  exit 1
fi

SLOT="$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')"
echo "Active slot: ${SLOT:-unknown}" | tee device-slot.txt

dump_one() {
  local part="$1"
  local dest="$2/${part}.img"
  if adb shell su -c "test -e /dev/block/by-name/${part}"; then
    echo "  dumping ${part} ..."
    adb shell su -c "dd if=/dev/block/by-name/${part} bs=4M 2>/dev/null" > "$dest"
    if [[ ! -s "$dest" ]]; then
      echo "  WARN: ${part} empty"
      rm -f "$dest"
      return
    fi
    sha256sum "$dest" | tee -a SHA256SUMS.txt >/dev/null
  else
    echo "  skip ${part}"
  fi
}

# Active slot first, then the other — both kept for safety
CRITICAL_BASE=(boot dtbo vendor_boot vbmeta vbmeta_system)
FIRMWARE_BASE=(
  abl xbl xbl_config imagefv keymaster cmnlib cmnlib64 uefisecapp
  devcfg qupfw hyp tz modem bluetooth dsp featenabler rpm
)

echo "==> CRITICAL (both slots)"
for base in "${CRITICAL_BASE[@]}"; do
  dump_one "${base}_a" critical
  dump_one "${base}_b" critical
done
dump_one misc critical
dump_one rescue critical

echo "==> FIRMWARE (both slots)"
for base in "${FIRMWARE_BASE[@]}"; do
  dump_one "${base}_a" firmware
  dump_one "${base}_b" firmware
done

echo "==> EXTRA useful"
for p in cust metadata frp splash storsec; do
  dump_one "$p" extra
done

# GPT heads
for disk in sda sdb sdc sdd sde sdf; do
  if adb shell su -c "test -b /dev/block/${disk}"; then
    adb shell su -c "dd if=/dev/block/${disk} bs=512 count=34 2>/dev/null" > "gpt-${disk}-head.bin" || true
  fi
done

# Convenience copies of ACTIVE slot under unsuffixed names (for fast restore)
if [[ -n "$SLOT" ]]; then
  mkdir -p active
  for base in boot dtbo vendor_boot vbmeta vbmeta_system abl xbl xbl_config modem; do
    src="critical/${base}${SLOT}.img"
    [[ -f "$src" ]] || src="firmware/${base}${SLOT}.img"
    if [[ -f "$src" ]]; then
      cp -n "$src" "active/${base}.img"
    fi
  done
fi

cat > RESTORE-NOTES.txt <<EOF
Device: rain (A/B)
Active slot when dumped: ${SLOT}

SOFT BRICK restore (fastboot):
  fastboot flash boot_a critical/boot_a.img
  fastboot flash boot_b critical/boot_b.img
  # or only active:
  fastboot flash boot${SLOT} critical/boot${SLOT}.img
  fastboot flash dtbo${SLOT} critical/dtbo${SLOT}.img
  fastboot flash vendor_boot${SLOT} critical/vendor_boot${SLOT}.img
  fastboot reboot

Or use active/ copies:
  fastboot flash boot active/boot.img
  fastboot flash dtbo active/dtbo.img
  fastboot flash vendor_boot active/vendor_boot.img

NEVER flash firmware/ unless you know why.
EOF

echo "DONE: $OUT"
du -h -d1 "$OUT" | sort -h
