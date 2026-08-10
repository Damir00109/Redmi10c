#!/bin/bash
# Run on phone after: wget -O- http://HOST:8765/bootstrap.sh | bash
# Or: curl -fsSL http://HOST:8765/bootstrap.sh | bash
set -euo pipefail
HOST_IP="${1:-}"
if [ -z "$HOST_IP" ]; then
  echo "usage: bootstrap.sh <host-ip>" >&2
  exit 1
fi
BASE="http://${HOST_IP}:8765"
WORKDIR=/tmp/rain-adb-inst
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"

echo "=== fetch from $BASE ==="
for f in \
  sbin/usb-adb-gadget.sh \
  sbin/rain-enable-adb-gadget.sh \
  sbin/install-adbd-debs.sh \
  systemd/usb-adb-gadget.service \
  systemd/adbd.service \
  debs/android-libbase_34.0.5-12build1_arm64.deb \
  debs/android-libboringssl_14.0.0+r45-2build1_arm64.deb \
  debs/android-libcutils_34.0.5-12build1_arm64.deb \
  debs/android-liblog_34.0.5-12build1_arm64.deb \
  debs/adbd_34.0.5-12build1_arm64.deb \
  debs/libprotobuf32t64_3.21.12-14ubuntu1.4_arm64.deb
do
  mkdir -p "$(dirname "$f")"
  wget -q -O "$f" "$BASE/$f" || curl -fsSL -o "$f" "$BASE/$f"
  echo "  ok $f ($(wc -c <"$f") bytes)"
done

echo "=== install scripts ==="
install -d /usr/local/sbin
install -m 0755 sbin/usb-adb-gadget.sh /usr/local/sbin/
install -m 0755 sbin/rain-enable-adb-gadget.sh /usr/local/sbin/
install -m 0755 sbin/install-adbd-debs.sh /usr/local/sbin/
install -d /etc/systemd/system
install -m 0644 systemd/usb-adb-gadget.service /etc/systemd/system/
install -m 0644 systemd/adbd.service /etc/systemd/system/

echo "=== install debs ==="
export DEBIAN_FRONTEND=noninteractive
dpkg -i debs/*.deb || apt-get -y -f install

echo "=== switch gadget ACM -> ADB ==="
/usr/local/sbin/rain-enable-adb-gadget.sh

echo "=== done — reboot to bind ADB cleanly ==="
sync
systemctl reboot
