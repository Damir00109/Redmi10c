#!/bin/bash
# Wi-Fi + Bluetooth bring-up for rain/fog (SM6225/WCN3990).
# Do NOT touch USB gadget here — rebinding UDC drops ttyGS0.
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/run/qcom-wifi-start.log
: >"$LOG"

main() {
echo "=== $(date -Is) wifi-start ==="

# Park µSD early — sdhci_msm IRQ timeouts soft-hang the board under wifi load
[ -x /usr/local/sbin/rain-mmc-park ] && /usr/local/sbin/rain-mmc-park off
# Keep UFS out of hibern8 during bring-up
echo on > /sys/class/scsi_host/host0/device/power/control 2>/dev/null || true

RP=/sys/class/remoteproc/remoteproc0
# Idempotent: already have live stack
if [ -d "$RP" ] && [ "$(cat "$RP/state" 2>/dev/null)" = running ] \
   && ip link show wlan0 >/dev/null 2>&1 \
   && pgrep -x tqftpserv >/dev/null && pgrep -x rmtfs >/dev/null; then
  ip link set wlan0 up 2>/dev/null || true
  echo "stack already up — OK"
  return 0
fi
[ -d "$RP" ] || { echo "FATAL: no modem remoteproc"; return 1; }

[ -f /run/rain-depmod.ok ] || { depmod -a 2>/dev/null || true; : >/run/rain-depmod.ok; }

mkdir -p /lib/firmware/qcom/sm6225 /var/lib/tqftpserv \
  /run/modem_partition /run/modem_fw /run/ath10k_fw
chmod 755 /var/lib/tqftpserv

if ! mountpoint -q /run/modem_partition; then
  mount -o ro /dev/disk/by-partlabel/modem_a /run/modem_partition || {
    echo "FATAL: cannot mount modem_a"; return 1; }
fi

# Modem firmware symlinks
for f in /run/modem_partition/image/modem.b* \
         /run/modem_partition/image/modem.mdt; do
  [ -f "$f" ] || continue
  ln -sfn "$f" "/run/modem_fw/$(basename "$f")"
  ln -sfn "$f" "/lib/firmware/qcom/sm6225/$(basename "$f")"
done

# ADSP / CDSP firmware (same partition)
for f in /run/modem_partition/image/adsp.b* \
         /run/modem_partition/image/adsp.mdt \
         /run/modem_partition/image/cdsp.b* \
         /run/modem_partition/image/cdsp.mdt \
         /run/modem_partition/image/qdsp6m.qdb; do
  [ -f "$f" ] || continue
  ln -sfn "$f" "/lib/firmware/qcom/sm6225/$(basename "$f")"
done

cp -an /run/modem_partition/image/*.jsn /lib/firmware/ 2>/dev/null || true
cp -an /run/modem_partition/image/*.jsn /lib/firmware/qcom/sm6225/ 2>/dev/null || true
ln -sfn /run/modem_partition/image/modem_pr /lib/firmware/qcom/sm6225/modem_pr
ln -sfn /run/modem_partition/image/bd3qvdfu.bin /run/ath10k_fw/board.bin
ln -sfn /usr/lib/firmware/ath10k/WCN3990/hw1.0/qcm2290/firmware-5.bin \
  /run/ath10k_fw/firmware-5.bin
ln -sfn /run/modem_partition/image/wlanmdsp.mbn \
  /lib/firmware/qcom/sm6225/wlanmdsp.mbn
mkdir -p /lib/firmware/ath10k/WCN3990/hw1.0
ln -sfn /run/ath10k_fw/board.bin /lib/firmware/ath10k/WCN3990/hw1.0/board.bin
ln -sfn /run/ath10k_fw/firmware-5.bin /lib/firmware/ath10k/WCN3990/hw1.0/firmware-5.bin

: >/var/lib/tqftpserv/lctoem.tmp
: >/var/lib/tqftpserv/mcfg.tmp
chmod 600 /var/lib/tqftpserv/lctoem.tmp /var/lib/tqftpserv/mcfg.tmp

cp -n /lib/firmware/*.jsn /lib/firmware/qcom/sm6225/ 2>/dev/null || true
if [ ! -f /lib/firmware/qcom/sm6225/wlanmdsp.mbn ]; then
  for c in /lib/firmware/wlanmdsp.mbn /lib/firmware/ath10k/WCN3990/hw1.0/wlanmdsp.mbn; do
    [ -f "$c" ] && cp -a "$c" /lib/firmware/qcom/sm6225/wlanmdsp.mbn && break
  done
fi

# Remoteproc support modules
for m in qcom_pdr_msg pdr_interface rmtfs_mem; do
  modprobe "$m" 2>/dev/null || true
done
insmod /lib/modules/$(uname -r)/kernel/drivers/soc/qcom/rmtfs_mem.ko 2>/dev/null || true
[ -e /dev/qcom_rmtfs_mem1 ] || { echo "FATAL: no rmtfs_mem"; return 1; }

# Bind modemst partitions for rmtfs
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

# Unload ath10k_snoc before modem restart (reverse order soft-hangs)
[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"
if lsmod 2>/dev/null | grep -q '^ath10k_snoc'; then
  rmmod ath10k_snoc 2>/dev/null || true
  sleep 0.5
elif [ -L /sys/bus/platform/drivers/ath10k_snoc/c800000.wifi ]; then
  echo c800000.wifi > /sys/bus/platform/drivers/ath10k_snoc/unbind
  sleep 0.5
fi

# Stop modem
st=$(cat "$RP/state")
if [ "$st" != offline ]; then
  echo stop >"$RP/state" 2>/dev/null || true
  for i in $(seq 1 20); do
    [ "$(cat "$RP/state")" = offline ] && break
    sleep 1
  done
  sleep 1
fi

# Start userspace helpers
TQFTP=/usr/local/sbin/tqftpserv
[ -x "$TQFTP" ] || TQFTP=$(command -v tqftpserv)

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

# Start modem
[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"
echo start >"$RP/state" || { echo "FATAL: modem start"; return 1; }
ok=0
for i in $(seq 1 40); do
  st=$(cat "$RP/state")
  case "$st" in
    running) ok=$((ok+1)); [ $ok -ge 5 ] && break ;;
    crashed|offline) echo "FATAL: modem $st"; return 1 ;;
    *) ok=0 ;;
  esac
  sleep 1
done
[ "$(cat "$RP/state")" = running ] || { echo "FATAL: modem not stable"; return 1; }

# Settle before WMI connect (avoids -110)
sleep 8
[ "$(cat "$RP/state")" = running ] || { echo "FATAL: modem dropped in settle"; return 1; }

# Load ath10k_snoc
if [ -d /sys/bus/platform/drivers/ath10k_snoc ] && \
   [ ! -L /sys/bus/platform/drivers/ath10k_snoc/c800000.wifi ]; then
  echo c800000.wifi > /sys/bus/platform/drivers/ath10k_snoc/bind || { echo "FATAL: snoc bind"; return 1; }
else
  lsmod | grep -q '^ath10k_snoc' || modprobe ath10k_snoc || { echo "FATAL: snoc"; return 1; }
fi

# Wait for wlan0
wlan=0
for i in $(seq 1 30); do
  ip link show wlan0 >/dev/null 2>&1 && { wlan=1; break; }
  [ "$(cat "$RP/state")" = running ] || {
    echo "FATAL: modem dropped"; rmmod ath10k_snoc 2>/dev/null
    echo stop >"$RP/state" 2>/dev/null; return 1; }
  sleep 1
done
if [ $wlan -eq 1 ]; then
  ip link set wlan0 up
  echo "OK: wlan0 up"
  echo 4a8c000.serial > /sys/bus/platform/drivers_probe 2>/dev/null || true
else
  echo "WARN: no wlan0"
fi

echo "modem=$(cat "$RP/state")"
echo "=== wifi-start done ==="
return 0
}

main "$@" >"$LOG" 2>&1
rc=$?
cp -a "$LOG" /var/log/qcom-wifi-start.log 2>/dev/null || true
exit "$rc"
