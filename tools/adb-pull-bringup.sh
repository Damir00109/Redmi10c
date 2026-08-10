#!/usr/bin/env bash
# Pull high-value Android (Lineage) artifacts for mainline bringup.
# Usage: tools/adb-pull-bringup.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$ROOT/notes/adb-pull-$STAMP"
mkdir -p "$OUT"/{dt,sysfs,ufs,pinctrl,regs,props,logs,firmware-extra}

echo "Waiting for adb device..."
adb wait-for-device
adb shell getprop ro.product.device | tee "$OUT/device.txt"
adb shell getprop ro.build.display.id | tee -a "$OUT/device.txt"
adb shell dumpsys battery | tee "$OUT/battery.txt" >/dev/null

# --- props / identity ---
adb shell getprop > "$OUT/props/getprop.txt"
adb shell 'cat /proc/cmdline; echo; cat /proc/version' > "$OUT/props/cmdline-version.txt"
adb shell 'getprop ro.boot.slot_suffix; getprop ro.boot.verifiedbootstate; getprop ro.hardware; getprop ro.board.platform' > "$OUT/props/boot.txt"

# --- live flattened DT (gold for UFS/rails/gpio) ---
echo "Dumping live device-tree..."
adb shell 'su -c "tar -C /sys/firmware/devicetree/base -cf - ."' > "$OUT/dt/sysfs-dt.tar" 2>/dev/null \
  || adb shell 'tar -C /sys/firmware/devicetree/base -cf - .' > "$OUT/dt/sysfs-dt.tar"
# also try /proc/device-tree
adb shell 'su -c "tar -C /proc/device-tree -cf - ."' > "$OUT/dt/proc-dt.tar" 2>/dev/null \
  || adb shell 'tar -C /proc/device-tree -cf - .' > "$OUT/dt/proc-dt.tar" 2>/dev/null || true

# focused UFS/gpio/regulator strings from live DT
adb shell 'find /sys/firmware/devicetree/base -iname "*ufs*" 2>/dev/null' > "$OUT/dt/ufs-paths.txt" || true
adb shell '
for n in $(find /sys/firmware/devicetree/base -iname "*ufs*" -type d 2>/dev/null); do
  echo "==== $n"
  ls "$n" 2>/dev/null
  for f in compatible status reg reset-gpios pinctrl-0 pinctrl-names \
           vcc-supply vccq-supply vccq2-supply vdd-hba-supply \
           vdda-phy-supply vdda-pll-supply \
           qcom,vddp-ref-clk-supply qcom,vddp-ref-clk-min-uV qcom,vddp-ref-clk-max-uV \
           vcc-voltage-level vccq2-voltage-level iommus phys phy-names; do
    p="$n/$f"
    if [ -e "$p" ]; then
      echo -n "$f="; xxd -p "$p" 2>/dev/null | tr -d "\n"; echo
    fi
  done
done
' > "$OUT/dt/ufs-nodes.txt" 2>/dev/null || true

adb shell '
echo "=== gpio ufs_reset / pin 113 hints ==="
find /sys/firmware/devicetree/base -iname "*ufs*reset*" 2>/dev/null
grep -rsa "ufs_reset" /sys/firmware/devicetree/base 2>/dev/null | head
find /sys/firmware/devicetree/base -name "pins" 2>/dev/null | while read p; do
  xxd -p "$p" 2>/dev/null | grep -q 7566735f7265736574 && echo "$p=$(xxd -p $p)"
done
' > "$OUT/dt/ufs-reset-search.txt" 2>/dev/null || true

# --- UFS runtime (Android kernel already linked storage) ---
echo "Collecting UFS/SCSI runtime..."
adb shell '
echo "=== block ==="; ls -l /dev/block/by-name 2>/dev/null | head -80
echo "=== scsi ==="; ls /sys/class/scsi_host /sys/class/scsi_device 2>/dev/null
echo "=== ufshcd ==="; find /sys -iname "*ufs*" 2>/dev/null | head -80
for h in /sys/class/scsi_host/host*; do
  [ -d "$h" ] || continue
  echo "---- $h"
  cat "$h"/proc_name 2>/dev/null; cat "$h"/unique_id 2>/dev/null
done
' > "$OUT/ufs/runtime.txt" 2>/dev/null || true

