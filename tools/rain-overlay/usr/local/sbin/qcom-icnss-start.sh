#!/bin/bash
# ICNSS + qcacld bring-up for rain/fog (NOT ath10k).
# Same modem/userspace stack as qcom-wifi-start.sh, different WLAN driver.
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/qcom-icnss-start.log
touch "$LOG"
KVER=$(uname -r)
EXTRA=/lib/modules/$KVER/extra
RP=/sys/class/remoteproc/remoteproc0

main() {
echo "=== $(date -Is) icnss-start ==="

if [ -d "$RP" ] && [ "$(cat "$RP/state" 2>/dev/null)" = running ] \
   && ip link show wlan0 >/dev/null 2>&1 \
   && lsmod | grep -q '^wlan '; then
  ip link set wlan0 up 2>/dev/null || true
  echo "stack already up — OK_wlan0"
  echo "=== icnss-start done ==="
  return 0
fi
[ -d "$RP" ] || { echo "no modem remoteproc"; return 1; }
[ -f "$EXTRA/msm_icnss.ko" ] || { echo "FATAL: no $EXTRA/msm_icnss.ko"; return 1; }
[ -f "$EXTRA/wlan.ko" ] || { echo "FATAL: no $EXTRA/wlan.ko"; return 1; }

pkill -x NetworkManager 2>/dev/null || true
# Never load ath10k with icnss (same MMIO)
if lsmod | grep -q '^ath10k'; then
  echo "rmmod ath10k* before icnss"
  rmmod ath10k_snoc 2>/dev/null || true
  rmmod ath10k_core 2>/dev/null || true
  rmmod ath 2>/dev/null || true
  sleep 0.5
fi

mkdir -p /lib/firmware/qcom/sm6225 /var/lib/tqftpserv /lib/firmware/wlan/qca_cld
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
# qcacld cfg / boarddata common paths
for src in /lib/firmware/qcom/sm6225/wlan/WCNSS_qcom_cfg.ini \
           /lib/firmware/vendor-orig/wlan/qca_cld/WCNSS_qcom_cfg.ini; do
  [ -f "$src" ] || continue
  cp -n "$src" /lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini
  cp -n "$src" /lib/firmware/WCNSS_qcom_cfg.ini
  break
done
for src in /lib/firmware/qcom/sm6225/wlan/bd3qvdfu.bin; do
  [ -f "$src" ] || continue
  cp -n "$src" /lib/firmware/wlan/qca_cld/bdwlan.bin
  cp -n "$src" /lib/firmware/bdwlan.bin
  cp -n "$src" /lib/firmware/bd3qvdfu.bin
  break
done
echo "fw check:"
ls -l /lib/firmware/qcom/sm6225/modem.mdt /lib/firmware/qcom/sm6225/wlanmdsp.mbn \
  /lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini \
  /lib/firmware/wlan/qca_cld/bdwlan.bin 2>&1

for m in qcom_pdr_msg pdr_interface rmtfs_mem libarc4 rfkill cfg80211; do
  modprobe "$m" 2>/dev/null || true
done
insmod /lib/modules/$KVER/kernel/drivers/soc/qcom/rmtfs_mem.ko 2>/dev/null || true
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

[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"
st=$(cat "$RP/state")
echo "modem was $st — stop before userspace"
if [ "$st" != offline ]; then
  # Unload WLAN first if present
  rmmod wlan 2>/dev/null || true
  rmmod msm_icnss 2>/dev/null || true
  echo stop >"$RP/state" 2>/dev/null || true
  for i in $(seq 1 20); do
    [ "$(cat "$RP/state")" = offline ] && break
    sleep 1
  done
  sleep 1
fi

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

sleep 2

echo "insmod msm_icnss"
insmod "$EXTRA/msm_icnss.ko" 2>&1 || { echo "FATAL: msm_icnss"; dmesg | tail -40; return 1; }
sleep 2
dmesg | grep -iE 'icnss|cnss' | tail -40

echo "insmod wlan"
insmod "$EXTRA/wlan.ko" 2>&1 || { echo "FATAL: wlan"; dmesg | tail -60; return 1; }

wlan=0
for i in $(seq 1 45); do
  ip link show wlan0 >/dev/null 2>&1 && { wlan=1; break; }
  [ "$(cat "$RP/state")" = running ] || {
    echo "FATAL: modem dropped"; return 1
  }
  echo "wait wlan0 t+$i"
  sleep 1
done
if [ $wlan -eq 1 ]; then
  ip link set wlan0 up
  echo OK_wlan0
else
  echo WARN_no_wlan0
  dmesg | grep -iE 'icnss|wlan|cnss|qcacld|firmware|msa|qmi' | tail -60
fi

lsmod | grep -iE 'icnss|wlan|cfg80211'
ip -br link
echo "modem=$(cat "$RP/state")"
echo "=== icnss-start done ==="
return 0
}

main "$@" 2>&1 | tee -a "$LOG"
exit "${PIPESTATUS[0]}"
