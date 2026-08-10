#!/usr/bin/env bash
# Dump Wi-Fi / WCN passport from live Android (Magisk root).
# Usage: tools/dump-android-wifi.sh [OUTDIR]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/notes/android-wifi-live}"
mkdir -p "$OUT"/{sysfs,fw,proc,props}

need() { command -v "$1" >/dev/null || { echo "need $1"; exit 1; }; }
need adb

adb wait-for-device
adb shell 'su -c id' | grep -q uid=0 || { echo "ERROR: Magisk su required"; exit 1; }

SU='su -c'
run() { adb shell "$SU" "$1"; }
save() {
  local rel="$1" cmd="$2"
  echo "==> $rel"
  adb shell "$SU" "$cmd" >"$OUT/$rel" 2>&1 || true
}

echo "dump → $OUT ($(date -Is))"

save props/getprop-wifi.txt \
  'getprop | grep -iE "wlan|wifi|wcn|qca|cnss|icnss|vendor.wlan|persist.vendor.wlan|ro.boot.wificountry|ro.hardware"'

save props/device.txt \
  'getprop ro.product.device; getprop ro.product.model; getprop ro.build.display.id; getprop ro.boot.slot_suffix; getprop ro.hardware; getprop ro.board.platform'

save dmesg-wifi.txt \
  'dmesg | grep -iE "icnss|cnss|wlan|qca|wcn|wifi|msa|pil|modem|qrtr|mhi" | tail -n 400'

save dmesg-full-tail.txt \
  'dmesg | tail -n 200'

save proc/modules-wifi.txt \
  'lsmod | grep -iE "wlan|icnss|cnss|qca|wcn|cfg80211|mac80211" || true'

save proc/interrupts-wifi.txt \
  'cat /proc/interrupts | grep -iE "wlan|cnss|wcn|wifi|msa" || true'

save proc/iomem-wifi.txt \
  'cat /proc/iomem | grep -iE "wlan|cnss|wcn|wifi|msa|c800000" || true'

save sysfs/platform-wifi.txt \
  'ls -la /sys/bus/platform/devices/ | grep -iE "icnss|cnss|wcn|wlan|wifi" || true; echo ---; find /sys/bus/platform/devices -maxdepth 2 -iname "*icnss*" -o -iname "*cnss*" -o -iname "*wcn*" 2>/dev/null | head -50'

save sysfs/icnss-uevent.txt \
  'for d in /sys/bus/platform/devices/*icnss* /sys/devices/platform/*/icnss* /sys/devices/platform/soc/*/icnss* /sys/bus/platform/devices/*cnss*; do [ -e "$d/uevent" ] && echo "### $d" && cat "$d/uevent" && echo; done; true'

save sysfs/net-ifaces.txt \
  'ip link; echo ---; ls -la /sys/class/net/; for i in /sys/class/net/*; do echo "### $i"; cat "$i/address" 2>/dev/null; cat "$i/uevent" 2>/dev/null; done'

save sysfs/firmware-wlan-ls.txt \
  'ls -laR /vendor/firmware/wlan /vendor/firmware_mnt/image 2>/dev/null | head -n 400; echo ---; ls -la /vendor/firmware/*.bin /vendor/firmware/*.b* /vendor/firmware/*wcn* /vendor/firmware/*wlan* /vendor/firmware/*qca* 2>/dev/null | head -n 200'

save sysfs/bdwlan.txt \
  'ls -la /mnt/vendor/persist/wlan /mnt/vendor/persist/*.bin /persist/wlan 2>/dev/null; find /mnt/vendor/persist /persist /data/vendor/wifi -iname "*bdf*" -o -iname "*bdwlan*" -o -iname "*mac*" 2>/dev/null | head -80'

save props/wifi-hal.txt \
  'ls -la /vendor/lib64/hw/*wifi* /vendor/bin/hw/*wifi* /vendor/etc/wifi 2>/dev/null; cat /vendor/etc/wifi/*.conf 2>/dev/null | head -n 200'

# DT snippets from live tree
save sysfs/dt-icnss.txt \
  'find /proc/device-tree /sys/firmware/devicetree/base -iname "*icnss*" -o -iname "*wcn*" -o -iname "*wlan*" 2>/dev/null | head -100'

save sysfs/dt-icnss-compat.txt \
  'for p in $(find /sys/firmware/devicetree/base -name compatible 2>/dev/null); do
     t=$(tr "\0" " " <"$p" 2>/dev/null)
     echo "$t" | grep -qiE "icnss|cnss|wcn|wlan" && echo "$p: $t"
   done | head -80'

# Pull firmware trees (best-effort)
pull_tree() {
  local remote="$1" localdir="$2"
  mkdir -p "$localdir"
  if adb shell "$SU" "test -d $remote"; then
    echo "==> pull $remote → $localdir"
    adb shell "$SU" "tar -C $remote -cf - ." 2>/dev/null | tar -C "$localdir" -xf - 2>/dev/null \
      || adb pull "$remote" "$localdir" 2>/dev/null || true
  fi
}

pull_tree /vendor/firmware/wlan "$OUT/fw/vendor-firmware-wlan"
# common BDF / cfg names
for f in \
  /vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
  /vendor/firmware/wlan/qca_cld/wlan_mac.bin \
  /vendor/firmware/bdwlan.bin \
  /vendor/firmware/bdwlanb.bin \
  /vendor/firmware/bd3qvdfu.bin \
  /mnt/vendor/persist/wlan/wlan_mac.bin
do
  base=$(basename "$f")
  adb shell "$SU" "test -f $f" >/dev/null 2>&1 || continue
  echo "==> pull file $f"
  adb shell "$SU" "cat $f" >"$OUT/fw/$base" 2>/dev/null || true
done

# Chip/version strings from driver sysfs if present
save sysfs/wcn-version.txt \
  'find /sys -name "*fw_version*" -o -name "*chip_id*" -o -name "*hw_version*" 2>/dev/null | head -40; \
   for p in $(find /sys -iname "*wcn*" -o -iname "*icnss*" 2>/dev/null | head -30); do
     [ -f "$p" ] && [ -r "$p" ] && echo "### $p" && cat "$p" 2>/dev/null
   done | head -n 200'

# SUMMARY skeleton filled by host later; stamp raw facts
{
  echo "# Android Wi-Fi live dump"
  echo "date: $(date -Is)"
  echo "device: $(adb shell getprop ro.product.device | tr -d '\r')"
  echo "build: $(adb shell getprop ro.build.display.id | tr -d '\r')"
  echo "slot: $(adb shell getprop ro.boot.slot_suffix | tr -d '\r')"
} >"$OUT/META.txt"

echo "DONE → $OUT"
ls -la "$OUT" | head
