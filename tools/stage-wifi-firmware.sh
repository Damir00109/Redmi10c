#!/usr/bin/env bash
# Stage Wi-Fi / modem firmware into out/initramfs-root and rebuild cpio.gz
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IR="$ROOT/out/initramfs-root"
FW="$IR/lib/firmware"
PHONE_FW="$ROOT/firmware/from-phone/firmware_mnt/image"
ATH_SRC=/lib/firmware/ath10k/WCN3990/hw1.0

mkdir -p "$FW/ath10k/WCN3990/hw1.0" "$FW/qcom/sm6225"

echo "Staging ath10k WCN3990..."
for f in board-2.bin firmware-5.bin wlanmdsp.mbn; do
  if [ -f "$ATH_SRC/$f" ]; then
    cp -a "$ATH_SRC/$f" "$FW/ath10k/WCN3990/hw1.0/"
  elif [ -f "$ATH_SRC/$f.zst" ]; then
    zstd -d -f -o "$FW/ath10k/WCN3990/hw1.0/$f" "$ATH_SRC/$f.zst"
  else
    echo "WARN: missing $f" >&2
  fi
done

echo "Staging modem.mdt set from phone firmware_mnt (~50MB)..."
if ls "$PHONE_FW"/modem.mdt >/dev/null 2>&1; then
  cp -a "$PHONE_FW"/modem.mdt "$PHONE_FW"/modem.b* "$FW/qcom/sm6225/" 2>/dev/null || \
    cp -a "$PHONE_FW"/modem.* "$FW/qcom/sm6225/"
else
  echo "ERROR: no modem.* in $PHONE_FW" >&2
  exit 1
fi

# Optional: keep Android BDF for later board-2 generation
mkdir -p "$FW/qcom/sm6225/wlan"
cp -a "$ROOT/notes/android-wifi-live/fw/bd3qvdfu.bin" "$FW/qcom/sm6225/wlan/" 2>/dev/null || true
cp -a "$ROOT/notes/android-wifi-live/fw/WCNSS_qcom_cfg.ini" "$FW/qcom/sm6225/wlan/" 2>/dev/null || true

echo "Rebuild initramfs.cpio.gz..."
( cd "$IR" && find . | cpio -o -H newc 2>/dev/null | gzip -n > "$ROOT/out/initramfs.cpio.gz" )
ls -lh "$ROOT/out/initramfs.cpio.gz"
du -sh "$FW/ath10k" "$FW/qcom/sm6225"
echo "OK"
