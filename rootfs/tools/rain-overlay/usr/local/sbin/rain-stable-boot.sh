#!/bin/bash
# Earliest userspace guard: ensure clean modem/WiFi state at boot.
# This runs on sysinit.target BEFORE the qcom-* service chain.
# NEVER call blocking `systemctl stop/mask` here — this unit runs during
# sysinit and will deadlock the job engine (TimeoutStartSec → failed).
#
# 2026-08-31: NM/rmtfs/pd-mapper/tqftpserv masks removed — the original
# soft-hang was caused by UFS memory corruption (now fixed), not by NM.
set +e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/sbin
LOG=/var/log/rain-stable-boot.log
# line-buffered so a SIGTERM still leaves a trail
exec >>"$LOG" 2>&1
echo "=== $(date -Is) rain-stable-boot ==="

# Kill any stale QMI/WiFi processes from a previous boot
pkill -x tqftpserv 2>/dev/null || true
pkill -x rmtfs 2>/dev/null || true
pkill -x pd-mapper 2>/dev/null || true
pkill -x ModemManager 2>/dev/null || true

# Stop any running remoteproc (modem) — qcom-modem-start will restart it
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

# Remove stale ath10k modules (qcom-modem-start + ath10k-snoc-load will reload)
for m in ath10k_snoc ath10k_core ath10k_pci ath10k_sdio; do
  lsmod 2>/dev/null | grep -q "^$m" && rmmod "$m" 2>/dev/null || true
done

# Remove stale masks from previous boots (rain-stable-boot used to mask these)
for u in NetworkManager.service NetworkManager-wait-online.service \
         tqftpserv.service rmtfs.service pd-mapper.service \
         netplan-wpa-wlan0.service wpa_supplicant.service; do
  [ -L "/etc/systemd/system/$u" ] && \
    [ "$(readlink "/etc/systemd/system/$u")" = "/dev/null" ] && \
    rm -f "/etc/systemd/system/$u" 2>/dev/null
done

# wlan0 only after qcom-wifi-start — never leave wifi netplan active across reboot
if [ -f /etc/netplan/99-rain-wifi.yaml ]; then
  mv -f /etc/netplan/99-rain-wifi.yaml /etc/netplan/99-rain-wifi.yaml.disabled
  echo "disabled netplan wifi (was active at boot)"
fi

echo "=== stable-boot done ==="
exit 0
