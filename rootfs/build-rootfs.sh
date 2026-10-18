#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
#
# Build Ubuntu 26.04.1 LTS (resolute) Server rootfs for Redmi 10C (fog/rain,
# SM6225) — flashed to the `cust` partition, booted via pivot-init.
#
# Standalone version: lives outside the Redmi10c repo. Optional hooks pull
# kernel modules and proprietary firmware from a local Redmi10c checkout
# (set REDMI=/path/to/Redmi10c to override).
#
# Flow: extract cloudimg rootfs -> chroot apt (qemu-aarch64 binfmt) ->
# users/hostname/fstab -> rain-overlay -> mask units -> pack ext4 -> sparse.
#
# NOTE: base init is systemd (as in the working 26.04 build). The device
# currently runs OpenRC — that migration overlay is NOT in git; recover it
# from the device cust partition if you need it (see README.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
CACHE="$ROOT/cache"
RF="$OUT/rootfs-26.04"
LOG="$ROOT/logs"
IMG_SIZE_MB="${IMG_SIZE_MB:-1800}"   # cust is 2 GiB; leave slack for fs
REDMI="${REDMI:-/mnt/128/Redmi10c}"

TAR="$CACHE/ubuntu-26.04-server-cloudimg-arm64-root.tar.xz"

mkdir -p "$OUT" "$LOG"

if [ ! -f "$TAR" ]; then
  echo "Missing $TAR" >&2
  echo "Download: curl -L -o $TAR \\" >&2
  echo "  https://cloud-images.ubuntu.com/releases/resolute/release/ubuntu-26.04-server-cloudimg-arm64-root.tar.xz" >&2
  exit 1
fi

echo "=== [1/6] extract 26.04 cloud rootfs ==="
if [ -d "$RF" ]; then
  if mountpoint -q "$RF" 2>/dev/null || mount | grep -q " $RF"; then
    echo "ERROR: $RF is mounted" >&2
    exit 1
  fi
  sudo rm -rf "$RF"
fi
mkdir -p "$RF"
sudo tar -C "$RF" -xJf "$TAR"
# cloud image expects cloud-init; we use local users instead
sudo rm -f "$RF/etc/cloud/cloud.cfg.d/"* 2>/dev/null || true

echo "=== [2/6] apt packages in chroot (binfmt qemu-aarch64, F flag) ==="
# binfmt_misc 'F' flag keeps the interpreter fd open -> no qemu copy needed
# inside the rootfs. If registration ever loses F, install qemu-user-static.
sudo tee "$RF/etc/apt/sources.list" >/dev/null <<'EOF'
deb http://ports.ubuntu.com/ubuntu-ports resolute main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports resolute-updates main restricted universe multiverse
deb http://ports.ubuntu.com/ubuntu-ports resolute-security main restricted universe multiverse
EOF
sudo rm -f "$RF/etc/apt/sources.list.d/"*.sources 2>/dev/null || true
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
  qrtr-tools rmtfs tqftpserv \
  libqrtr1 net-tools wireless-regdb locales qbootctl \
  network-manager dbus \
  bluez pulseaudio pulseaudio-module-bluetooth pulseaudio-utils \
  alsa-utils dbus-x11 python3-dbus adbd \
  gnss-share \
  gcc libqmi-glib-dev pkg-config \
  v4l-utils \
  fastboot upower fake-hwclock
