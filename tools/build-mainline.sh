#!/usr/bin/env bash
# Build mainline ARM64 kernel for rain — COMPILE ONLY, never flashes phone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/mainline/linux}"
OUT="$ROOT/out/mainline"
JOBS="$(nproc)"

mkdir -p "$OUT"
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

echo "SRC=$SRC"
echo "OUT=$OUT"
test -d "$SRC" || { echo "missing kernel sources"; exit 1; }

cd "$SRC"
if [ ! -f "$OUT/.config" ]; then
  echo "Seeding defconfig (defconfig + useful qcom bits later)..."
  make O="$OUT" defconfig
  # Soft enable common Qualcomm bringup options if present
  scripts/config --file "$OUT/.config" \
    --enable CONFIG_ARCH_QCOM \
    --enable CONFIG_SERIAL_MSM \
    --enable CONFIG_SERIAL_MSM_CONSOLE \
    --enable CONFIG_DRM_MSM \
    --enable CONFIG_DRM_MSM_DSI \
    --enable CONFIG_PHY_QCOM_QMP \
    --enable CONFIG_USB_DWC3_QCOM \
    --enable CONFIG_QCOM_CLK_RPMH \
    --enable CONFIG_PINCTRL_SM6115 \
    --enable CONFIG_NFC_NXP_NCI \
    --enable CONFIG_NFC_NXP_NCI_I2C \
    || true
  make O="$OUT" olddefconfig
fi

echo "Building Image.gz + modules (no install to phone)..."
make O="$OUT" -j"$JOBS" Image.gz modules dtbs

echo
echo "DONE (host only):"
ls -lh "$OUT/arch/arm64/boot/Image.gz" 2>/dev/null || ls -lh "$OUT/arch/arm64/boot/Image"
echo "DTBs:" 
ls "$OUT/arch/arm64/boot/dts/qcom"/*bengal* "$OUT/arch/arm64/boot/dts/qcom"/*sm6115* 2>/dev/null | head || true
echo
echo "Next (tomorrow, still safe): pack boot and: fastboot boot out/boot-mainline.img"
echo "NEVER run flash from this script."
