#!/bin/bash
# Load ath10k_snoc after modem is running.
# Gives the WLAN DSP / QRTR services time to settle before WMI connect
# (avoids -110 timeouts).
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

RP=/sys/class/remoteproc/remoteproc0

# Verify modem is still running
[ "$(cat "$RP/state" 2>/dev/null)" = running ] || {
  echo "FATAL: modem not running, cannot load ath10k_snoc" >&2
  exit 1
}

# Settle time for WLAN DSP / QRTR services
echo "settling 3s before ath10k_snoc..."
sleep 3

[ "$(cat "$RP/state")" = running ] || {
  echo "FATAL: modem dropped during settle" >&2
  exit 1
}

# Load ath10k_snoc (built-in =y, but probe may need a nudge)
# If built-in, the device should already be probed; just bring wlan0 up.
if ip link show wlan0 >/dev/null 2>&1; then
  echo "wlan0 already exists"
else
  modprobe ath10k_snoc 2>/dev/null || true
fi

# Wait for wlan0 to appear
wlan=0
for i in $(seq 1 20); do
  ip link show wlan0 >/dev/null 2>&1 && { wlan=1; break; }
  [ "$(cat "$RP/state")" = running ] || {
    echo "FATAL: modem dropped while waiting for wlan0" >&2
    exit 1
  }
  sleep 1
done

if [ $wlan -eq 1 ]; then
  ip link set wlan0 up 2>/dev/null || true
  echo "OK_wlan0"
else
  echo "WARN_no_wlan0 — ath10k_snoc may need more time"
  dmesg | grep -iE 'ath10k|wlan' | tail -20
fi

exit 0
