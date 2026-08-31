#!/bin/bash
# Capture a boot snapshot to cust (persistent) for hang diagnosis.
set +e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/sbin
DIR=/var/log/rain
mkdir -p "$DIR"
STAMP=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || date +%s)
OUT="$DIR/boot-$STAMP.log"
{
  echo "=== rain boot report $STAMP ==="
  echo "host=$(cat /etc/hostname 2>/dev/null) uname=$(uname -a)"
  echo "os=$(grep PRETTY_NAME /etc/os-release 2>/dev/null)"
  echo "uptime=$(cat /proc/uptime 2>/dev/null)"
  echo
  echo "=== remoteproc ==="
  for d in /sys/class/remoteproc/remoteproc*; do
    [ -d "$d" ] || continue
    echo "$(basename "$d") state=$(cat "$d/state" 2>/dev/null) recovery=$(cat "$d/recovery" 2>/dev/null)"
  done
  echo
  echo "=== failed units ==="
  systemctl --failed --no-pager --full 2>/dev/null
  echo
  echo "=== interesting unit status ==="
  for u in rain-stable-boot usb-acm-gadget rain-serial-console qcom-wifi-bringup \
           multipathd tqftpserv rmtfs pd-mapper NetworkManager snapd cloud-init; do
    echo "--- $u ---"
    systemctl is-enabled "$u" 2>&1
    systemctl is-active "$u" 2>&1
    systemctl status "$u" --no-pager -l 2>&1 | head -20
  done
  echo
  echo "=== dmesg (last 200) ==="
  dmesg -T 2>/dev/null | tail -200 || dmesg | tail -200
  echo
  echo "=== journal -b (priority err..alert, 300 lines) ==="
  journalctl -b -p err..alert --no-pager -n 300 2>/dev/null
  echo
  echo "=== journal -b (last 150 lines) ==="
  journalctl -b --no-pager -n 150 2>/dev/null
  echo
  echo "=== end ==="
} >"$OUT" 2>&1

ln -sfn "$(basename "$OUT")" "$DIR/latest.log"
# keep last 10 boot reports
ls -1t "$DIR"/boot-*.log 2>/dev/null | tail -n +11 | xargs -r rm -f
chmod 644 "$OUT" "$DIR/latest.log" 2>/dev/null || true
echo "$OUT"
exit 0
