#!/bin/bash
# EXPERIMENTAL. NM + ath10k/MPSS soft-hangs this SoC in either order.
# Default: refuse. Only with FORCE_NM_ONLY=1 (modem stays OFF — no Wi-Fi).
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

if [ "${FORCE_NM_ONLY:-0}" != 1 ]; then
  echo "REFUSED: NetworkManager cannot coexist with Wi-Fi on this board (soft hang)."
  echo "Use: sudo qcom-wifi-start.sh && sudo qcom-wifi-connect.sh"
  echo "NM-without-modem lab only: FORCE_NM_ONLY=1 sudo -E $0 --full"
  exit 2
fi

MARK=/var/log/rain/nm-safe.step
LOG=/var/log/rain/nm-safe.log
mkdir -p /var/log/rain /etc/NetworkManager/conf.d /etc/systemd/system/NetworkManager.service.d
exec > >(tee -a "$LOG") 2>&1
mark() { echo "$1 $(date -Is)" | tee "$MARK"; sync; sync; }

echo "=== $(date -Is) FORCE NM-only (no Wi-Fi) ==="
cat >/etc/systemd/system/NetworkManager.service.d/rain.conf <<'EOF'
[Service]
TimeoutStartSec=30
Restart=no
EOF
cat >/etc/NetworkManager/conf.d/10-globally-managed-devices.conf <<'EOF'
[keyfile]
unmanaged-devices=*
EOF
cat >/etc/NetworkManager/conf.d/99-rain-wifi.conf <<'EOF'
[main]
plugins=keyfile
dns=none
rc-manager=unmanaged
no-auto-default=*
[connectivity]
enabled=false
[keyfile]
unmanaged-devices=*
EOF

pkill -x wpa_supplicant 2>/dev/null || true
pkill -x udhcpc 2>/dev/null || true
rmmod ath10k_snoc 2>/dev/null || true
if [ -e /sys/class/remoteproc/remoteproc0/state ]; then
  echo stop >/sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
fi
pkill -x tqftpserv 2>/dev/null || true
pkill -x rmtfs 2>/dev/null || true
pkill -x pd-mapper 2>/dev/null || true

systemctl mask ModemManager NetworkManager-wait-online 2>/dev/null || true
systemctl unmask NetworkManager 2>/dev/null || true
systemctl daemon-reload
mark "before-nm"
timeout 20 systemctl start NetworkManager || { mark FAIL; systemctl mask NetworkManager; exit 1; }
mark "nm-ok"
nmcli -t general status
echo "NM-only OK — do NOT start qcom-wifi while NM is running"
exit 0
