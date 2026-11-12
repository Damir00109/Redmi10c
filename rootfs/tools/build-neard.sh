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
# so the unit is written here).  The kernel rfkill device for nfc0 starts
# soft-blocked; unblock it before neard powers the adapter on.
sudo tee "$RF/etc/systemd/system/neard.service" >/dev/null <<'EOF'
[Unit]
Description=Near Field Communication (NFC) management daemon
After=dbus.service
Wants=dbus.service

[Service]
ExecStartPre=/usr/sbin/rfkill unblock nfc
ExecStart=/usr/local/libexec/nfc/neard -n
ExecStartPost=/usr/local/bin/nfc-poweron.sh
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
# neard discovers the adapter but leaves it unpowered; power it on and start
# the initiator poll loop as soon as the bus name is owned.
sudo tee "$RF/usr/local/bin/nfc-poweron.sh" >/dev/null <<'EOF'
#!/bin/sh
# Power on the NFC adapter and start the initiator poll loop once neard
# owns its bus name. neard discovers adapters but does not power them
# by itself at boot, so this runs as neard.service ExecStartPost.
for i in $(seq 1 30); do
  gdbus call --system --dest org.freedesktop.DBus \
    --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner org.neard >/dev/null 2>&1 && break
  sleep 1
done
path=$(busctl --system tree org.neard 2>/dev/null | grep -o "/org/neard/nfc[0-9]*" | tail -1)
[ -z "$path" ] && exit 0
gdbus call --system --dest org.neard --object-path "$path" \
  --method org.freedesktop.DBus.Properties.Set org.neard.Adapter Powered "<true>" >/dev/null 2>&1
sleep 1
gdbus call --system --dest org.neard --object-path "$path" \
  --method org.neard.Adapter.StartPollLoop Initiator >/dev/null 2>&1
EOF
sudo chmod 755 "$RF/usr/local/bin/nfc-poweron.sh"
# meson installs the bus policy under /usr/local/etc/dbus-1/system.d, which
# dbus-daemon does NOT read — the allow rules for org.neard must live in
# /etc/dbus-1/system.d or neard cannot own its bus name.
sudo cp "$RF/usr/local/etc/dbus-1/system.d/org.neard.conf" \
  "$RF/etc/dbus-1/system.d/org.neard.conf"
sudo chmod 644 "$RF/etc/dbus-1/system.d/org.neard.conf"
sudo mkdir -p "$RF/usr/share/dbus-1/system-services"
sudo tee "$RF/usr/share/dbus-1/system-services/org.neard.service" >/dev/null <<'EOF'
[D-BUS Service]
Name=org.neard
Exec=/usr/local/libexec/nfc/neard --replace
SystemdService=neard.service
EOF
sudo chroot "$RF" systemctl enable neard.service

echo "neard installed into $RF"
