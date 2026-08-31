#!/bin/bash
# Associate + DHCP. Requires wlan0 from qcom-wifi-start.sh (modem+ath10k).
#
# Safe path for rain/fog ath10k_snoc:
#  - NO systemctl (deadlocks / races)
#  - NO `iw` (soft-hang)
#  - NO kill+restart of wpa while scanning
#  - NO explicit wpa scan loops (let assoc drive discovery)
#  - NO `exec > >(tee)` (bash freezes waiting for process-subst)
# Prefer: sudo rain-wifi connect
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/qcom-wifi-connect.log
touch "$LOG"

main() {
echo "=== $(date -Is) wifi-connect ==="

SSID="${1:-${WIFI_SSID:-Xiaomi_E4C4}}"
PASS="${2:-${WIFI_PASS:-GP54006948}}"
CTRL=/run/wpa_supplicant
CONF=/run/qcom-wpa.conf
ASSOC_MAX="${WIFI_ASSOC_TIMEOUT:-25}"

wpa_cmd() {
  timeout 3 wpa_cli -p "$CTRL" -i wlan0 "$@" 2>/dev/null \
    || timeout 3 wpa_cli -i wlan0 "$@" 2>/dev/null
}

wpa_st() {
  wpa_cmd status 2>/dev/null | sed -n 's/^wpa_state=//p'
}

ip link show wlan0 >/dev/null 2>&1 || {
  echo "FATAL: no wlan0 — run: sudo qcom-wifi-start.sh"
  return 1
}
[ "$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null)" = running ] || \
  echo "WARN: modem not running — connect may fail"

pkill -x NetworkManager 2>/dev/null || true
pkill -x udhcpc 2>/dev/null || true

ip link set wlan0 up 2>/dev/null || true
mkdir -p "$CTRL"
chmod 775 "$CTRL"
chgrp netdev "$CTRL" 2>/dev/null || true

# Reuse existing wpa — killing it mid-scan soft-hangs the board
if pgrep -x wpa_supplicant >/dev/null; then
  echo "reusing wpa_supplicant (abort_scan + reconfigure network)"
  wpa_cmd abort_scan >/dev/null
  sleep 0.3
else
  cat >"$CONF" <<CFG
ctrl_interface=$CTRL
ctrl_interface_group=netdev
update_config=0
ap_scan=1
bss_max_count=100
network={
	ssid="$SSID"
	psk="$PASS"
	key_mgmt=WPA-PSK
	proto=RSN
	pairwise=CCMP
	group=CCMP TKIP
	scan_ssid=1
}
CFG
  chmod 600 "$CONF"
  : >/var/log/wpa_supplicant-wlan0.log
  wpa_supplicant -B -i wlan0 -c "$CONF" -P /run/wpa_supplicant-wlan0.pid \
    -f /var/log/wpa_supplicant-wlan0.log
  sleep 1
  if ! pgrep -x wpa_supplicant >/dev/null; then
    echo "FATAL: wpa_supplicant died"
    tail -40 /var/log/wpa_supplicant-wlan0.log 2>/dev/null
    return 1
  fi
fi

echo "associating to $SSID (max ${ASSOC_MAX}s, no forced scan) ..."
wpa_cmd remove_network all >/dev/null
nid=$(wpa_cmd add_network | tr -d '\r' | awk '/^[0-9]+$/ {print; exit}')
if [ -z "$nid" ]; then
  echo "FATAL: add_network failed"
  return 1
fi
wpa_cmd set_network "$nid" ssid "\"$SSID\"" >/dev/null || return 1
wpa_cmd set_network "$nid" psk "\"$PASS\"" >/dev/null || return 1
wpa_cmd set_network "$nid" key_mgmt WPA-PSK >/dev/null
wpa_cmd set_network "$nid" scan_ssid 1 >/dev/null
wpa_cmd enable_network "$nid" >/dev/null
wpa_cmd select_network "$nid" >/dev/null

ok=0
for i in $(seq 1 "$ASSOC_MAX"); do
  st=$(wpa_st)
  echo "t+${i}s wpa_state=${st:-?}"
  if [ "$st" = COMPLETED ]; then ok=1; break; fi
  if [ "$st" = INTERFACE_DISABLED ]; then
    ip link set wlan0 up 2>/dev/null || true
  fi
  sleep 1
done

if [ $ok -ne 1 ]; then
  echo "FATAL: association timeout (wpa left running — do NOT pkill it; retry or reboot)"
  wpa_cmd status 2>&1 | head -25
  tail -30 /var/log/wpa_supplicant-wlan0.log 2>/dev/null
  return 1
fi

BB=/usr/local/bin/busybox
[ -x "$BB" ] || BB=busybox
timeout 45 "$BB" udhcpc -i wlan0 -n -q -t 6 -T 2 -s /usr/local/sbin/udhcpc-wlan.script
ip -br addr show wlan0
ip route | head -5
timeout 8 ping -c 2 -W 2 1.1.1.1
echo "=== wifi-connect done ==="
return 0
}

main "$@" 2>&1 | tee -a "$LOG"
exit "${PIPESTATUS[0]}"
