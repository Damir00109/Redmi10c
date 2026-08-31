#!/bin/bash
# Safe Wi-Fi bring-up for rain Ubuntu: health probes + live kmsg + UFS awake.
# Run ON DEVICE as root, or: adb shell 'bash -s' < this-script
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/rain-wifi-safe.log
KMSG=/var/log/rain-kmsg-wifi.log
EXTRA=/lib/modules/$(uname -r)/extra
RP=/sys/class/remoteproc/remoteproc0

exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) rain-wifi-safe ==="

alive() { echo 0; }

# Keep UFS out of hibern8 during bring-up
for d in /sys/devices/platform/*/ufshc /sys/devices/platform/*/*/ufshc /sys/class/scsi_host/host*/device; do
  [ -e "$d/power/control" ] || continue
  echo on >"$d/power/control" 2>/dev/null
  echo "ufs-pm $d -> on"
done
for d in /sys/block/sd*/device/power/control; do
  [ -f "$d" ] || continue
  echo on >"$d" 2>/dev/null
done

# Live kernel log
: >"$KMSG"
( cat /dev/kmsg >>"$KMSG" ) &
KMSG_PID=$!
trap 'kill $KMSG_PID 2>/dev/null' EXIT

echo "health=$(alive)"

# Modem if needed
st=$(cat "$RP/state" 2>/dev/null)
echo "modem=$st"
if [ "$st" != running ]; then
  qcom-modem-minimal.sh || { echo FATAL_modem; tail -40 "$KMSG"; exit 1; }
fi
echo "health=$(alive) modem=$(cat $RP/state)"

# Firmware INI must have IPA off
mkdir -p /lib/firmware/wlan/qca_cld
cp -f /lib/firmware/qcom/sm6225/wlan/WCNSS_qcom_cfg.ini /lib/firmware/wlan/qca_cld/ 2>/dev/null
echo "INI=$(grep -m1 ^gIPAConfig= /lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini)"
echo "REORDER=$(grep -m1 ^gReorderOffloadSupported= /lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini)"

# Load modules from tmpfs (avoid UFS during probe)
mkdir -p /tmp/rain-ko
cp -f "$EXTRA/msm_icnss.ko" /tmp/rain-ko/ 2>/dev/null
cp -f "$EXTRA/wlan.ko" /tmp/rain-ko/
ls -la /tmp/rain-ko/

if ! lsmod | grep -q '^msm_icnss '; then
  echo "insmod msm_icnss"
  insmod /tmp/rain-ko/msm_icnss.ko || { echo FATAL_icnss; tail -80 "$KMSG"; exit 1; }
  sleep 2
fi
echo "health=$(alive)"
dmesg | grep -iE 'icnss|WLAN FW|BDF' | tail -20

if ! lsmod | grep -q '^wlan '; then
  echo "insmod wlan (fill gated)"
  # run insmod in background so we can health-poll
  insmod /tmp/rain-ko/wlan.ko &
  INSPID=$!
  for i in $(seq 1 90); do
    if ! kill -0 $INSPID 2>/dev/null; then
      wait $INSPID
      echo "insmod_done rc=$?"
      break
    fi
    h=$(alive 2>/dev/null || echo dead)
    echo "t+$i health=$h nets=$(ls /sys/class/net 2>/dev/null | tr '\n' ',')"
    [ "$h" = "0" ] || { echo FATAL_health_lost; tail -100 "$KMSG"; exit 1; }
    sleep 1
  done
fi

for i in $(seq 1 60); do
  h=$(alive 2>/dev/null || echo dead)
  echo "wait_wlan0 t+$i health=$h nets=$(ls /sys/class/net 2>/dev/null | tr '\n' ',')"
  [ "$h" = "0" ] || { echo FATAL_health_lost; tail -100 "$KMSG"; exit 1; }
  ip link show wlan0 >/dev/null 2>&1 && break
  sleep 1
done

if ! ip link show wlan0 >/dev/null 2>&1; then
  echo WARN_no_wlan0
  dmesg | grep -iE 'rain-htt|wlan|icnss|hdd|hif|htt' | tail -80
  exit 1
fi

echo OK_wlan0_down
ip -br link show wlan0
dmesg | grep -iE 'rain-htt|WLAN FW|BDF|hdd' | tail -40
echo "NEXT: RAIN_WIFI_UP=1 ip link set wlan0 up   # enables RX fill"
exit 0
