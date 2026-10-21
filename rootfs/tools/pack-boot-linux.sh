#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Pack boot-linux.img for Redmi 10C (fog/rain, SM6225):
# mainline kernel + pivot initramfs (mounts PARTLABEL=cust -> switch_root).
#
# Standalone version — adapted from origin/main:tools/pack-ubuntu-boot.sh
# to the new `kernel`-branch tree layout (out/src, out/build).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REDMI="${REDMI:-$(cd "$ROOT/.." && pwd)}"
OUT="$ROOT/out"
IR="$OUT/pivot-initramfs"
KERN="$REDMI/out/build/linux-7.3"
DTB="$KERN/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb"
IMG="$OUT/boot-linux.img"
BB="$REDMI/out/build/busybox"
MKBOOTIMG="$REDMI/out/src/mkbootimg/mkbootimg.py"
FW="$REDMI/initramfs/lib/firmware"

for f in "$BB" "$MKBOOTIMG" "$DTB" "$KERN/arch/arm64/boot/Image.gz"; do
  [ -e "$f" ] || { echo "Missing: $f" >&2; exit 1; }
done

rm -rf "$IR"
mkdir -p "$IR"/{bin,sbin,dev,proc,sys,tmp,newroot,sys/kernel/config}

cp -a "$BB" "$IR/bin/busybox"
(
  cd "$IR/bin"
  ./busybox --list | while read -r app; do
    [ "$app" = "busybox" ] && continue
    ln -sfn busybox "$app"
  done
)
ln -sfn ../bin/busybox "$IR/sbin/init"
if find "$IR/bin" -type l -lname '/*' | grep -q .; then
  echo "FATAL: absolute busybox symlinks in pivot initramfs" >&2
  exit 1
fi
cp -a "$ROOT/tools/pivot-init" "$IR/init"
chmod 755 "$IR/init"

# Firmware loaded BEFORE switch_root (GPU zap, ADSP, modem, IPA, wlan, touch,
# PA): the cust rootfs is not mounted yet, so blobs must live in initramfs.
if [ -d "$FW" ]; then
  mkdir -p "$IR/lib/firmware"
  cp -a "$FW/." "$IR/lib/firmware/"
else
  echo "WARN: no firmware dir $FW — run $REDMI/tools/fetch-firmware.sh" >&2
fi

( cd "$IR" && find . | cpio -o -H newc 2>/dev/null | zstd -19 -T0 -q -f -o "$OUT/pivot.cpio.zst" )

python3 "$MKBOOTIMG" \
  --header_version 2 \
  --kernel "$KERN/arch/arm64/boot/Image.gz" \
  --ramdisk "$OUT/pivot.cpio.zst" \
  --dtb "$DTB" \
  --pagesize 2048 --base 0x0 \
  --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x1f00000 \
  --os_version 16.0.0 --os_patch_level 2026-06 \
  --cmdline 'console=tty0 console=ttyGS0,115200 earlycon consoleblank=0 log_buf_len=2M pd_ignore_unused clk_ignore_unused deferred_probe_timeout=120 fw_devlink=off root=PARTLABEL=cust rw rootwait rcupdate.rcu_cpu_stall_timeout=300 rcupdate.rcu_cpu_stall_suppress=1 panic=0 nmi_watchdog=0 softlockup_panic=0' \
  --output "$IMG"

python3 -c "import os; s=os.path.getsize('$IMG'); print('$IMG', round(s/1048576,2), 'MiB', 'OK' if s<57500000 else 'TOO_BIG')"
sha256sum "$IMG"
