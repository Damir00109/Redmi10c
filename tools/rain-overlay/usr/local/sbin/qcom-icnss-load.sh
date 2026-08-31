#!/bin/bash
# Load msm_icnss + wlan after modem is already running (qcom-modem-start.sh).
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/qcom-icnss-load.log
KVER=$(uname -r)
EXTRA=/lib/modules/$KVER/extra
RP=/sys/class/remoteproc/remoteproc0
{
echo "=== $(date -Is) icnss-load ==="
[ "$(cat "$RP/state" 2>/dev/null)" = running ] || { echo "FATAL: modem not running"; exit 1; }
[ -f "$EXTRA/msm_icnss.ko" ] || { echo "FATAL: no msm_icnss.ko"; exit 1; }
[ -f "$EXTRA/wlan.ko" ] || { echo "FATAL: no wlan.ko"; exit 1; }

# firmware for qcacld — always refresh INI (IPA must stay off on mainline)
mkdir -p /lib/firmware/wlan/qca_cld
if [ -f /lib/firmware/qcom/sm6225/wlan/WCNSS_qcom_cfg.ini ]; then
  cp -f /lib/firmware/qcom/sm6225/wlan/WCNSS_qcom_cfg.ini /lib/firmware/wlan/qca_cld/
  echo "INI gIPAConfig=$(grep -m1 '^gIPAConfig=' /lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini)"
fi
[ -f /lib/firmware/wlan/qca_cld/bdwlan.bin ] || \
  cp /lib/firmware/qcom/sm6225/wlan/bd3qvdfu.bin /lib/firmware/wlan/qca_cld/bdwlan.bin 2>/dev/null

if lsmod | grep -q '^ath10k'; then
  echo "FATAL: unload ath10k first"; exit 1
fi

if ! lsmod | grep -q '^msm_icnss '; then
  echo "insmod msm_icnss"
  if ! insmod "$EXTRA/msm_icnss.ko"; then
    echo "FATAL: msm_icnss"; dmesg | tail -50; exit 1
  fi
  sleep 2
fi
dmesg | grep -iE 'icnss' | tail -30

if ! lsmod | grep -q '^wlan '; then
  echo "insmod wlan"
  if ! insmod "$EXTRA/wlan.ko"; then
    echo "FATAL: wlan"; dmesg | tail -80; exit 1
  fi
fi

for i in $(seq 1 45); do
  if ip link show wlan0 >/dev/null 2>&1; then
    echo OK_wlan0_present
    ip -br link show wlan0
    # Default: do NOT auto-up (RX fill used to soft-hang). Set RAIN_WIFI_UP=1 to try.
    if [ "${RAIN_WIFI_UP:-0}" = 1 ]; then
      echo "ip link set wlan0 up (RAIN_WIFI_UP=1)"
      ip link set wlan0 up
      echo UP:$?
      ip -br link show wlan0
    else
      echo "skip up — run: RAIN_WIFI_UP=1 $0   or: ip link set wlan0 up"
    fi
    exit 0
  fi
  echo "wait wlan0 t+$i"
  sleep 1
done
echo WARN_no_wlan0
dmesg | grep -iE 'icnss|wlan|cnss|qcacld|firmware|msa|qmi|pdr' | tail -80
exit 1
} 2>&1 | tee -a "$LOG"
exit ${PIPESTATUS[0]}
