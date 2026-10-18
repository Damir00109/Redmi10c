#!/bin/bash
# Stage the userspace bits the modem/WLAN stack needs at boot.
#
# The firmware itself is baked into the rootfs by build-rootfs.sh
# (initramfs/lib/firmware -> /lib/firmware), so nothing has to be copied
# or symlinked from the modem_a partition any more — verified 2026-10-18:
# the modem, WLAN DSP and ath10k all come up with this reduced script.
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# --- tqftpserv work dir ---
# mcfg.tmp / lctoem.tmp: the modem asks TFTP for its carrier config (MBN).
# Do NOT serve a raw mcfg_sw.mbn here: an unactivated MBN injected this way
# leaves NAS in 'not-registered-searching' (scan fails, no service). With an
# empty placeholder the modem falls back to its built-in / PDC-stored
# configuration and picks the carrier automatically from the SIM — that is
# what gives us provider switching without touching NV data.
# (Verified 2026-10-11: empty -> Tele2 RU LTE registered; tele2 mbn -> no service.)
mkdir -p /var/lib/tqftpserv
chmod 755 /var/lib/tqftpserv
: >/var/lib/tqftpserv/mcfg.tmp
: >/var/lib/tqftpserv/lctoem.tmp
chmod 600 /var/lib/tqftpserv/mcfg.tmp /var/lib/tqftpserv/lctoem.tmp

# tqftpserv must also answer absolute /readonly/vendor/firmware_mnt/image/
# requests for the WLAN DSP firmware.
mkdir -p /var/lib/tqftpserv/readonly/vendor/firmware_mnt/image
if [ -f /lib/firmware/qcom/sm6225/wlanmdsp.mbn ]; then
  ln -sfn /lib/firmware/qcom/sm6225/wlanmdsp.mbn \
    /var/lib/tqftpserv/readonly/vendor/firmware_mnt/image/wlanmdsp.mbn
fi

# --- rmtfs storage ---
# rmtfs runs with "-o /var/lib/rmtfs" and expects the modem partitions
# there.  Discover them by PARTNAME: the /dev/sdX names are not guaranteed
# stable across kernel/DT changes.
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

echo "firmware staging complete"
exit 0
