#!/usr/bin/env bash
# Build Ubuntu 26.04 (resolute) rootfs for Redmi 10C rain dual-boot (cust).
# Preserves working rain Wi-Fi/serial stack from the 24.04 tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/out/ubuntu-dualboot"
CACHE="$OUT/cache"
OLD_RF="$OUT/rootfs"
RF="$OUT/rootfs-26.04"
LOG="$OUT/logs"
IMG_SIZE_MB="${IMG_SIZE_MB:-1800}"
TAR="$CACHE/ubuntu-26.04-server-cloudimg-arm64-root.tar.xz"
FW_NOTES="$ROOT/notes/vendor-firmware-20260731/extract"
IR_SRC="$ROOT/out/initramfs-root"

mkdir -p "$CACHE" "$LOG" "$OUT"

if [ ! -f "$TAR" ]; then
  echo "Missing $TAR — download first" >&2
  exit 1
fi

echo "=== [1/6] extract 26.04 cloud rootfs ==="
if [ -d "$RF" ]; then
  # avoid wiping a mounted tree
  if mountpoint -q "$RF" 2>/dev/null; then
    echo "ERROR: $RF is mounted" >&2
    exit 1
  fi
  sudo rm -rf "$RF"
fi
mkdir -p "$RF"
sudo tar -C "$RF" -xJf "$TAR"
# cloud image expects cloud-init; we use local users instead
sudo rm -f "$RF/etc/cloud/cloud.cfg.d/"* 2>/dev/null || true

echo "=== [2/6] qemu-user + apt packages ==="
QEMU=$(command -v qemu-aarch64-static || command -v qemu-aarch64)
sudo cp "$QEMU" "$RF/usr/bin/qemu-aarch64-static"
sudo tee "$RF/etc/apt/sources.list" >/dev/null <<'EOF'
deb http://ports.ubuntu.com/ubuntu-ports resolute main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports resolute-updates main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports resolute-security main restricted universe multiverse
EOF
# drop cloud image ubuntu.sources if present (noble→resolute conflict)
sudo rm -f "$RF/etc/apt/sources.list.d/"*.sources 2>/dev/null || true
# cloud image resolv.conf is a dangling systemd stub — replace for apt in chroot
sudo rm -f "$RF/etc/resolv.conf"
sudo tee "$RF/etc/resolv.conf" >/dev/null <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

sudo chroot "$RF" bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true
apt-get update
apt-get install -y --no-install-recommends \
  systemd systemd-sysv udev kmod sudo \
  wpasupplicant iw rfkill \
  iproute2 iputils-ping ca-certificates curl wget \
  nano less htop tmux bash-completion \
  usbutils fdisk parted e2fsprogs \
  qrtr-tools rmtfs tqftpserv protection-domain-mapper \
  libqrtr1 net-tools wireless-regdb locales \
  network-manager dbus
