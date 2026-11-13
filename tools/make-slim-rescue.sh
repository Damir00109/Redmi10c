#!/usr/bin/env bash
# Build the "slim" rescue image: same kernel/dtb as the full rescue, but an
# initramfs with the bulk firmware dropped (the 57 MiB full image exceeds the
# fastboot boot transfer limit).  Kept: sileadta TA blobs + fp templates
# (fingerprint tests), busybox, and the diagnostic tools.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out"
IR=$OUT/build/initramfs
SLIM=$OUT/build/initramfs-slim
IMG=${1:-$OUT/boot-rescue-slim.img}

aarch64-linux-gnu-gcc -static -O2 -o "$IR/bin/nfc-probe" "$ROOT/tools/nfc-probe.c"
aarch64-linux-gnu-gcc -static -O2 -o "$IR/bin/nfc-nci-test" "$ROOT/tools/nfc-nci-test.c"
aarch64-linux-gnu-gcc -static -O2 -o "$IR/bin/nfc-read" "$ROOT/tools/nfc-read.c"
aarch64-linux-gnu-gcc -static -O2 -o "$IR/bin/nfc-mfread" "$ROOT/tools/nfc-mfread.c"

rm -rf "$SLIM"
mkdir -p "$SLIM"
cp -a "$IR"/. "$SLIM"/

# Drop firmware we do not need for the console/fingerprint/NFC experiments.
find "$SLIM/lib/firmware" -mindepth 1 -maxdepth 1 \
	! -name qcom ! -name fp -exec rm -rf {} +
find "$SLIM/lib/firmware/qcom" -mindepth 1 -maxdepth 1 \
	! -name 'sileadta*' -exec rm -rf {} +

( cd "$SLIM" && find . | cpio -o -H newc 2>/dev/null | zstd -19 -T0 -q -f \
	-o "$OUT/build/rescue-slim.cpio.zst" )

python3 "$OUT/src/mkbootimg/mkbootimg.py" \
  --header_version 2 \
  --kernel "$OUT/build/linux-7.3/arch/arm64/boot/Image.gz" \
  --ramdisk "$OUT/build/rescue-slim.cpio.zst" \
  --dtb "$OUT/build/linux-7.3/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb" \
  --pagesize 2048 --base 0x0 \
  --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x4000000 \
  --os_version 16.0.0 --os_patch_level 2026-06 \
  --cmdline 'console=tty0 earlycon consoleblank=0 quiet loglevel=3 initcall_debug log_buf_len=2M pd_ignore_unused clk_ignore_unused deferred_probe_timeout=120 fw_devlink=off panic=15 hung_task_panic=1 nmi_watchdog=0 softlockup_panic=0 hardlockup_panic=1' \
  --output "$IMG"

ls -la "$IMG"
