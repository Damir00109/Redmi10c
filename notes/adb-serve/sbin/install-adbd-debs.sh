#!/bin/bash
# Install Ubuntu arm64 adbd + android libs from staged debs (offline-friendly).
set -euo pipefail
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# Allow: /usr/local/sbin/install-adbd-debs.sh  OR  run from overlay tree
if [ -d /usr/local/share/rain-adbd-debs ]; then
  DEBDIR=/usr/local/share/rain-adbd-debs
elif [ -d "$SCRIPT_DIR/../../../pkg/adbd" ]; then
  DEBDIR=$(cd "$SCRIPT_DIR/../../../pkg/adbd" && pwd)
elif [ -d /tmp/rain-adbd-debs ]; then
  DEBDIR=/tmp/rain-adbd-debs
else
  echo "No deb directory found (expected /usr/local/share/rain-adbd-debs or /tmp/rain-adbd-debs)" >&2
  exit 1
fi

echo "Installing debs from $DEBDIR"
shopt -s nullglob
DEBS=( "$DEBDIR"/android-lib*.deb "$DEBDIR"/libprotobuf*.deb "$DEBDIR"/adbd_*.deb )
if [ ${#DEBS[@]} -eq 0 ]; then
  echo "No matching debs in $DEBDIR" >&2
  exit 1
fi

dpkg -i "${DEBS[@]}" || true
# Fix any missing shared deps from distro repos if network is up
apt-get install -y -f --no-install-recommends 2>/dev/null || true

command -v adbd >/dev/null
adbd_path=$(command -v adbd)
echo "adbd OK: $adbd_path"
ls -l "$adbd_path"
