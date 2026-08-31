#!/bin/bash
# Bisect which NetworkManager action kills rain/fog.
# Each step syncs a marker to /var/log/rain/nm-bisect.step BEFORE the action.
# After reboot: cat that file — last completed step is the killer candidate.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
MARK=/var/log/rain/nm-bisect.step
LOG=/var/log/rain/nm-bisect.log
mkdir -p /var/log/rain
exec > >(tee -a "$LOG") 2>&1

step() {
  echo "=== $(date -Is) STEP $1: $2 ==="
  echo "$1 $2 $(date -Is)" >"$MARK"
  sync; sync
  sleep 0.3
}

SSID="${1:-2.4GHz_WiFi_219}"
PASS="${2:-GP54006948}"

echo "=== $(date -Is) nm-bisect start ==="
echo "prev marker: $(cat "$MARK" 2>/dev/null || echo none)"

# Preconditions: working wpa path
if ! ping -c1 -W3 1.1.1.1 >/dev/null 2>&1; then
  echo "bringing wifi..."
  /usr/local/sbin/qcom-wifi-start.sh || exit 1
  /usr/local/sbin/qcom-wifi-connect.sh "$SSID" "$PASS" || exit 1
fi
ip -br addr show wlan0
iw dev wlan0 link | head -5

systemctl stop ModemManager 2>/dev/null
systemctl mask ModemManager 2>/dev/null
systemctl stop NetworkManager 2>/dev/null
systemctl unmask NetworkManager 2>/dev/null
systemctl daemon-reload 2>/dev/null

mkdir -p /etc/NetworkManager/conf.d
# Start with wlan COMPLETELY unmanaged
cat >/etc/NetworkManager/conf.d/99-rain-bisect.conf <<'CFG'
[main]
no-auto-default=*
plugins=keyfile

[device]
wifi.powersave=2
wifi.scan-rand-mac-address=no

[keyfile]
unmanaged-devices=interface-name:wlan0;interface-name:usb0;interface-name:rndis0
CFG

step 1 "start-NM-wlan-unmanaged"
systemctl start NetworkManager
sleep 3
systemctl is-active NetworkManager || { echo FAIL_start; exit 1; }
ping -c1 -W3 1.1.1.1 || { echo FAIL_ping_after_nm_start; exit 1; }
echo OK_step1

step 2 "nmcli-general-status"
nmcli -t general status
nmcli -t device status
ping -c1 -W3 1.1.1.1 || exit 1
echo OK_step2

step 3 "write-connection-profile-only"
UUID=$(uuidgen 2>/dev/null || cat /proc/sys/kernel/random/uuid)
CONN=rain-bisect
rm -f /etc/NetworkManager/system-connections/${CONN}.nmconnection
cat >/etc/NetworkManager/system-connections/${CONN}.nmconnection <<CFG
[connection]
id=$CONN
uuid=$UUID
type=wifi
interface-name=wlan0
autoconnect=false

[wifi]
mode=infrastructure
ssid=$SSID
cloned-mac-address=permanent
powersave=2

[wifi-security]
key-mgmt=wpa-psk
psk=$PASS

[ipv4]
method=auto

[ipv6]
method=disabled
CFG
chmod 600 /etc/NetworkManager/system-connections/${CONN}.nmconnection
nmcli connection reload
ping -c1 -W3 1.1.1.1 || exit 1
echo OK_step3

step 4 "allow-manage-wlan0-config-reload"
# Flip unmanaged off, reload NM without killing wpa yet
cat >/etc/NetworkManager/conf.d/99-rain-bisect.conf <<'CFG'
[main]
no-auto-default=*
plugins=keyfile

[device]
wifi.powersave=2
wifi.scan-rand-mac-address=no

[keyfile]
unmanaged-devices=interface-name:usb0;interface-name:rndis0
CFG
nmcli general reload conf
sleep 2
nmcli -t device status
ping -c1 -W3 1.1.1.1 || exit 1
echo OK_step4

step 5 "nmcli-device-set-wlan0-managed-yes"
nmcli device set wlan0 managed yes
sleep 2
nmcli -t device status
# wpa may still own assoc — check survival
ping -c1 -W3 1.1.1.1; echo ping_rc=$?
echo OK_step5

step 6 "kill-manual-wpa-udhcpc-keep-NM"
pkill -x udhcpc 2>/dev/null
pkill -x wpa_supplicant 2>/dev/null
sleep 2
ip -br addr show wlan0
iw dev wlan0 link | head -5
ping -c1 -W3 1.1.1.1; echo ping_rc=$?
echo OK_step6

step 7 "nmcli-connection-up-associate"
nmcli connection up "$CONN" ifname wlan0
sleep 3
nmcli -t device status
ip -br addr show wlan0
ping -c2 -W3 1.1.1.1
echo OK_step7

step 8 "nmcli-device-wifi-rescan"
nmcli device wifi rescan ifname wlan0
sleep 3
ping -c1 -W3 1.1.1.1 || exit 1
echo OK_step8

echo "ALL_STEPS_SURVIVED $(date -Is)" | tee "$MARK"
sync
echo "=== nm-bisect done OK ==="
exit 0
