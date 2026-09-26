#!/usr/bin/env bash
# fetch-firmware.sh — download the proprietary blobs from the GitHub release.
#
# The tarball contains:
#   firmware/  → initramfs/lib/firmware (and out/build/initramfs/lib/firmware)
#   qcom/      → vendor-qcom/qcom (hexagonrpcd -R tree, used by the pmOS build)
#
# Override with env vars:
#   FIRMWARE_REPO=owner/repo  FIRMWARE_TAG=<tag>
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${FIRMWARE_REPO:-Damir00109/Redmi10c}"
TAG="${FIRMWARE_TAG:-firmware-2026.09.25}"
ASSET="redmi10c-firmware.tar.zst"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo ">> fetching $ASSET from $REPO@$TAG"
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
	gh release download "$TAG" -R "$REPO" -p "$ASSET" -O "$TMP/$ASSET"
else
	curl -L --fail --retry 3 -o "$TMP/$ASSET" \
		"https://github.com/$REPO/releases/download/$TAG/$ASSET"
fi

tar --zstd -xf "$TMP/$ASSET" -C "$TMP"

for lib in "$R/initramfs/lib" "$R/out/build/initramfs/lib"; do
	[ -d "$(dirname "$lib")" ] || continue
	mkdir -p "$lib"
	cp -a "$TMP/firmware" "$lib/"
	echo ">> unpacked firmware into $lib/firmware"
done

if [ -d "$TMP/qcom" ]; then
	mkdir -p "$R/vendor-qcom"
	rm -rf "$R/vendor-qcom/qcom"
	cp -a "$TMP/qcom" "$R/vendor-qcom/qcom"
	echo ">> unpacked hexagonrpcd tree into $R/vendor-qcom/qcom"
fi
