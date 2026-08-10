#!/usr/bin/env bash
# Full parallel bringup build — host only, never flashes phone.
# Uses all CPU threads + ccache.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS="$(nproc)"
export CCACHE_DIR="${CCACHE_DIR:-$ROOT/.ccache}"
export ARCH=arm64
export CROSS_COMPILE="ccache aarch64-linux-gnu-"
# Prefer clang if present for modern trees
if command -v clang >/dev/null; then
  export LLVM=1
  export CC="ccache clang"
fi

build_one() {
  local name="$1" src="$2" defconfig_hint="$3"
  local out="$ROOT/out/$name"
  mkdir -p "$out" "$ROOT/logs"
  echo "======== BUILD $name ========" | tee -a "$ROOT/logs/build-$name.log"
  if [ ! -d "$src" ]; then
    echo "SKIP $name: missing $src" | tee -a "$ROOT/logs/build-$name.log"
    return 0
  fi
  cd "$src"
  if [ ! -f "$out/.config" ]; then
    if [ -f "arch/arm64/configs/${defconfig_hint}" ]; then
      make O="$out" "$defconfig_hint" 2>&1 | tee -a "$ROOT/logs/build-$name.log"
    else
      make O="$out" defconfig 2>&1 | tee -a "$ROOT/logs/build-$name.log"
    fi
    # Enable common QCOM bringup knobs when symbols exist
    ./scripts/config --file "$out/.config" \
      --enable CONFIG_ARCH_QCOM \
      --enable CONFIG_SERIAL_MSM \
      --enable CONFIG_SERIAL_MSM_CONSOLE \
      --enable CONFIG_DRM \
      --enable CONFIG_DRM_MSM \
      --enable CONFIG_FB_SIMPLE \
      --enable CONFIG_USB_DWC3 \
      --enable CONFIG_USB_DWC3_QCOM \
      --enable CONFIG_PHY_QCOM_QMP \
      --enable CONFIG_QCOM_CLK_RPMH \
      --enable CONFIG_NFC_NXP_NCI \
      --enable CONFIG_NFC_NXP_NCI_I2C \
      --enable CONFIG_BLK_DEV_INITRD \
      --enable CONFIG_DEVTMPFS \
      --enable CONFIG_DEVTMPFS_MOUNT \
      || true
    make O="$out" olddefconfig 2>&1 | tee -a "$ROOT/logs/build-$name.log"
  fi
  echo "Compiling with -j$JOBS ..." | tee -a "$ROOT/logs/build-$name.log"
  make O="$out" -j"$JOBS" Image.gz modules dtbs 2>&1 | tee -a "$ROOT/logs/build-$name.log"
  echo "DONE $name" | tee -a "$ROOT/logs/build-$name.log"
  ls -lh "$out/arch/arm64/boot/Image.gz" 2>/dev/null || true
  find "$out/arch/arm64/boot/dts" -iname '*fog*' -o -iname '*rain*' -o -iname '*sm6225*' 2>/dev/null | head | tee -a "$ROOT/logs/build-$name.log" || true
}

# Sequential heavy builds still use all cores each; avoids RAM thrash of two full trees
build_one "statzar-v6.1" "$ROOT/mainline/linux-sm6225" "defconfig"
build_one "scos-v6.19" "$ROOT/mainline/sm6225-mainline" "defconfig"
build_one "upstream" "$ROOT/mainline/linux" "defconfig"

echo ALL_BUILDS_FINISHED
ccache -s || true
