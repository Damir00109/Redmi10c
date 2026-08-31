#!/bin/bash
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
SSID="${1:-2.4GHz_WiFi_219}"
PASS="${2:-GP54006948}"
echo "=== ONESHOT begin $(date -Is) ==="
rain-mmc-park off
systemctl mask qcom-wifi-bringup NetworkManager 2>/dev/null || true
pkill -x NetworkManager 2>/dev/null || true
pkill -x wpa_supplicant 2>/dev/null || true

qcom-wifi-start.sh || { echo FATAL_START; exit 1; }
echo MARK_POST_START
dmesg | grep htt-ver | tail -1

# Do not bounce the link — down/up after probe soft-hangs this board.
ip link set wlan0 address f0:6c:5d:02:36:a2 2>/dev/null || true
ip link set wlan0 up 2>/dev/null || true

mkdir -p /run/wpa_supplicant
cat >/run/qcom-wpa.conf <<EOF
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=netdev
update_config=0
ap_scan=1
network={
	ssid="$SSID"
	psk="$PASS"
	key_mgmt=WPA-PSK
}
EOF
chmod 600 /run/qcom-wpa.conf
# Log to tmpfs — UFS writes during assoc soft-hang
wpa_supplicant -B -i wlan0 -c /run/qcom-wpa.conf \
  -P /run/wpa_supplicant-wlan0.pid -f /run/wpa_supplicant-wlan0.log
for i in $(seq 1 40); do
  st=$(wpa_cli -p /run/wpa_supplicant -i wlan0 status 2>/dev/null | sed -n 's/^wpa_state=//p')
  echo "t+${i}s wpa=${st:-?}"
  [ "$st" = COMPLETED ] && break
  sleep 1
done
[ "$st" = COMPLETED ] || { echo ASSOC_FAIL; tail -40 /run/wpa_supplicant-wlan0.log; exit 1; }
echo ASSOC_OK

BB=/usr/local/bin/busybox; [ -x "$BB" ] || BB=busybox
$BB udhcpc -i wlan0 -n -q -t 12 -T 3 -s /usr/local/sbin/udhcpc-wlan.script
ip -br addr show wlan0
ping -c4 -W2 1.1.1.1
echo PING_RC=$?
ping -c2 -W2 8.8.8.8
echo "=== ONESHOT connected ==="

for s in 30 60 90; do
  sleep 30
  if ping -c1 -W2 1.1.1.1 >/dev/null; then
    echo "t+${s}s PING_OK"
  else
    echo "t+${s}s PING_FAIL"
  fi
done
echo WORKING_STABLE
ip -br addr
wpa_cli -p /run/wpa_supplicant -i wlan0 status 2>/dev/null | grep -E 'wpa_state|ssid|ip_address'
exit 0
