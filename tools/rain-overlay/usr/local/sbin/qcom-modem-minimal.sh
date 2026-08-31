#!/bin/bash
# Absolute minimum modem start — isolate hang vs full wifi-start userspace.
set +e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/sbin
RP=/sys/class/remoteproc/remoteproc0
echo "=== minimal modem $(date -Is) ==="
echo "state=$(cat $RP/state)"
# Ensure rmtfs links exist
mkdir -p /var/lib/rmtfs /var/lib/tqftpserv
for u in /sys/block/*/*/uevent; do
  [ -f "$u" ] || continue
  pn=$(sed -n 's/^PARTNAME=//p' "$u")
  case "$pn" in
    modemst1) tgt=modem_fs1 ;; modemst2) tgt=modem_fs2 ;;
    fsg) tgt=fsg ;; fsc) tgt=fsc ;; *) continue ;;
  esac
  ln -sfn "/dev/$(basename "$(dirname "$u")")" "/var/lib/rmtfs/$tgt"
done
modprobe rmtfs_mem 2>/dev/null || true
insmod /lib/modules/$(uname -r)/kernel/drivers/soc/qcom/rmtfs_mem.ko 2>/dev/null || true
pgrep -x qrtr-ns >/dev/null || qrtr-ns -f 1 &
sleep 1
pgrep -x pd-mapper >/dev/null || pd-mapper &
sleep 1
pgrep -x rmtfs >/dev/null || rmtfs -s -o /var/lib/rmtfs &
sleep 1
pgrep -x tqftpserv >/dev/null || tqftpserv &
sleep 1
echo "procs: qrtr=$(pgrep -x qrtr-ns) pd=$(pgrep -x pd-mapper) rmtfs=$(pgrep -x rmtfs) tqftp=$(pgrep -x tqftpserv)"
[ -e $RP/recovery ] && echo disabled >$RP/recovery
echo start >$RP/state
for i in $(seq 1 30); do
  st=$(cat $RP/state)
  echo "t+$i $st"
  [ "$st" = running ] && { echo OK_modem; exit 0; }
  [ "$st" = crashed ] && { echo FATAL_crash; dmesg | tail -30; exit 1; }
  sleep 1
done
echo FAIL_timeout; dmesg | tail -40; exit 1
