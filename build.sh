#!/usr/bin/env bash
# Build a Redmi 10C (rain/fog, SM6225) rescue boot image from a clean checkout.
# Produces: out/boot-display-console-rescue.img
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
JOBS="${JOBS:-$(nproc)}"
ARCH=arm64
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
KERNEL_TAG="${KERNEL_TAG:-v7.1.5}"
BUSYBOX_TAG="${BUSYBOX_TAG:-1_36_1}"
TINYALSA_TAG="${TINYALSA_TAG:-1.1.1}"

OUT="$ROOT/out"
SRC="$OUT/src"
BUILD="$OUT/build"
MAINLINE="$SRC/linux"
BUSYBOX_SRC="$SRC/busybox"
TINYALSA_SRC="$SRC/tinyalsa"
MKBOOTIMG_SRC="$SRC/mkbootimg"
KERN_OUT="$BUILD/linux-7.1.5"
IR="$BUILD/initramfs"

check_deps() {
  local tool
  for tool in git make python3 cpio gzip; do
    command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; exit 1; }
  done
  command -v "${CROSS_COMPILE}gcc" >/dev/null || { echo "Missing cross compiler: ${CROSS_COMPILE}gcc" >&2; exit 1; }
}

clone_kernel() {
  if [ ! -d "$MAINLINE/.git" ]; then
    rm -rf "$MAINLINE"
    git clone --branch "$KERNEL_TAG" --depth 1 \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git "$MAINLINE"
  fi
}

apply_patch() {
  cd "$MAINLINE"
  if [ -n "$(git status --porcelain)" ]; then
    git checkout -- .
    git clean -fd
  fi
  git apply --check "$ROOT/kernel.patch"
  git apply "$ROOT/kernel.patch"
}

build_kernel() {
  mkdir -p "$KERN_OUT"
  cp "$ROOT/kernel.config" "$KERN_OUT/.config"
  make -C "$MAINLINE" O="$KERN_OUT" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
    olddefconfig Image.gz dtbs -j"$JOBS"
}

build_busybox() {
  if [ -f "$BUILD/busybox" ]; then
    return 0
  fi
  if [ ! -d "$BUSYBOX_SRC/.git" ]; then
    rm -rf "$BUSYBOX_SRC"
    git clone --branch "$BUSYBOX_TAG" --depth 1 https://github.com/mirror/busybox.git "$BUSYBOX_SRC"
  fi
  cp "$ROOT/initramfs/busybox.config" "$BUSYBOX_SRC/.config"
  # oldconfig may exit 141 because of SIGPIPE from yes; ignore.
  yes '' | make -C "$BUSYBOX_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" oldconfig || true
  make -C "$BUSYBOX_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS" busybox
  mkdir -p "$BUILD"
  cp "$BUSYBOX_SRC/busybox" "$BUILD/busybox"
}

build_tinyalsa() {
  if [ -x "$BUILD/tinymix" ] && [ -x "$BUILD/tinyplay" ] && [ -x "$BUILD/tinypcminfo" ]; then
    return 0
  fi
  if [ ! -d "$TINYALSA_SRC/.git" ]; then
    git clone --branch "$TINYALSA_TAG" --depth 1 https://github.com/tinyalsa/tinyalsa.git "$TINYALSA_SRC"
  fi
  python3 - "$TINYALSA_SRC/utils/tinyplay.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
s = path.read_text()
s = s.replace(
    'fprintf(stderr, "error playing sample: %s\\n", pcm_get_error(ctx->pcm)");',
    'fprintf(stderr, "error playing sample: %s\\n", pcm_get_error(ctx->pcm));',
)
s = s.replace(
    'fprintf(stderr, "error playing sample\\n");',
    'fprintf(stderr, "error playing sample: %s\\n", pcm_get_error(ctx->pcm));',
)
path.write_text(s)
PY
  make -C "$TINYALSA_SRC/src" CROSS_COMPILE="$CROSS_COMPILE" CFLAGS="-O2" libtinyalsa.a -j"$JOBS"
  for tool in tinymix tinyplay tinypcminfo; do
    "${CROSS_COMPILE}gcc" -static -O2 -I"$TINYALSA_SRC/include" \
      "$TINYALSA_SRC/utils/$tool.c" "$TINYALSA_SRC/src/libtinyalsa.a" -o "$BUILD/$tool"
  done
}