locale-gen en_US.UTF-8 || true
apt-get clean
rm -rf /var/lib/apt/lists/*
' 2>&1 | tee "$LOG/apt-26.04.log"

# rain-loc-tool: applies the Qualcomm A-GPS config (SUPL server, operation
# mode, UTC time, XTRA orbits) that qmicli does not expose; built here because
# it needs libqmi headers, which are removed again afterwards to keep the
# image small.
if [ -f "$ROOT/tools/rain-loc-tool.c" ]; then
  sudo cp "$ROOT/tools/rain-loc-tool.c" "$RF/tmp/rain-loc-tool.c"
  sudo chroot "$RF" bash -c '
    set -e
    export DEBIAN_FRONTEND=noninteractive
    mkdir -p /usr/local/bin
    gcc -O2 -o /usr/local/bin/rain-loc-tool /tmp/rain-loc-tool.c $(pkg-config --cflags --libs qmi-glib)
    rm -f /tmp/rain-loc-tool.c
    apt-get purge -y gcc libqmi-glib-dev pkg-config >/dev/null 2>&1 || true
    apt-get autoremove -y >/dev/null 2>&1 || true
    ls -la /usr/local/bin/rain-loc-tool
  ' 2>&1 | tail -2
else
  echo "WARN: no $ROOT/tools/rain-loc-tool.c — A-GPS config will not be applied" >&2
fi

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
groupadd -f netdev; groupadd -f video; groupadd -f plugdev; groupadd -f audio
id rain >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo,netdev,video,plugdev,audio rain
# unprivileged user hexagonrpcd runs as (Snapdragon Sensor Core)
groupadd -r -f fastrpc
id fastrpc >/dev/null 2>&1 || useradd -r -g fastrpc -d /var/lib/fastrpc -s /usr/sbin/nologin fastrpc
# gnss-share chowns its NMEA socket to this group (else it exits 1)
groupadd -r -f geoclue
echo "rain:rain" | chpasswd
echo "root:rain" | chpasswd
mkdir -p /var/lib/systemd/linger
touch /var/lib/systemd/linger/rain
# sshd needs host keys; they are not generated inside a chroot by the package
ssh-keygen -A >/dev/null 2>&1 || true
'

# machine-id: cloudimg ships an EMPTY /etc/machine-id — dbus refuses to
# start without a valid ID (kills logind → dead power button etc.).
MID="$(cat /proc/sys/kernel/random/uuid | tr -d -)"
echo "$MID" | sudo tee "$RF/etc/machine-id" >/dev/null
sudo chmod 644 "$RF/etc/machine-id"
sudo mkdir -p "$RF/var/lib/dbus"
echo "$MID" | sudo tee "$RF/var/lib/dbus/machine-id" >/dev/null
sudo chmod 644 "$RF/var/lib/dbus/machine-id"

echo "=== [4/6] modules + firmware + rain-overlay ==="
# Kernel modules from the Redmi10c build tree (make modules_install staging).
if [ -n "$REDMI" ] && [ -d "$REDMI/out/build/linux-7.1.5" ]; then
  MODVER=$(ls "$REDMI/out/build/linux-7.1.5/lib/modules" 2>/dev/null | head -1 || true)
  if [ -z "$MODVER" ]; then
    echo ">> no staged modules; run in $REDMI:"
    echo "   make -C out/src/linux O=out/build/linux-7.1.5 ARCH=arm64 \\"
    echo "     CROSS_COMPILE=aarch64-linux-gnu- modules -j$(nproc)"
    echo "   make -C out/src/linux O=out/build/linux-7.1.5 ARCH=arm64 \\"
    echo "     CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH=out/build/linux-7.1.5 modules_install"
  else
    sudo mkdir -p "$RF/lib/modules"
    sudo rsync -a --delete "$REDMI/out/build/linux-7.1.5/lib/modules/$MODVER/" \
      "$RF/lib/modules/$MODVER/"
    echo ">> modules $MODVER staged"
  fi
fi

# Proprietary firmware fetched by $REDMI/tools/fetch-firmware.sh
sudo mkdir -p "$RF/lib/firmware" "$RF/var/lib/tqftpserv"
if [ -n "$REDMI" ] && [ -d "$REDMI/initramfs/lib/firmware" ]; then
  sudo rsync -a "$REDMI/initramfs/lib/firmware/" "$RF/lib/firmware/"
  echo ">> firmware staged from $REDMI/initramfs/lib/firmware"
fi
# hexagonrpcd vendor tree (DSP files served to the sensor/adsp cores)
if [ -n "$REDMI" ] && [ -d "$REDMI/vendor-qcom" ]; then
  sudo mkdir -p "$RF/usr/share/vendor-qcom"
  sudo rsync -a "$REDMI/vendor-qcom/" "$RF/usr/share/vendor-qcom/"
  echo ">> vendor-qcom tree staged"
fi
# Snapdragon Sensor Core userspace: hexagonrpcd/ssccli (musl builds from the
# postmarketOS image, fetched by tools/fetch-firmware.sh into $REDMI/sensors)
# plus the -R tree the sensor PD is served from (/usr/share/qcom, the path
# hexagonrpcd-adsp-sensorspd.service passes on the command line).
if [ -n "$REDMI" ] && [ -d "$REDMI/sensors" ]; then
  sudo install -m755 "$REDMI"/sensors/bin/* "$RF/usr/bin/"
  sudo install -m755 "$REDMI/sensors/ld-musl-aarch64.so.1" "$RF/usr/lib/ld-musl-aarch64.so.1"
  sudo mkdir -p "$RF/usr/lib/musl"
  sudo rsync -a "$REDMI/sensors/lib-musl/" "$RF/usr/lib/musl/"
  sudo mkdir -p "$RF/usr/share/qcom"
  [ -d "$REDMI/vendor-qcom/qcom" ] && \
    sudo rsync -a "$REDMI/vendor-qcom/qcom/" "$RF/usr/share/qcom/"
  echo ">> sensors (hexagonrpcd + musl runtime + -R tree) staged"
else
  echo "WARN: no $REDMI/sensors — run $REDMI/tools/fetch-firmware.sh for hexagonrpcd" >&2
fi
sudo chmod 755 "$RF/var/lib/tqftpserv"

# Only tqftpserv is staged from the rescue initramfs: its unit runs
# /usr/local/sbin/tqftpserv.  rmtfs and qrtr-ns come from the distro
# packages (their units use /usr/bin) — copying them here was redundant.
sudo mkdir -p "$RF/usr/local/sbin" "$RF/usr/local/bin"
[ -x "$REDMI/initramfs/tqftpserv" ] && sudo cp -a "$REDMI/initramfs/tqftpserv" "$RF/usr/local/sbin/tqftpserv"
[ -x "$REDMI/initramfs/qrtr-lookup" ] && sudo cp -a "$REDMI/initramfs/qrtr-lookup" "$RF/usr/local/sbin/qrtr-lookup"
[ -x "$REDMI/out/build/busybox" ] && sudo cp -a "$REDMI/out/build/busybox" "$RF/usr/local/bin/busybox"

# wifi / serial / stable-boot overlay (canonical copies under tools/rain-overlay)
OVER="$ROOT/tools/rain-overlay"
if [ -d "$OVER" ]; then
  sudo rsync -a "$OVER/" "$RF/"
  echo ">> rain-overlay applied ($(find "$OVER" -type f | wc -l) files)"
else
  echo "WARN: $OVER missing — skipping overlay" >&2
fi

sudo chown -R 0:0 "$RF/etc"
sudo chmod 755 "$RF/etc"
sudo chmod 600 "$RF/etc/netplan/"*.yaml 2>/dev/null || true
sudo find "$RF/etc/systemd/system" -maxdepth 2 -type f \( -name "*.conf" -o -name "*.service" \) -exec chmod 644 {} + 2>/dev/null || true
sudo chmod 644 "$RF/etc/NetworkManager/conf.d/"*.conf "$RF/etc/pulse/system.pa" \
  "$RF/etc/environment" "$RF/etc/profile.d/"*.sh "$RF/etc/udev/rules.d/"*.rules 2>/dev/null || true
[ -d "$RF/etc/bluetooth" ] && sudo chmod 555 "$RF/etc/bluetooth"
sudo chmod 755 "$RF/usr/local/sbin/"*.sh 2>/dev/null || true
sudo chmod 755 "$RF/usr/local/sbin/tqftpserv" "$RF/usr/local/sbin/nm-dispatcher-dummy.py" 2>/dev/null || true
sudo chmod 755 "$RF/usr/local/bin/busybox" 2>/dev/null || true
sudo chown -R 0:0 "$RF/usr/local/sbin" 2>/dev/null || true

# Stage adbd debs for offline install
if [ -d "$OVER/pkg/adbd" ]; then
  sudo mkdir -p "$RF/usr/local/share/rain-adbd-debs"
  sudo cp -a "$OVER/pkg/adbd/"*.deb "$RF/usr/local/share/rain-adbd-debs/" 2>/dev/null || true
fi

echo "=== [5/6] mask boot-time hazards, enable rain units ==="
# Mask cloud/snap/boot-hazard units. NM runs and manages BOTH wlan0 and the
# modem (netplan profiles 90-NM-*.yaml; wlan0 soft-hangs turned out to be a
# UFS-corruption artifact, NM scans fine). NM-wait-online stays masked: it
# only delays boot. The QRTR/modem helpers are NOT masked — the qcom-*
# chain (modem firmware + ath10k_snoc load) still needs them.
for u in NetworkManager-wait-online.service \
         cloud-init-local.service cloud-init-main.service cloud-init-network.service \
         cloud-config.service cloud-final.service cloud-init-hotplugd.socket \
         snapd.service snapd.seeded.service snapd.autoimport.service \
         snapd.apparmor.service snapd.core-fixup.service \
         open-vm-tools.service pollinate.service \
         systemd-networkd.service systemd-networkd.socket systemd-networkd-wait-online.service \
         apport.service apport-autoreport.path apport-autoreport.timer apport-forward.socket \
         lvm2-monitor.service blk-availability.service dm-event.socket lvm2-lvmpolld.socket \
         open-iscsi.service iscsid.socket lxd-installer.socket \
         ubuntu-advantage.service ua-reboot-cmds.service ua-timer.timer \
         secureboot-db.service vgauth.service \
         motd-news.timer update-notifier-download.timer update-notifier-motd.timer \
         xfs_scrub_all.timer mdcheck_continue.timer mdcheck_start.timer mdmonitor-oneshot.timer \
         fwupd-refresh.timer sysstat.service sysstat-collect.timer sysstat-rotate.timer sysstat-summary.timer; do
  sudo ln -sfn /dev/null "$RF/etc/systemd/system/${u}"
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
# wpa_supplicant НЕ убираем: NetworkManager поднимает его только по D-Bus,
# а эта активация в образе не срабатывает — без запущенного саппликанта
# wlan0 остаётся unavailable (проверено на alpha-20261014-05).
sudo rm -f "$RF/etc/systemd/system/multi-user.target.wants/rain-wifi-nm.service" 2>/dev/null || true

sudo chroot "$RF" bash -c '
systemctl enable rain-stable-boot.service usb-adb-gadget.service usb-adb-resume.service adbd.service getty@tty1.service 2>/dev/null || true
# hci0 comes up by itself (hci_uart built-in + bluetoothd); PulseAudio is a
# separate unit now — rain-bt used to be the only thing starting it.
systemctl enable bluetooth pulseaudio.service rain-serial-console.service 2>/dev/null || true
# pd-mapper is NOT enabled: the in-kernel QCOM_PD_MAPPER answers the
# ADSP/modem PD lookups (verified 2026-10-18).
systemctl enable qcom-firmware-stage.service qrtr-ns.service rmtfs.service tqftpserv.service 2>/dev/null || true
systemctl enable qcom-modem-start.service uim-provision.service uim-hotplug.service 2>/dev/null || true
systemctl enable ModemManager.service NetworkManager.service NetworkManager-dispatcher.service 2>/dev/null || true
systemctl enable ssh 2>/dev/null || true
systemctl enable wpa_supplicant.service 2>/dev/null || true
# Snapdragon Sensor Core (hexagonrpcd -> ADSP fastrpc)
systemctl enable sensors-setup.service hexagonrpcd-adsp-rootpd.service hexagonrpcd-adsp-sensorspd.service 2>/dev/null || true
# ASoC/WCD9375 stack (speaker + headset) needs no unit any more: every
# piece is built-in (=y) and the ADSP auto-boots (adsp_resource_init has
# auto_boot=true).  rain-stable-boot must still not stop remoteproc1.
# GNSS: the modem exposes QMI LOC/PDS (QRTR service 16); the ModemManager
# Location interface drives it and gnss-share republishes the NMEA on
# /var/run/gnss-share.sock for geoclue/apps (device_driver="mm").
systemctl enable rain-gps.timer 2>/dev/null || true
# gnss-share stops the receiver when the last client disconnects, so a fix
# (minutes of uninterrupted tracking) needs a permanent client.
' || true

# Host adb public key → pre-authorized adbd (no RSA prompt on a headless device)
if [ -f "$HOME/.android/adbkey.pub" ]; then
  sudo mkdir -p "$RF/etc/adb"
  sudo cp "$HOME/.android/adbkey.pub" "$RF/etc/adb/adb_keys"
  sudo chmod 644 "$RF/etc/adb/adb_keys"
fi
sudo mkdir -p "$RF/var/lib/rmtfs"

sudo tee "$RF/etc/issue" >/dev/null <<'EOF'
Ubuntu 26.04.1 LTS rain-ubuntu \n \l

WiFi+LTE: nmcli / nmtui (autoconnect configured)
EOF

echo "=== [6/6] pack ext4 ${IMG_SIZE_MB}M -> sparse ==="
ROOTIMG="$OUT/linux_rootfs.img"
sudo umount "$OUT/mnt-root" 2>/dev/null || true
sudo rm -f "$ROOTIMG"
sudo dd if=/dev/zero of="$ROOTIMG" bs=1M count="$IMG_SIZE_MB" status=progress
sudo mkfs.ext4 -L rain-ubuntu -O ^metadata_csum_seed "$ROOTIMG"
MNT="$OUT/mnt-root"
mkdir -p "$MNT"
sudo mount -o loop "$ROOTIMG" "$MNT"
sudo rsync -aHAX --numeric-ids \
  --exclude=dev/fd --exclude=dev/pts --exclude=dev/shm \
  --exclude=proc --exclude=sys --exclude=run \
  "$RF"/ "$MNT"/
sudo mkdir -p "$MNT/dev" "$MNT/proc" "$MNT/sys" "$MNT/run" "$MNT/tmp"
# rsync copies the source dir's owner onto the image root too; /, /usr,
# /usr/local etc. must be root:root or early-boot services fail (tmpfiles
# "unsafe path transition", dbus /dev/null EACCES). Host-uid leaks from
# overlay/firmware copies get the same fix; only /home/rain stays uid 1000.
sudo chown 0:0 "$MNT" && sudo chmod 755 "$MNT"
# The uid sweep must only touch files owned by whoever owned the source
# tree, never every root-owned file: chown clears setuid/setgid bits.
# In CI the whole script runs under sudo so id -u = 0 and find -uid 0
# would chown (and de-setuid) the entire rootfs. The leaked files all come
# from the overlay/firmware copies, so match the overlay dir's owner —
# root means nothing leaked and the sweep is skipped.
RSYNC_SRC_UID="$(stat -c %u "$OVER" 2>/dev/null || stat -c %u "$ROOT")"
if [ "$RSYNC_SRC_UID" != "0" ]; then
  sudo find "$MNT" -xdev -uid "$RSYNC_SRC_UID" ! -path "$MNT/home/rain*" -exec chown 0:0 {} + 2>/dev/null || true
fi
sudo chgrp 101 "$MNT/var/log" 2>/dev/null || true   # syslog:x:101 in image
sudo umount "$MNT"
img2simg "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"
ls -lh "$ROOTIMG" "$OUT/linux_rootfs.sparse.img"

echo "OK Ubuntu 26.04.1 rootfs ready"
grep PRETTY_NAME "$RF/etc/os-release" || true
echo "Flash: fastboot flash cust $OUT/linux_rootfs.sparse.img && fastboot continue"
