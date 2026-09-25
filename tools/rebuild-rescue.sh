#!/usr/bin/env bash
set -euo pipefail
set -o pipefail
ROOT=/home/damir00109/Desktop/Redmi10C_UPDATE/Kernel_Redmi10c
OUT=$ROOT/out
IR=$OUT/build/initramfs
make -C "$OUT/src/linux" O="$OUT/build/linux-7.1.5" ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- Image.gz dtbs -j"$(nproc)" 2>&1 | tail -3
aarch64-linux-gnu-gcc -static -O2 -I"$OUT/src/tinyalsa/include" \
  "$ROOT/tools/pcm-hold.c" "$OUT/src/tinyalsa/src/libtinyalsa.a" -o "$IR/bin/pcm-hold"
( cd "$IR" && find . | cpio -o -H newc 2>/dev/null | xz -9e --check=crc32 > "$OUT/build/rescue.cpio.xz" )
python3 "$OUT/src/mkbootimg/mkbootimg.py" \
  --header_version 2 \
  --kernel "$OUT/build/linux-7.1.5/arch/arm64/boot/Image.gz" \
  --ramdisk "$OUT/build/rescue.cpio.xz" \
  --dtb "$OUT/build/linux-7.1.5/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb" \
  --pagesize 2048 --base 0x0 \
  --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x1f00000 \
  --os_version 16.0.0 --os_patch_level 2026-06 \
  --cmdline 'console=tty0 earlycon consoleblank=0 quiet loglevel=3 initcall_debug log_buf_len=2M pd_ignore_unused clk_ignore_unused deferred_probe_timeout=120 fw_devlink=off panic=15 hung_task_panic=1 nmi_watchdog=0 softlockup_panic=0 hardlockup_panic=1' \
  --output "$OUT/boot-display-console-rescue.img"
sha256sum "$OUT/boot-display-console-rescue.img"
