#!/bin/bash
# Manual Wi-Fi bring-up for rain/fog. Do NOT touch USB gadget here —
# rebinding UDC drops ttyGS0 and looks like a hang.
#
# Logging: never `exec > >(tee …)` — bash waits on that process-subst and
# freezes the calling shell (looks like a full system hang over adb/ssh).
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/qcom-modem-start.log
touch "$LOG"

main() {
echo "=== $(date -Is) modem-start ==="

RP=/sys/class/remoteproc/remoteproc0
# Idempotent: already have live stack
if [ -d "$RP" ] && [ "$(cat "$RP/state" 2>/dev/null)" = running ] \
   && ip link show wlan0 >/dev/null 2>&1 \
   && pgrep -x tqftpserv >/dev/null && pgrep -x rmtfs >/dev/null; then
  # Never `iw` here — soft-hangs ath10k_snoc on this board
  ip link set wlan0 up 2>/dev/null || true
  echo "stack already up — OK_wlan0 (skip restart)"
  echo "Next: sudo qcom-wifi-connect.sh   # or: sudo rain-wifi connect"
  echo "=== modem-start done ==="
  return 0
fi
[ -d "$RP" ] || { echo "no modem remoteproc"; return 1; }

# No systemctl — early/parallel jobs deadlock; pkill is enough
pkill -x NetworkManager 2>/dev/null || true
[ -f /run/rain-depmod.ok ] || { depmod -a 2>/dev/null || true; : >/run/rain-depmod.ok; }

mkdir -p /lib/firmware/qcom/sm6225 /var/lib/tqftpserv
chmod 755 /var/lib/tqftpserv
: >/var/lib/tqftpserv/lctoem.tmp
: >/var/lib/tqftpserv/mcfg.tmp
chmod 600 /var/lib/tqftpserv/lctoem.tmp /var/lib/tqftpserv/mcfg.tmp

cp -n /lib/firmware/*.jsn /lib/firmware/qcom/sm6225/ 2>/dev/null || true
if [ ! -f /lib/firmware/qcom/sm6225/wlanmdsp.mbn ]; then
  for c in /lib/firmware/wlanmdsp.mbn /lib/firmware/ath10k/WCN3990/hw1.0/wlanmdsp.mbn; do
    [ -f "$c" ] && cp -a "$c" /lib/firmware/qcom/sm6225/wlanmdsp.mbn && break
  done
fi
echo "fw check:"
ls -l /lib/firmware/qcom/sm6225/modem.mdt /lib/firmware/qcom/sm6225/wlanmdsp.mbn \
  /lib/firmware/qcom/sm6225/modem_pr/mcfg/configs/mcfg_hw/mbn_hw.dig \
  /lib/firmware/qcom/sm6225/modem_pr/mcfg/configs/mcfg_sw/mbn_sw.dig 2>&1

for m in qcom_pdr_msg pdr_interface rmtfs_mem \
         libarc4 rfkill cfg80211; do
  modprobe "$m" 2>/dev/null || true
done
insmod /lib/modules/$(uname -r)/kernel/drivers/soc/qcom/rmtfs_mem.ko 2>/dev/null || true
[ -e /dev/qcom_rmtfs_mem1 ] || { echo "FATAL: no rmtfs_mem"; return 1; }

mkdir -p /var/lib/rmtfs
for u in /sys/block/*/*/uevent; do
  [ -f "$u" ] || continue
  pn=$(sed -n 's/^PARTNAME=//p' "$u")
  case "$pn" in
    modemst1) tgt=modem_fs1 ;; modemst2) tgt=modem_fs2 ;;
    fsg) tgt=fsg ;; fsc) tgt=fsc ;; *) continue ;;
  esac
  ln -sfn "/dev/$(basename "$(dirname "$u")")" "/var/lib/rmtfs/$tgt"
done

# Driver before PAS stop — reverse order soft-hangs
[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"
if lsmod 2>/dev/null | grep -q '^ath10k_snoc'; then
  echo "rmmod ath10k_snoc before modem restart"
  rmmod ath10k_snoc 2>/dev/null || true
  sleep 0.5
fi

st=$(cat "$RP/state")
echo "modem was $st — stop before userspace"
if [ "$st" != offline ]; then
  echo stop >"$RP/state" 2>/dev/null || true
  for i in $(seq 1 20); do
    [ "$(cat "$RP/state")" = offline ] && break
    sleep 1
  done
  sleep 1
fi

TQFTP=/usr/local/sbin/tqftpserv
[ -x "$TQFTP" ] || TQFTP=$(command -v tqftpserv)
echo "using tqftpserv=$TQFTP"
if grep -a -q 'firmware_mnt' "$TQFTP" 2>/dev/null; then
  echo "tqftp: vendor paths OK"
else
  echo "WARN: tqftp may lack vendor paths"
fi

pkill -x pd-mapper 2>/dev/null
pkill -x rmtfs 2>/dev/null
pkill -x tqftpserv 2>/dev/null
sleep 0.5
: >/var/log/tqftpserv.log

pgrep -x qrtr-ns >/dev/null || qrtr-ns -f 1 &
sleep 0.5
pd-mapper &
sleep 0.5
pgrep -x pd-mapper >/dev/null || { echo "FATAL: pd-mapper"; return 1; }
rmtfs -s -o /var/lib/rmtfs &
sleep 0.5
pgrep -x rmtfs >/dev/null || { echo "FATAL: rmtfs"; return 1; }
("$TQFTP" >>/var/log/tqftpserv.log 2>&1 &)
sleep 1
pgrep -x tqftpserv >/dev/null || { echo "FATAL: tqftpserv"; return 1; }

[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"
echo start >"$RP/state" || { echo "FATAL: modem start"; return 1; }
ok=0
for i in $(seq 1 40); do
  st=$(cat "$RP/state"); echo "t+$i state=$st"
  case "$st" in
    running) ok=$((ok+1)); [ $ok -ge 5 ] && break ;;
    crashed|offline) echo "FATAL: modem $st"; tail -50 /var/log/tqftpserv.log; return 1 ;;
    *) ok=0 ;;
  esac
  sleep 1
done
[ "$(cat "$RP/state")" = running ] || { echo "FATAL: not stable"; return 1; }
echo "--- tqftp ---"
grep -E 'wlanmdsp|reject|unable|RRQ|WRQ|mcfg|lctoem|open' /var/log/tqftpserv.log | tail -20

sleep 1

lsmod | grep -q '^ath10k_snoc' || \
  insmod /lib/modules/$(uname -r)/kernel/drivers/net/wireless/ath/ath10k/ath10k_snoc.ko || \
  modprobe ath10k_snoc || { echo "FATAL: snoc"; return 1; }

wlan=0
for i in $(seq 1 30); do
  ip link show wlan0 >/dev/null 2>&1 && { wlan=1; break; }
  [ "$(cat "$RP/state")" = running ] || {
    echo "FATAL: modem dropped"; rmmod ath10k_snoc 2>/dev/null
    echo stop >"$RP/state" 2>/dev/null; return 1
  }
  sleep 1
done
if [ $wlan -eq 1 ]; then
  ip link set wlan0 up
  echo OK_wlan0
else
  echo WARN_no_wlan0
  dmesg | egrep -i 'ath10k|wlan|firmware|handover|q6v5' | tail -40
fi

ip -br link
echo "modem=$(cat "$RP/state")"
echo "Next: sudo qcom-wifi-connect.sh   # or: sudo rain-wifi connect"
echo "=== modem-start done ==="
return 0
}

main "$@" 2>&1 | tee -a "$LOG"
exit "${PIPESTATUS[0]}"
