#!/bin/bash
# ExecStartPre for stock NetworkManager on rain/fog.
# Prepares modem+ath10k but does NOT run our wpa client — NM must be the
# first userspace owner of wlan0 (stealing a live wpa session reboots).
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/rain/nm-prestart.log
mkdir -p /var/log/rain
exec >>"$LOG" 2>&1
echo "=== $(date -Is) nm-prestart ==="

# ModemManager + package QMI units must stay away from QRTR
systemctl stop ModemManager 2>/dev/null || true
systemctl mask ModemManager 2>/dev/null || true
systemctl stop wpa_supplicant 2>/dev/null || true
pkill -x wpa_supplicant 2>/dev/null || true
pkill -x udhcpc 2>/dev/null || true

# Ensure wlan0 exists (modem PAS + ath10k_snoc)
if ! ip link show wlan0 >/dev/null 2>&1; then
  /usr/local/sbin/qcom-wifi-start.sh || {
    echo "FATAL: qcom-wifi-start failed"
    exit 1
  }
fi

iw dev wlan0 set power_save off 2>/dev/null || true
ip link set wlan0 up 2>/dev/null || true
# Leave interface idle — no association yet
echo "wlan0 ready for NM (idle)"
exit 0