adb shell 'dmesg 2>/dev/null | grep -iE "ufs|ufshcd|scsi|qmp|ice|link startup|vddp|ref.clk" | tail -200' > "$OUT/logs/dmesg-ufs.txt" || true
adb shell 'logcat -d -b all 2>/dev/null | grep -iE "ufs|ufshcd|smb135|charger|vddp" | tail -200' > "$OUT/logs/logcat-ufs.txt" || true

# --- regulators / SPMI (voltages Android actually set) ---
echo "Regulators..."
adb shell '
echo "=== power_supply ==="; ls /sys/class/power_supply
for p in /sys/class/power_supply/*; do
  echo "---- $p"
  for f in type status capacity voltage_now current_now present online; do
    [ -e "$p/$f" ] && echo -n "$f="; cat "$p/$f" 2>/dev/null; echo
  done
done
echo "=== regulator summary (names with ufs/l4/l11/l18/l24) ==="
ls /sys/class/regulator 2>/dev/null
for r in /sys/class/regulator/regulator.*; do
  n=$(cat "$r/name" 2>/dev/null)
  case "$n" in
    *ufs*|*UFS*|*L4*|*L11*|*L12*|*L18*|*L24*|*l4*|*l11*|*l18*|*l24*|*vddp*|*vcc*)
      echo "$n microvolts=$(cat $r/microvolts 2>/dev/null) status=$(cat $r/status 2>/dev/null) mode=$(cat $r/mode 2>/dev/null)"
      ;;
  esac
done
' > "$OUT/regs/power-regulators.txt" 2>/dev/null || true

# full regulator dump (can be large)
adb shell 'for r in /sys/class/regulator/regulator.*; do echo -n "$(cat $r/name 2>/dev/null)= "; cat $r/microvolts 2>/dev/null; echo -n " "; cat $r/status 2>/dev/null; echo; done' \
  > "$OUT/regs/all-regulators.txt" 2>/dev/null || true

# --- pinctrl / gpio ---
adb shell '
ls /sys/class/pinctrl /sys/class/gpio 2>/dev/null
ls /sys/kernel/debug/pinctrl 2>/dev/null
# may need root
su -c "cat /sys/kernel/debug/pinctrl/*/pinmux-pins 2>/dev/null | grep -iE \"ufs|113|reset\" | head -80"
su -c "cat /sys/kernel/debug/gpio 2>/dev/null | grep -iE \"ufs|113\" | head -40"
' > "$OUT/pinctrl/gpio-ufs.txt" 2>/dev/null || true

# --- SMB / charger (for later mainline charge) ---
adb shell '
ls /sys/class/power_supply
find /sys -iname "*smb*" 2>/dev/null | head -40
getprop | grep -iE "charge|battery|smb|fg|fuel"
' > "$OUT/regs/charger.txt" 2>/dev/null || true

# --- firmware extras we may still miss ---
echo "Firmware extras..."
adb shell 'ls -la /vendor/firmware 2>/dev/null; ls /vendor/firmware/wlan 2>/dev/null; ls /mnt/vendor/persist 2>/dev/null | head' > "$OUT/firmware-extra/listing.txt" || true
# BDF / WLAN if not already local
adb pull /vendor/firmware/bd3qvdfu.bin "$OUT/firmware-extra/" 2>/dev/null || true
adb pull /vendor/etc/wifi "$OUT/firmware-extra/wifi" 2>/dev/null || true
# panel / touch conf crumbs
adb shell 'ls /vendor/firmware/*focal* /vendor/firmware/*novatek* /vendor/firmware/*a610* 2>/dev/null' >> "$OUT/firmware-extra/listing.txt" || true

# --- partitions map + sizes (for later dd if needed) ---
adb shell 'ls -l /dev/block/by-name; cat /proc/partitions' > "$OUT/props/partitions.txt" 2>/dev/null || true

# --- kernel config / modules hints ---
adb shell 'zcat /proc/config.gz 2>/dev/null | grep -iE "UFS|QMP|SMB|CHARGER|PINCTRL_SM|SPMI|UFSHCD|NFC|FTS|KGSL|DRM_MSM" | head -120' \
  > "$OUT/props/config-grep.txt" 2>/dev/null || true
adb shell 'lsmod; cat /proc/modules' > "$OUT/props/modules.txt" 2>/dev/null || true

# --- summary ---
{
  echo "pulled $STAMP"
  echo "OUT=$OUT"
  du -sh "$OUT"/* 2>/dev/null
} | tee "$OUT/SUMMARY.txt"

ln -sfn "$(basename "$OUT")" "$ROOT/notes/adb-pull-latest"
echo "OK -> $OUT (symlink notes/adb-pull-latest)"