ensure_firmware() {
  local fw="$ROOT/initramfs/lib/firmware"
  if [ -f "$fw/qcom/sm6225/a610_zap.mdt" ] && [ -f "$fw/qcom/sm6225/adsp.mdt" ]; then
    return
  fi
  [ -x "$ROOT/tools/fetch-firmware.sh" ] || {
    echo "Firmware is missing; run tools/fetch-firmware.sh" >&2
    exit 1
  }
  "$ROOT/tools/fetch-firmware.sh"
}

build_initramfs() {
  ensure_firmware
  rm -rf "$IR"
  mkdir -p "$IR"/bin "$IR"/dev "$IR"/proc "$IR"/sys "$IR"/tmp "$IR"/sys/kernel/config "$IR"/lib/firmware
  cp "$BUILD/busybox" "$IR/bin/busybox"
  cp "$BUILD/tinymix" "$BUILD/tinyplay" "$BUILD/tinypcminfo" "$IR/bin/"
  python3 -c 'import math,struct,wave,sys; w=wave.open(sys.argv[1],"wb"); w.setparams((2,2,48000,24000,"NONE","not compressed")); w.writeframes(b"".join(struct.pack("<hh", int(300*math.sin(2*math.pi*480*i/48000)), int(300*math.sin(2*math.pi*480*i/48000))) for i in range(24000))); w.close()' "$IR/test-quiet.wav"
  (
    cd "$IR/bin"
    ./busybox --list | while read -r app; do
      [ "$app" = "busybox" ] && continue
      ln -sfn busybox "$app"
    done
  )
  # CRDA regulatory database for cfg80211 (silences firmware load error).
  if [ -f /lib/firmware/regulatory.db ]; then
    cp /lib/firmware/regulatory.db "$IR/lib/firmware/"
    cp /lib/firmware/regulatory.db.p7s "$IR/lib/firmware/" 2>/dev/null || true
  fi
  # Focaltech FT8006S app firmware — IC boots into boot mode without it
  # (fts_spi downloads this to PRAM at every probe, like the Android driver).
  if [ -f "$ROOT/initramfs/lib/firmware/focaltech_ts_fw_xinli.bin" ]; then
    cp "$ROOT/initramfs/lib/firmware/focaltech_ts_fw_xinli.bin" "$IR/lib/firmware/"
  fi
  # Extra firmware tree (WCN3990 modem/wlan/bt, ath10k board, etc.)
  if [ -d "$ROOT/initramfs/lib/firmware" ]; then
    cp -r "$ROOT/initramfs/lib/firmware/." "$IR/lib/firmware/" 2>/dev/null || true
  fi
  # /readonly/ tree at root for modem TQFT absolute-path requests
  if [ -d "$ROOT/initramfs/readonly" ]; then
    cp -r "$ROOT/initramfs/readonly" "$IR/"
  fi
  # tqftpserv + shared libs (dynamic aarch64): serves modem TQFT requests
  # for wlanmdsp.mbn during remoteproc boot (WCN3990 Wi-Fi bring-up).
  if [ -f "$ROOT/initramfs/tqftpserv" ]; then
    for t in tqftpserv qrtr-ns pd-mapper rmtfs qrtr-lookup; do
      [ -f "$ROOT/initramfs/$t" ] && cp "$ROOT/initramfs/$t" "$IR/bin/$t"
    done
    mkdir -p "$IR/lib" "$IR/usr/lib"
    cp "$ROOT"/initramfs/lib/ld-linux-aarch64.so.1 "$IR/lib/" 2>/dev/null
    cp "$ROOT"/initramfs/lib/libc.so.6 "$IR/usr/lib/" 2>/dev/null
    cp "$ROOT"/initramfs/lib/libqrtr.so.1.0 "$IR/usr/lib/" 2>/dev/null
    cp "$ROOT"/initramfs/lib/libudev.so.1.7.8 "$IR/usr/lib/" 2>/dev/null
    cp "$ROOT"/initramfs/lib/libcap.so.2.66 "$IR/usr/lib/" 2>/dev/null
    ln -sfn libqrtr.so.1.0 "$IR/usr/lib/libqrtr.so.1"
    ln -sfn libudev.so.1.7.8 "$IR/usr/lib/libudev.so.1"
    ln -sfn libcap.so.2.66 "$IR/usr/lib/libcap.so.2"
  fi
  # vibtest: FF rumble ioctl helper (static aarch64)
  if [ -f "$ROOT/initramfs/vibtest" ]; then
    cp "$ROOT/initramfs/vibtest" "$IR/bin/vibtest"
  fi
  # ttyexec: setsid+TIOCSCTTY helper so shells get a real controlling tty
  # (busybox cttyhack only works for the active console, not /dev/ttyGS0).
  if [ -f "$ROOT/tools/ttyexec.c" ]; then
    "${CROSS_COMPILE}gcc" -static -O2 -o "$IR/bin/ttyexec" "$ROOT/tools/ttyexec.c"
  elif [ -f "$ROOT/initramfs/ttyexec" ]; then
    cp "$ROOT/initramfs/ttyexec" "$IR/bin/ttyexec"
  fi
  if [ -f "$ROOT/docs/audio/capture/asoc-dump.c" ]; then
    "${CROSS_COMPILE}gcc" -static -O2 -o "$IR/bin/asoc-dump" \
      "$ROOT/docs/audio/capture/asoc-dump.c"
  fi
  cp "$ROOT/initramfs/display-console-rescue-init" "$IR/init"
  chmod 755 "$IR/init"
  ( cd "$IR" && find . | cpio -o -H newc 2>/dev/null | xz -9e --check=crc32 > "$BUILD/rescue.cpio.xz" )
}

