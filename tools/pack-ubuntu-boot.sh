#!/usr/bin/env bash
# Pack boot-linux.img: mainline kernel + slim pivot initramfs (no huge FW).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out/ubuntu-dualboot"
KERN="$ROOT/out/linux-7.1.5"
DTB="$KERN/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb"
IMG="$OUT/boot-linux.img"
IR="$OUT/pivot-initramfs"

rm -rf "$IR"
mkdir -p "$IR"/{bin,sbin,dev,proc,sys,tmp,newroot,sys/kernel/config}

# busybox from bringup initramfs
# IMPORTANT: busybox --install -s uses absolute paths → broken on device.
# Install relative symlinks only.
BB="$ROOT/out/initramfs-root/bin/busybox"
cp -a "$BB" "$IR/bin/busybox"
(
  cd "$IR/bin"
  ./busybox --list | while read -r app; do
    [ "$app" = "busybox" ] && continue
    ln -sfn busybox "$app"
  done
)
ln -sfn ../bin/busybox "$IR/sbin/init"
# verify no absolute host paths leaked into symlinks
if find "$IR/bin" -type l -lname '/*' | grep -q .; then
  echo "FATAL: absolute busybox symlinks in pivot initramfs" >&2
  find "$IR/bin" -type l -lname '/*' | head
  exit 1
fi
cp -a "$ROOT/tools/pivot-init" "$IR/init"
chmod 755 "$IR/init"

# Adreno GPU firmware — must be in initramfs because the GPU probe runs
# before switch_root and cannot wait for the cust rootfs to be mounted.
FW="$ROOT/notes/vendor-firmware-20260731/extract/firmware"
if [ -d "$FW" ]; then
  mkdir -p "$IR/lib/firmware/qcom/sm6225"
  cp -a "$FW/a630_sqe.fw" "$IR/lib/firmware/qcom/"
  cp -a "$FW"/a610_zap.* "$IR/lib/firmware/qcom/sm6225/"
fi

# fsck.ext4 helper if available (static or from host - skip if not arm64)
# optional: copy e2fsck from rootfs later

( cd "$IR" && find . | cpio -o -H newc 2>/dev/null | gzip -9n > "$OUT/pivot.cpio.gz" )

python3 "$ROOT/tools/mkbootimg/mkbootimg.py" \
  --header_version 2 \
  --kernel "$KERN/arch/arm64/boot/Image.gz" \
  --ramdisk "$OUT/pivot.cpio.gz" \
  --dtb "$DTB" \
  --pagesize 2048 --base 0x0 \
  --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x1f00000 \
  --os_version 16.0.0 --os_patch_level 2026-06 \
  --cmdline 'console=tty0 earlycon consoleblank=0 log_buf_len=2M pd_ignore_unused clk_ignore_unused root=PARTLABEL=cust rw rootwait' \
  --output "$IMG"

python3 -c "import os; s=os.path.getsize('$IMG'); print('boot-linux.img', round(s/1048576,2), 'MiB', 'OK' if s<57500000 else 'TOO_BIG')"
cp -a "$IMG" "$OUT/twrp/images/boot-linux.img"