# keep NM installed but we mask it (ath10k reboot on this SoC)
locale-gen en_US.UTF-8 || true
apt-get clean
rm -rf /var/lib/apt/lists/*
' 2>&1 | tee "$LOG/apt-26.04.log"

echo "=== [3/6] users / hostname / fstab ==="
echo 'rain-ubuntu' | sudo tee "$RF/etc/hostname" >/dev/null
sudo tee "$RF/etc/hosts" >/dev/null <<'EOF'
127.0.0.1 localhost
10.0.0.2 rain-ubuntu
::1     localhost ip6-localhost ip6-loopback
EOF
sudo tee "$RF/etc/fstab" >/dev/null <<'EOF'
/dev/root  /     ext4  noatime,errors=remount-ro  0 1
tmpfs      /tmp  tmpfs defaults,nosuid,nodev       0 0
EOF
sudo chroot "$RF" bash -c '
set -e
groupadd -f netdev; groupadd -f video; groupadd -f plugdev
id rain >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo,netdev,video,plugdev rain
echo "rain:rain" | chpasswd
echo "root:rain" | chpasswd
'

echo "=== [4/6] rain overlay: modules, firmware, scripts, units ==="
# kernel modules
MODVER=$(ls "$IR_SRC/lib/modules" 2>/dev/null | head -1 || true)
if [ -z "$MODVER" ] && [ -d "$OLD_RF/lib/modules" ]; then
  MODVER=$(ls "$OLD_RF/lib/modules" | head -1)
fi
if [ -n "$MODVER" ]; then
  sudo mkdir -p "$RF/lib/modules"
  if [ -d "$IR_SRC/lib/modules/$MODVER" ]; then
    sudo rsync -a --delete "$IR_SRC/lib/modules/$MODVER/" "$RF/lib/modules/$MODVER/"
  elif [ -d "$OLD_RF/lib/modules/$MODVER" ]; then
    sudo rsync -a --delete "$OLD_RF/lib/modules/$MODVER/" "$RF/lib/modules/$MODVER/"
  fi
fi

# firmware from old working tree + vendor notes
sudo mkdir -p "$RF/lib/firmware/qcom/sm6225" "$RF/var/lib/tqftpserv"
if [ -d "$OLD_RF/lib/firmware" ]; then
  sudo rsync -a "$OLD_RF/lib/firmware/" "$RF/lib/firmware/"
fi
if [ -d "$FW_NOTES/firmware_mnt/image/modem_pr" ]; then
  sudo rsync -a --delete "$FW_NOTES/firmware_mnt/image/modem_pr/" \
    "$RF/lib/firmware/qcom/sm6225/modem_pr/"
fi
sudo chmod 755 "$RF/var/lib/tqftpserv"

# binaries from old tree
sudo mkdir -p "$RF/usr/local/sbin" "$RF/usr/local/bin"
for b in tqftpserv busybox; do
  if [ -x "$OLD_RF/usr/local/sbin/$b" ]; then
    sudo cp -a "$OLD_RF/usr/local/sbin/$b" "$RF/usr/local/sbin/"
  elif [ -x "$OLD_RF/usr/local/bin/$b" ]; then
    sudo cp -a "$OLD_RF/usr/local/bin/$b" "$RF/usr/local/bin/"
  fi
done
# busybox path we use
if [ -x "$OLD_RF/usr/local/bin/busybox" ]; then
  sudo cp -a "$OLD_RF/usr/local/bin/busybox" "$RF/usr/local/bin/"
fi

# wifi / serial / stable-boot scripts (canonical copies under tools/rain-overlay)
OVER="$ROOT/tools/rain-overlay"
if [ -d "$OVER" ]; then
  sudo rsync -a "$OVER/" "$RF/"
else
  # fallback: copy known files from old rootfs
  for f in \
    usr/local/sbin/qcom-wifi-start.sh \
    usr/local/sbin/qcom-wifi-connect.sh \
    usr/local/sbin/qcom-wifi-bringup.sh \
    usr/local/sbin/udhcpc-wlan.script \
    usr/local/sbin/usb-acm-gadget.sh \
    usr/local/sbin/usb-adb-gadget.sh \
    usr/local/sbin/rain-enable-adb-gadget.sh \
    usr/local/sbin/install-adbd-debs.sh \
    usr/local/sbin/rain-stable-boot.sh \
    etc/systemd/system/usb-acm-gadget.service \
    etc/systemd/system/usb-adb-gadget.service \
    etc/systemd/system/adbd.service \
    etc/systemd/system/rain-serial-console.service \
    etc/systemd/system/rain-stable-boot.service \
    etc/systemd/system/qcom-wifi-bringup.service \
    etc/modprobe.d/rain-stable.conf
  do
    if [ -e "$OLD_RF/$f" ] || [ -e "$ROOT/tools/rain-overlay/$f" ]; then
      sudo mkdir -p "$RF/$(dirname "$f")"
      if [ -e "$ROOT/tools/rain-overlay/$f" ]; then
        sudo cp -a "$ROOT/tools/rain-overlay/$f" "$RF/$f"
      else
        sudo cp -a "$OLD_RF/$f" "$RF/$f"
      fi
    fi
  done
fi

# Ensure connect/start scripts are executable and NM-safe messaging
sudo chmod 755 "$RF/usr/local/sbin/"*.sh "$RF/usr/local/sbin/udhcpc-wlan.script" 2>/dev/null || true
sudo chmod 755 "$RF/usr/local/sbin/tqftpserv" "$RF/usr/local/bin/busybox" 2>/dev/null || true

# Stage adbd debs for offline install
if [ -d "$ROOT/tools/rain-overlay/pkg/adbd" ]; then
  sudo mkdir -p "$RF/usr/local/share/rain-adbd-debs"
  sudo cp -a "$ROOT/tools/rain-overlay/pkg/adbd/"*.deb "$RF/usr/local/share/rain-adbd-debs/" 2>/dev/null || true
fi

# Mask MPSS/WiFi/cloud/snap boot units (auto-start hangs this SoC)
for u in NetworkManager NetworkManager-wait-online NetworkManager-dispatcher \
         ModemManager wpa_supplicant \
         tqftpserv rmtfs pd-mapper qrtr-ns \
         cloud-init-local cloud-init-main cloud-init-network cloud-config cloud-final \
         snapd snapd.seeded snapd.autoimport snapd.apparmor snapd.core-fixup \
         open-vm-tools pollinate systemd-networkd systemd-networkd-wait-online; do
  sudo ln -sfn /dev/null "$RF/etc/systemd/system/${u}.service"
done
sudo mkdir -p "$RF/etc/NetworkManager/conf.d" "$RF/etc/cloud/cloud.cfg.d"
sudo tee "$RF/etc/NetworkManager/conf.d/default-wifi-powersave-on.conf" >/dev/null <<'EOF'
[connection]
wifi.powersave=2
EOF
sudo tee "$RF/etc/cloud/cloud.cfg.d/99-disable-cloud-init.cfg" >/dev/null <<'EOF'
datasource_list: [ None ]
EOF
sudo touch "$RF/etc/cloud/cloud-init.disabled"
sudo rm -f "$RF/etc/systemd/system/multi-user.target.wants/"{tqftpserv,rmtfs,pd-mapper,qrtr-ns,NetworkManager,wpa_supplicant}.service 2>/dev/null || true

# enable rain units — ADB gadget (not ACM)
sudo chroot "$RF" bash -c '
systemctl enable rain-stable-boot.service usb-adb-gadget.service adbd.service qcom-wifi-bringup.service getty@tty1.service 2>/dev/null || true
systemctl disable usb-acm-gadget.service rain-serial-console.service 2>/dev/null || true
systemctl disable NetworkManager ssh tqftpserv rmtfs pd-mapper 2>/dev/null || true
' || true

# issue / motd hint
sudo tee "$RF/etc/issue" >/dev/null <<'EOF'
Ubuntu 26.04 LTS rain-ubuntu \n \l

WiFi: sudo qcom-wifi-start.sh && sudo qcom-wifi-connect.sh
EOF

echo "=== [5/6] pack ext4 ${IMG_SIZE_MB}M ==="
ROOTIMG="$OUT/linux_rootfs.img"
sudo umount "$OUT/mnt-root" 2>/dev/null || true
sudo rm -f "$ROOTIMG"
sudo dd if=/dev/zero of="$ROOTIMG" bs=1M count="$IMG_SIZE_MB" status=progress
sudo mkfs.ext4 -L rain-ubuntu -O ^metadata_csum_seed "$ROOTIMG"
MNT="$OUT/mnt-root"
mkdir -p "$MNT"
sudo mount -o loop "$ROOTIMG" "$MNT"
# exclude host junk if any
sudo rsync -aHAX --numeric-ids \
  --exclude=dev/fd --exclude=dev/pts --exclude=dev/shm \
  --exclude=proc --exclude=sys --exclude=run \
  "$RF"/ "$MNT"/
sudo mkdir -p "$MNT/dev" "$MNT/proc" "$MNT/sys" "$MNT/run" "$MNT/tmp"
sudo umount "$MNT"
img2simg "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"
ls -lh "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"

# Point active rootfs symlink/dir for other tools
if [ -d "$OLD_RF" ] && [ ! -L "$OLD_RF" ]; then
  echo "=== [6/6] archive old 24.04 rootfs → rootfs-24.04.bak ==="
  if [ ! -d "$OUT/rootfs-24.04.bak" ]; then
    sudo mv "$OLD_RF" "$OUT/rootfs-24.04.bak"
  fi
fi
sudo rm -rf "$OUT/rootfs"
sudo ln -sfn rootfs-26.04 "$OUT/rootfs"

echo "OK Ubuntu 26.04 rootfs ready"
grep PRETTY_NAME "$RF/etc/os-release" || true
echo "Flash: fastboot flash cust $OUT/linux_rootfs.sparse.img && fastboot continue"
