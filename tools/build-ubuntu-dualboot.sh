#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Build Ubuntu-on-cust dual-boot package for Redmi 10C (rain/fog):
#   - out/ubuntu-dualboot/boot-linux.img     (mainline kernel → mount cust → switch_root)
#   - out/ubuntu-dualboot/linux_rootfs.img   (ext4, flashed to cust)
#   - out/ubuntu-dualboot/twrp-ubuntu-rain.zip  (adb sideload in TWRP)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out/ubuntu-dualboot"
RF="$OUT/rootfs"
KERN="$ROOT/out/linux-7.1.5"
FW_NOTES="$ROOT/notes/vendor-firmware-20260731/extract"
BT_FW_NOTES="$ROOT/notes/vendor-bt-firmware/image"
IR_SRC="$ROOT/out/initramfs-root"
LOG="$OUT/logs"
IMG_SIZE_MB="${IMG_SIZE_MB:-1800}"   # cust is 2GiB; leave slack for fs

mkdir -p "$OUT" "$LOG" "$OUT/twrp/META-INF/com/google/android" "$OUT/twrp/images"

echo "=== [1/5] debootstrap second stage + packages ==="
if [ ! -f "$RF/etc/os-release" ]; then
  echo "Run debootstrap foreign first (see build log)." >&2
  exit 1
fi

sudo cp /usr/bin/qemu-aarch64 "$RF/usr/bin/qemu-aarch64-static" 2>/dev/null \
  || sudo cp /usr/bin/qemu-aarch64 "$RF/usr/bin/qemu-aarch64-static"

if [ ! -f "$RF/debootstrap/debootstrap" ] && [ ! -d "$RF/var/lib/dpkg" ]; then
  echo "unexpected rootfs state" >&2
  exit 1
fi

if [ -d "$RF/debootstrap" ]; then
  sudo chroot "$RF" /debootstrap/debootstrap --second-stage \
    2>&1 | tee "$LOG/debootstrap-second.log"
fi

# apt sources
sudo tee "$RF/etc/apt/sources.list" >/dev/null <<'EOF'
deb http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse
EOF

sudo chroot "$RF" bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
export LANG=C.UTF-8 LC_ALL=C.UTF-8
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
apt-get update
apt-get install -y --no-install-recommends \
  systemd systemd-sysv udev kmod sudo openssh-server \
  network-manager wpasupplicant iw rfkill \
  iproute2 iputils-ping ca-certificates curl wget \
  mc nano less htop tmux bash-completion \
  usbutils pciutils fdisk parted e2fsprogs \
  qrtr-tools rmtfs tqftpserv protection-domain-mapper \
  libqrtr1 \
  net-tools wireless-regdb locales
