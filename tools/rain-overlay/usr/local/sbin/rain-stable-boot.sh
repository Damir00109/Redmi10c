#!/bin/bash
# Earliest userspace guard: modem offline, no WiFi, kill QMI helpers.
# NEVER call blocking `systemctl stop/mask` here — this unit runs during
# sysinit and will deadlock the job engine (TimeoutStartSec → failed).
set +e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/sbin
LOG=/var/log/rain-stable-boot.log
# line-buffered so a SIGTERM still leaves a trail
exec >>"$LOG" 2>&1
echo "=== $(date -Is) rain-stable-boot ==="

pkill -x tqftpserv 2>/dev/null || true
pkill -x rmtfs 2>/dev/null || true
pkill -x pd-mapper 2>/dev/null || true
pkill -x NetworkManager 2>/dev/null || true
pkill -x ModemManager 2>/dev/null || true
pkill -x wpa_supplicant 2>/dev/null || true

for RP in /sys/class/remoteproc/remoteproc*; do
  [ -d "$RP" ] || continue
  [ -e "$RP/recovery" ] && echo disabled >"$RP/recovery" 2>/dev/null
  st=$(cat "$RP/state" 2>/dev/null)
  name=$(basename "$RP")
  echo "$name was $st"
  case "$st" in
    running|starting|crashed|stopping)
      echo stop >"$RP/state" 2>/dev/null || true
      for i in 1 2 3 4 5 6 7 8; do
        [ "$(cat "$RP/state" 2>/dev/null)" = offline ] && break
        sleep 0.5
      done
      echo "$name now $(cat "$RP/state" 2>/dev/null)"
      ;;
  esac
done

for m in ath10k_snoc ath10k_core ath10k_pci ath10k_sdio \
         sh366101_fg_bringup smb1351_charger_bringup; do
  lsmod 2>/dev/null | grep -q "^$m" && rmmod "$m" 2>/dev/null || true
done

# Static masks via symlink (no dbus / no job engine). Idempotent.
mask_unit() {
  local u=$1
  ln -sfn /dev/null "/etc/systemd/system/$u" 2>/dev/null || true
}
mask_unit NetworkManager.service
mask_unit NetworkManager-wait-online.service
mask_unit ModemManager.service
mask_unit tqftpserv.service
mask_unit rmtfs.service
mask_unit pd-mapper.service
mask_unit netplan-wpa-wlan0.service

# wlan0 only after qcom-wifi-start — never leave wifi netplan active across reboot
if [ -f /etc/netplan/99-rain-wifi.yaml ]; then
  mv -f /etc/netplan/99-rain-wifi.yaml /etc/netplan/99-rain-wifi.yaml.disabled
  echo "disabled netplan wifi (was active at boot)"
fi

echo "=== stable-boot done ==="
exit 0