clone_mkbootimg() {
  if [ ! -d "$MKBOOTIMG_SRC/.git" ]; then
    rm -rf "$MKBOOTIMG_SRC"
    git clone --depth 1 https://android.googlesource.com/platform/system/tools/mkbootimg "$MKBOOTIMG_SRC"
  fi
}

pack_boot() {
  local img="$OUT/boot-display-console-rescue.img"
  mkdir -p "$OUT"
  python3 "$MKBOOTIMG_SRC/mkbootimg.py" \
    --header_version 2 \
    --kernel "$KERN_OUT/arch/arm64/boot/Image.gz" \
    --ramdisk "$BUILD/rescue.cpio.xz" \
    --dtb "$KERN_OUT/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb" \
    --pagesize 2048 --base 0x0 \
    --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
    --tags_offset 0x100 --dtb_offset 0x1f00000 \
    --os_version 16.0.0 --os_patch_level 2026-06 \
    --cmdline 'console=tty0 earlycon consoleblank=0 quiet loglevel=3 initcall_debug log_buf_len=2M pd_ignore_unused clk_ignore_unused deferred_probe_timeout=120 fw_devlink=off panic=15 hung_task_panic=1 nmi_watchdog=0 softlockup_panic=0 hardlockup_panic=1' \
    --output "$img"
  python3 -c "import os; s=os.path.getsize('$img'); print('$img', round(s/1048576,2), 'MiB', 'OK' if s<57500000 else 'TOO_BIG')"
  sha256sum "$img" > "$OUT/SHA256SUMS"
  cat "$OUT/SHA256SUMS"
}

check_deps
clone_kernel
apply_patch
build_kernel
build_busybox
build_tinyalsa
build_initramfs
clone_mkbootimg
pack_boot

echo "Done. Output: $OUT/boot-display-console-rescue.img"