locale-gen en_US.UTF-8 || true
apt-get clean
rm -rf /var/lib/apt/lists/*
'

echo "=== [2/5] rain bringup: firmware, modules, units, users ==="
# kernel modules
MODVER=$(ls "$IR_SRC/lib/modules" 2>/dev/null | head -1 || echo 7.1.5-dirty)
sudo mkdir -p "$RF/lib/modules"
if [ -d "$IR_SRC/lib/modules/$MODVER" ]; then
  sudo rsync -a --delete "$IR_SRC/lib/modules/$MODVER/" "$RF/lib/modules/$MODVER/"
fi

# firmware: modem + ath10k + PD maps + vendor dump highlights
sudo mkdir -p "$RF/lib/firmware/qcom/sm6225" "$RF/lib/firmware/ath10k"
sudo rsync -a "$IR_SRC/lib/firmware/" "$RF/lib/firmware/" || true
if [ -d "$FW_NOTES/firmware_mnt/image" ]; then
  sudo mkdir -p "$RF/lib/firmware/qcom/sm6225/android-image"
  # BDFs + jsn (small); skip huge modem if already staged
  sudo cp -a "$FW_NOTES/firmware_mnt/image/"*.jsn "$RF/lib/firmware/" 2>/dev/null || true
  sudo cp -a "$FW_NOTES/firmware_mnt/image/bd3qvdfu.bin" "$RF/lib/firmware/qcom/sm6225/wlan/" 2>/dev/null || true
fi
if [ -d "$FW_NOTES/firmware" ]; then
  sudo mkdir -p "$RF/lib/firmware/vendor-orig"
  sudo rsync -a "$FW_NOTES/firmware/" "$RF/lib/firmware/vendor-orig/"
fi
if [ -f "$BT_FW_NOTES/crnv21.bin" ] && [ -f "$BT_FW_NOTES/crbtfw21.tlv" ]; then
  sudo mkdir -p "$RF/lib/firmware/qca/Xiaomi/rain"
  sudo cp -a "$BT_FW_NOTES/crnv21.bin" "$BT_FW_NOTES/crbtfw21.tlv" \
    "$RF/lib/firmware/qca/Xiaomi/rain/"
fi

# hostname / users
echo 'rain-ubuntu' | sudo tee "$RF/etc/hostname" >/dev/null
sudo tee "$RF/etc/hosts" >/dev/null <<'EOF'
127.0.0.1 localhost
10.0.0.2 rain-ubuntu
::1     localhost ip6-localhost ip6-loopback
EOF

# user: rain / rain (please change)
sudo chroot "$RF" bash -c '
set -e
groupadd -f netdev
groupadd -f video
groupadd -f plugdev
id rain >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo,netdev,video,plugdev rain
echo "rain:rain" | chpasswd
echo "root:rain" | chpasswd
'

# fstab — root is cust
sudo tee "$RF/etc/fstab" >/dev/null <<'EOF'
# <file system> <mount point> <type> <options> <dump> <pass>
/dev/root  /     ext4  noatime,errors=remount-ro  0 1
tmpfs      /tmp  tmpfs defaults,nosuid,nodev       0 0
EOF

# QMI / WiFi bringup unit
sudo tee "$RF/etc/systemd/system/qcom-wifi-bringup.service" >/dev/null <<'EOF'
[Unit]
Description=Qualcomm WiFi/modem userspace (qrtr/rmtfs/tqftpserv/pd-mapper)
After=systemd-modules-load.service sys-kernel-debug.mount
Wants=network.target
Before=NetworkManager.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/qcom-wifi-bringup.sh

[Install]
WantedBy=multi-user.target
EOF

sudo tee "$RF/usr/local/sbin/qcom-wifi-bringup.sh" >/dev/null <<'EOF'
#!/bin/bash
set -e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
modprobe rmtfs_mem 2>/dev/null || true
# modem PAS is usually built-in; if modular:
for m in qmi_helpers qcom_pdr_msg pdr_interface qrtr qrtr-smd mdt_loader \
         qcom_common qcom_q6v5 qcom_sysmon qcom_q6v5_pas rmtfs_mem \
         libarc4 rfkill cfg80211 mac80211 ath ath10k_core ath10k_snoc; do
  modprobe "$m" 2>/dev/null || true
done
mkdir -p /var/lib/rmtfs
# bind modemst partitions for rmtfs
for u in /sys/block/*/*/uevent; do
  [ -f "$u" ] || continue
  pn=$(sed -n 's/^PARTNAME=//p' "$u")
  case "$pn" in
    modemst1) tgt=modem_fs1 ;;
    modemst2) tgt=modem_fs2 ;;
    fsg) tgt=fsg ;;
    fsc) tgt=fsc ;;
    *) continue ;;
  esac
  dev=/dev/$(basename "$(dirname "$u")")
  ln -sfn "$dev" "/var/lib/rmtfs/$tgt"
done
command -v qrtr-ns >/dev/null && qrtr-ns -f 1 &
sleep 1
command -v pd-mapper >/dev/null && pd-mapper &
command -v rmtfs >/dev/null && rmtfs -s -o /var/lib/rmtfs &
command -v tqftpserv >/dev/null && tqftpserv &
sleep 2
if [ -e /sys/class/remoteproc/remoteproc0/state ]; then
  st=$(cat /sys/class/remoteproc/remoteproc0/state)
  if [ "$st" = offline ]; then
    echo start >/sys/class/remoteproc/remoteproc0/state || true
  fi
fi
exit 0
EOF
sudo chmod 755 "$RF/usr/local/sbin/qcom-wifi-bringup.sh"
sudo chroot "$RF" systemctl enable qcom-wifi-bringup.service NetworkManager ssh || true

# getty on ttyGS0 (USB ACM) + tty0
sudo mkdir -p "$RF/etc/systemd/system/getty.target.wants"
sudo chroot "$RF" bash -c '
systemctl enable getty@tty0.service || true
mkdir -p /etc/systemd/system/getty@ttyGS0.service.d
cat >/etc/systemd/system/getty@ttyGS0.service.d/override.conf <<E
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o "-p -- \\\\u" --keep-baud 115200,57600,38400,9600 - \$TERM
E
ln -sf /lib/systemd/system/getty@.service /etc/systemd/system/getty.target.wants/getty@ttyGS0.service || true
'

echo "=== [3/5] make ext4 rootfs image (${IMG_SIZE_MB}M) ==="
ROOTIMG="$OUT/linux_rootfs.img"
sudo rm -f "$ROOTIMG"
sudo dd if=/dev/zero of="$ROOTIMG" bs=1M count="$IMG_SIZE_MB" status=progress
sudo mkfs.ext4 -L rain-ubuntu -O ^metadata_csum_seed "$ROOTIMG"
MNT="$OUT/mnt-root"
mkdir -p "$MNT"
sudo mount -o loop "$ROOTIMG" "$MNT"
sudo rsync -aHAX --numeric-ids "$RF"/ "$MNT"/
sudo umount "$MNT"
# sparse for faster sideload where supported
img2simg "$ROOTIMG" "$OUT/linux_rootfs.sparse.img" 2>/dev/null \
  || cp "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"
ls -lh "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"

echo "=== [4/5] boot-linux.img (pivot to cust) ==="
# built by companion step / inline below if script continued
echo "boot image: run tools/pack-ubuntu-boot.sh"

echo "=== [5/5] TWRP zip skeleton (filled after boot pack) ==="
echo "OK rootfs ready at $OUT"
