#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Build and install neard (NFC management daemon) into the Ubuntu rootfs
# chroot ($RF). neard is no longer in the Ubuntu/Debian archives, so it is
# built from https://git.kernel.org/pub/scm/network/nfc/neard.git with meson.
#
# Usage: build-neard.sh /path/to/rootfs   (run BEFORE the image is packed)
set -euo pipefail

RF="${1:?usage: $0 <rootfs dir>}"
NEARD_REPO="${NEARD_REPO:-https://git.kernel.org/pub/scm/network/nfc/neard.git}"
NEARD_REF="${NEARD_REF:-master}"
SRC=/tmp/neard-src

sudo cp -a /tmp/neard "$RF$SRC" 2>/dev/null || {
  rm -rf /tmp/neard
  git clone --depth 1 -b "$NEARD_REF" "$NEARD_REPO" /tmp/neard
  sudo cp -a /tmp/neard "$RF$SRC"
}

sudo chroot "$RF" bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq gcc make meson ninja-build pkg-config \
  libglib2.0-dev libdbus-1-dev libnl-3-dev libnl-genl-3-dev libsystemd-dev
cd /tmp/neard-src
meson setup --wipe build -Dsystemd=disabled -Dtools=true
ninja -C build
ninja -C build install
rm -rf /tmp/neard-src
apt-get purge -y -qq gcc g++ make meson ninja-build pkg-config \
  libglib2.0-dev libdbus-1-dev libnl-3-dev libnl-genl-3-dev libsystemd-dev \
  cpp cpp-15 gcc-15 libcc1-0 binutils binutils-aarch64-linux-gnu || true
apt-get clean
rm -rf /var/lib/apt/lists/*
'

# systemd unit + D-Bus activation (meson build has systemd support disabled,
# so the unit is written here)
sudo tee "$RF/etc/systemd/system/neard.service" >/dev/null <<'EOF'
[Unit]
Description=Near Field Communication (NFC) management daemon
After=dbus.service
Wants=dbus.service

[Service]
ExecStart=/usr/local/libexec/nfc/neard -n
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
sudo mkdir -p "$RF/usr/share/dbus-1/system-services"
sudo tee "$RF/usr/share/dbus-1/system-services/org.neard.service" >/dev/null <<'EOF'
[D-BUS Service]
Name=org.neard
Exec=/usr/local/libexec/nfc/neard --replace
SystemdService=neard.service
EOF
sudo chroot "$RF" systemctl enable neard.service

echo "neard installed into $RF"
