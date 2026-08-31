#!/bin/bash
# Stage Qualcomm modem + WiFi firmware early in boot.
# Mounts modem_a partition, creates symlinks for ath10k/tqftpserv, and
# sets up rmtfs partition links.  Runs as a oneshot systemd unit on
# sysinit.target so the modem can be started as soon as the userspace
# daemons are ready (target: ~10s after boot instead of ~43s).
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# --- Mount modem_a partition (read-only) ---
# The systemd unit has a Requires= on the partition device unit, so it
# is guaranteed to exist by the time we run.
MODEM_DEV=/dev/disk/by-partlabel/modem_a
mkdir -p /run/modem_partition
if ! mountpoint -q /run/modem_partition; then
  mount -o ro "$MODEM_DEV" /run/modem_partition || {
    echo "FATAL: cannot mount modem_a" >&2
    exit 1
  }
fi

# --- Symlink modem firmware segments into /run/modem_fw ---
mkdir -p /run/modem_fw /run/ath10k_fw
for f in /run/modem_partition/image/modem.b* \
         /run/modem_partition/image/modem.mdt \
         /run/modem_partition/image/wlanmdsp.mbn; do
  [ -f "$f" ] || continue
  ln -sfn "$f" "/run/modem_fw/$(basename "$f")"
done

# --- ath10k board data + firmware-5.bin ---
ln -sfn /run/modem_partition/image/bd3qvdfu.bin /run/ath10k_fw/board.bin
ln -sfn /usr/lib/firmware/ath10k/WCN3990/hw1.0/qcm2290/firmware-5.bin \
  /run/ath10k_fw/firmware-5.bin

# --- ath10k firmware lookup path ---
mkdir -p /lib/firmware/ath10k/WCN3990/hw1.0
ln -sfn /run/ath10k_fw/board.bin \
  /lib/firmware/ath10k/WCN3990/hw1.0/board.bin
ln -sfn /run/ath10k_fw/firmware-5.bin \
  /lib/firmware/ath10k/WCN3990/hw1.0/firmware-5.bin

# --- tqftpserv work dir + placeholder files ---
mkdir -p /var/lib/tqftpserv
chmod 755 /var/lib/tqftpserv
: >/var/lib/tqftpserv/lctoem.tmp
: >/var/lib/tqftpserv/mcfg.tmp
chmod 600 /var/lib/tqftpserv/lctoem.tmp /var/lib/tqftpserv/mcfg.tmp

# --- wlanmdsp.mbn into /lib/firmware/qcom/sm6225/ (for tqftpserv) ---
if [ ! -f /lib/firmware/qcom/sm6225/wlanmdsp.mbn ]; then
  for c in /lib/firmware/wlanmdsp.mbn \
           /lib/firmware/ath10k/WCN3990/hw1.0/wlanmdsp.mbn \
           /run/modem_partition/image/wlanmdsp.mbn; do
    [ -f "$c" ] && cp -a "$c" /lib/firmware/qcom/sm6225/wlanmdsp.mbn && break
  done
fi

# --- Copy .jsn files if present ---
cp -n /lib/firmware/*.jsn /lib/firmware/qcom/sm6225/ 2>/dev/null || true

# --- rmtfs partition links ---
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

# --- Load rmtfs_mem kernel module if not already loaded ---
if [ ! -e /dev/qcom_rmtfs_mem1 ]; then
  for m in qcom_pdr_msg pdr_interface rmtfs_mem; do
    modprobe "$m" 2>/dev/null || true
  done
  insmod /lib/modules/$(uname -r)/kernel/drivers/soc/qcom/rmtfs_mem.ko 2>/dev/null || true
fi

echo "firmware staging complete"
exit 0
