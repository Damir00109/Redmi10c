#!/bin/bash
# Tear down Wi-Fi safely on rain/fog (ath10k_snoc + MPSS).
#
# HARD RULES:
#  1) Never call `iw` (soft-hangs).
#  2) Never kill wpa mid-scan without abort_scan first.
#  3) Always rmmod ath10k_snoc BEFORE stopping the modem PAS.
#  4) Never call blocking systemctl from early/shutdown paths.
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin

CTRL=/run/wpa_supplicant
RP=/sys/class/remoteproc/remoteproc0
QUIET=${QCOM_WIFI_TEARDOWN_QUIET:-0}

log() { [ "$QUIET" = 1 ] || echo "wifi-teardown: $*"; }

wpa_try() {
  timeout 2 wpa_cli -p "$CTRL" -i wlan0 "$@" >/dev/null 2>&1 \
    || timeout 2 wpa_cli -i wlan0 "$@" >/dev/null 2>&1 \
    || true
}

log "begin"
# Graceful leave (scan→kill without abort = SoC soft-hang)
if pgrep -x wpa_supplicant >/dev/null 2>&1; then
  wpa_try abort_scan
  wpa_try disconnect
  sleep 0.3
fi
pkill -x udhcpc 2>/dev/null || true
pkill -x wpa_supplicant 2>/dev/null || true
sleep 0.2

# Drop carrier without nl80211 "iw"
ip link set wlan0 down 2>/dev/null || true

# Driver MUST go before PAS stop — reverse order hangs shutdown / reboot
if lsmod 2>/dev/null | grep -q '^ath10k_snoc'; then
  log "rmmod ath10k_snoc"
  rmmod ath10k_snoc 2>/dev/null || true
  sleep 0.4
fi

if [ -e "$RP/state" ]; then
  st=$(cat "$RP/state" 2>/dev/null)
  if [ "$st" != offline ]; then
    log "modem stop (was $st)"
    [ -e "$RP/recovery" ] && echo disabled >"$RP/recovery" 2>/dev/null
    echo stop >"$RP/state" 2>/dev/null || true
    for i in 1 2 3 4 5 6 7 8; do
      [ "$(cat "$RP/state" 2>/dev/null)" = offline ] && break
      sleep 0.4
    done
  fi
fi

log "done (modem=$(cat "$RP/state" 2>/dev/null || echo n/a))"
exit 0
